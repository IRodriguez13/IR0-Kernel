/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_load_early.c
 * Description: Load embedded musl hello ELF64 into DRAM and drop to EL0 entry.
 */

#include "elf_load_early.h"
#include "mmu_early.h"
#include "pl011.h"

#include <stdint.h>
#include <ir0/arch_elf.h>
#include <ir0/boot_log.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define PT_LOAD 1
#define PF_X 1
#define PAGE_SIZE 4096UL

#define MUSL_STACK_TOP 0x43180000UL
#define MUSL_STACK_PAGES 2U
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

extern const uint8_t hello_aarch64_blob[];
extern const uint8_t hello_aarch64_blob_end[];

static int g_musl_wrote;
static int g_musl_mode;

int arm64_musl_hello_wrote(void)
{
	return g_musl_wrote;
}

int arm64_musl_mode(void)
{
	return g_musl_mode;
}

void arm64_musl_note_write(const char *buf, uint64_t len)
{
	static const char expect[] = "IR0_MUSL_AARCH64_HELLO_OK";
	uint64_t i;

	if (!buf || len < sizeof(expect) - 1)
		return;
	for (i = 0; i < sizeof(expect) - 1; i++)
	{
		if (buf[i] != expect[i])
			return;
	}
	g_musl_wrote = 1;
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
	if (ehdr->e_type != ET_EXEC || !elf_machine_supported(ehdr->e_machine))
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

static void setup_auxv_stack(uint64_t sp_top)
{
	uint64_t *sp = (uint64_t *)(uintptr_t)(sp_top - 8UL * 4UL);

	sp[0] = 0;
	sp[1] = 0;
	sp[2] = 0;
	sp[3] = 0;
	__asm__ volatile("msr sp_el0, %0" :: "r"(sp) : "memory");
}

void arm64_after_musl(void)
{
	g_musl_mode = 0;
	if (g_musl_wrote)
		ir0_boot_smoke("ARM64_MUSL_HELLO_OK");
	else
		ir0_boot_smoke("ARM64_MUSL_HELLO_FAIL");
	/* BusyBox echo applet, then F7c EL0 syscall smoke. */
	if (arm64_busybox_el0() != 0)
	{
		extern void arm64_enter_el0(void);

		arm64_enter_el0();
	}
}

static void enable_fp_simd(void)
{
	uint64_t cpacr;

	/* CPACR_EL1.FPEN = 0b11 — don't trap FP/SIMD at EL0/EL1 (musl memset). */
	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr |= (3UL << 20);
	__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

static void enter_musl_el0(uint64_t entry)
{
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL0T;

	enable_fp_simd();
	g_musl_mode = 1;
	setup_auxv_stack(MUSL_STACK_TOP);
	ir0_boot_smoke("ARM64_MUSL_EL0_DROP");

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

int arm64_musl_hello_el0(void)
{
	const uint8_t *blob = hello_aarch64_blob;
	uint64_t blob_len = (uint64_t)(hello_aarch64_blob_end - hello_aarch64_blob);
	uint64_t entry;
	unsigned i;

	g_musl_wrote = 0;
	g_musl_mode = 0;

	for (i = 0; i < MUSL_STACK_PAGES; i++)
	{
		uint64_t page = MUSL_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;

		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
		{
			ir0_boot_smoke("ARM64_MUSL_LOAD_FAIL");
			return -1;
		}
		zero_bytes((void *)(uintptr_t)page, PAGE_SIZE);
	}

	if (load_elf(blob, blob_len, &entry) != 0)
	{
		ir0_boot_smoke("ARM64_MUSL_LOAD_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_MUSL_LOAD_OK");
	enter_musl_el0(entry);
	return 0;
}
