/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: busybox_load_early.c
 * Description: Load embedded BusyBox aarch64 ELF and run `echo` applet in EL0.
 */

#include "elf_load_early.h"
#include "mmu_early.h"
#include "pl011.h"
#include "rootfs_early.h"
#include "syscall_early.h"

#include <stdint.h>
#include <ir0/arch_elf.h>
#include <ir0/boot_log.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define PT_LOAD 1
#define PF_X 1
#define PAGE_SIZE 4096UL

#define BB_STACK_TOP 0x441a0000UL
#define BB_STACK_PAGES 4U
#define SPSR_DAIF_MASKED 0x3c0UL
#define SPSR_MODE_EL0T 0x0UL

struct elf64_ehdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct elf64_phdr
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

extern const uint8_t busybox_aarch64_blob[];
extern const uint8_t busybox_aarch64_blob_end[];

static int g_bb_mode;
static int g_bb_wrote;
static int g_bb_init_wrote;
static int g_bb_stage;
static uint64_t g_bb_entry;

int arm64_busybox_mode(void)
{
	return g_bb_mode;
}

int arm64_busybox_wrote(void)
{
	return g_bb_wrote;
}

void arm64_busybox_note_write(const char *buf, uint64_t len)
{
	static const char expect_el0[] = "ARM64_BUSYBOX_EL0_OK";
	static const char expect_init[] = "ARM64_BUSYBOX_INIT_OK";
	uint64_t i;

	if (!buf)
		return;

	if (g_bb_stage == 0)
	{
		if (len < sizeof(expect_el0) - 1)
			return;
		for (i = 0; i < sizeof(expect_el0) - 1; i++)
		{
			if (buf[i] != expect_el0[i])
				return;
		}
		g_bb_wrote = 1;
		return;
	}

	if (len < sizeof(expect_init) - 1)
		return;
	for (i = 0; i < sizeof(expect_init) - 1; i++)
	{
		if (buf[i] != expect_init[i])
			return;
	}
	g_bb_init_wrote = 1;
}

static void copy_bytes(void *dst, const void *src, uint64_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	while (n--)
		*d++ = *s++;
}

static void zero_bytes(void *dst, uint64_t n)
{
	uint8_t *d = dst;

	while (n--)
		*d++ = 0;
}

static int map_range(uint64_t va, uint64_t memsz, int exec)
{
	uint64_t page;
	uint64_t end = va + memsz;

	if (end < va)
		return -1;
	for (page = va & ~(PAGE_SIZE - 1UL); page < end; page += PAGE_SIZE)
	{
		if (arm64_mmu_map_user_page_flags(page, exec) != 0)
			return -1;
	}
	return 0;
}

static int load_elf(const uint8_t *blob, uint64_t blob_len, uint64_t *entry_out)
{
	const struct elf64_ehdr *ehdr;
	uint16_t i;

	if (blob_len < sizeof(*ehdr))
		return -1;
	ehdr = (const struct elf64_ehdr *)blob;
	if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
	    ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
		return -1;
	if (ehdr->e_type != ET_EXEC || !arch_elf_machine_supported(ehdr->e_machine))
		return -1;
	if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > blob_len)
		return -1;

	for (i = 0; i < ehdr->e_phnum; i++)
	{
		const struct elf64_phdr *ph;
		uint64_t off = ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize;
		uint64_t va;
		uint64_t memsz;
		int exec;

		ph = (const struct elf64_phdr *)(blob + off);
		if (ph->p_type != PT_LOAD)
			continue;
		if (ph->p_offset + ph->p_filesz > blob_len)
			return -1;
		va = ph->p_vaddr;
		memsz = ph->p_memsz;
		if (memsz < ph->p_filesz)
			return -1;
		exec = (ph->p_flags & PF_X) ? 1 : 0;
		if (map_range(va, memsz, exec) != 0)
			return -1;
		copy_bytes((void *)(uintptr_t)va, blob + ph->p_offset, ph->p_filesz);
		if (memsz > ph->p_filesz)
			zero_bytes((void *)(uintptr_t)(va + ph->p_filesz),
				   memsz - ph->p_filesz);
	}

	*entry_out = ehdr->e_entry;
	return 0;
}

static void setup_busybox_argv_stack(uint64_t sp_top, int init_stage)
{
	/*
	 * Stage 0: echo ARM64_BUSYBOX_EL0_OK.
	 * Stage 1: init demo — openat/read/fstat on "/init" validated in EL1
	 * via arm64_rootfs_smoke_init() before this drop.  BusyBox init applet
	 * needs clone/execve/wait (still ENOSYS), so argv[0] stays "echo".
	 */
	char *str_area = (char *)(uintptr_t)(sp_top - 256UL);
	uint64_t *sp;
	char *p = str_area;
	uint64_t *aux;
	uint64_t *randp;

	copy_bytes(p, "echo", 5);
	p += 5;
	if (init_stage)
		copy_bytes(p, "ARM64_BUSYBOX_INIT_OK", 22);
	else
		copy_bytes(p, "ARM64_BUSYBOX_EL0_OK", 20);

	randp = (uint64_t *)(uintptr_t)(sp_top - 256UL - 16UL);
	randp[0] = 0x72706e646f6d3149ULL;
	randp[1] = 0x495231302e302e31ULL;

	sp = (uint64_t *)(uintptr_t)(sp_top - 256UL - 16UL - 8UL * 16UL);
	sp[0] = 2;
	sp[1] = (uint64_t)(uintptr_t)str_area;
	sp[2] = (uint64_t)(uintptr_t)(str_area + 5);
	sp[3] = 0;
	sp[4] = 0;
	aux = &sp[5];
	aux[0] = 6;
	aux[1] = 4096UL;
	aux[2] = 25;
	aux[3] = (uint64_t)(uintptr_t)randp;
	aux[4] = 0;
	aux[5] = 0;
	__asm__ volatile("msr sp_el0, %0" :: "r"(sp) : "memory");
}

static void enable_fp_simd(void)
{
	uint64_t cpacr;

	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr |= (3UL << 20);
	__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void arm64_after_busybox(void)
{
	extern void arm64_enter_el0(void);

	g_bb_mode = 0;
	if (g_bb_stage == 0)
	{
		if (g_bb_wrote)
			ir0_boot_smoke("ARM64_BUSYBOX_EL0_OK");
		else
			ir0_boot_smoke("ARM64_BUSYBOX_EL0_FAIL");
		g_bb_stage = 1;
		if (arm64_busybox_init_el0() != 0)
			arm64_enter_el0();
		return;
	}

	if (g_bb_init_wrote)
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_OK");
	else
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_FAIL");
	arm64_enter_el0();
}

static void enter_busybox_el0(uint64_t entry, int init_stage)
{
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL0T;

	enable_fp_simd();
	arm64_syscall_reset_busybox_heap();
	g_bb_mode = 1;
	setup_busybox_argv_stack(BB_STACK_TOP, init_stage);
	if (init_stage)
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_EL0_DROP");
	else
		ir0_boot_smoke("ARM64_BUSYBOX_EL0_DROP");

	__asm__ volatile(
		"msr	elr_el1, %0\n"
		"msr	spsr_el1, %1\n"
		"isb\n"
		"eret\n"
		:
		: "r"(entry), "r"(spsr)
		: "memory");
	__builtin_unreachable();
}

int arm64_busybox_init_el0(void)
{
	if (g_bb_entry == 0)
		return -1;
	if (arm64_rootfs_smoke_init() != 0)
	{
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_FAIL");
		return -1;
	}
	g_bb_init_wrote = 0;
	enter_busybox_el0(g_bb_entry, 1);
	return 0;
}

int arm64_busybox_el0(void)
{
	const uint8_t *blob = busybox_aarch64_blob;
	uint64_t blob_len = (uint64_t)(busybox_aarch64_blob_end - busybox_aarch64_blob);
	uint64_t entry;
	unsigned i;

	g_bb_wrote = 0;
	g_bb_init_wrote = 0;
	g_bb_stage = 0;
	g_bb_mode = 0;
	arm64_rootfs_early_init();

	for (i = 0; i < BB_STACK_PAGES; i++)
	{
		uint64_t page = BB_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;

		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
		{
			ir0_boot_smoke("ARM64_BUSYBOX_LOAD_FAIL");
			return -1;
		}
		zero_bytes((void *)(uintptr_t)page, PAGE_SIZE);
	}

	if (load_elf(blob, blob_len, &entry) != 0)
	{
		ir0_boot_smoke("ARM64_BUSYBOX_LOAD_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_BUSYBOX_LOAD_OK");
	g_bb_entry = entry;
	enter_busybox_el0(entry, 0);
	return 0;
}
