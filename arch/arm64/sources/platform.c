/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: platform.c
 * Description: ARM64 platform ops — virt PSCI + RPi stub; board selects via arm64_board.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>
#include <arch/common/arch_interface.h>
#include <ir0/platform_ops.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t get_cpu_id(void)
{
	return 0;
}

uint32_t get_cpu_count(void)
{
	return 1;
}

int get_cpu_vendor(char *vendor_buf)
{
	if (!vendor_buf)
		return -1;
	strncpy(vendor_buf, "Unknown", 8);
	return -1;
}

int get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping)
{
	if (family)
		*family = 0;
	if (model)
		*model = 0;
	if (stepping)
		*stepping = 0;
	return -1;
}

int get_cpuid_max_leaf(uint32_t *max_leaf)
{
	if (max_leaf)
		*max_leaf = 0;
	return -1;
}

int get_cpu_brand_string(char *buf, size_t size)
{
	(void)buf;
	(void)size;
	return -1;
}

int get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx)
{
	if (out_edx)
		*out_edx = 0;
	if (out_ecx)
		*out_ecx = 0;
	return -1;
}

uint32_t get_cpu_clflush_size(void)
{
	return 0;
}

int hypervisor_present(void)
{
	return 0;
}

int hypervisor_vendor(char *buf, size_t n)
{
	if (buf && n)
		buf[0] = '\0';
	return -1;
}

static void __attribute__((noreturn)) arm64_halt_loop(void)
{
	disable_interrupts();
	for (;;)
		cpu_wait();
}

static void arm64_psci_hvc(uint64_t fn)
{
	register uint64_t x0 __asm__("x0") = fn;

	__asm__ volatile("hvc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
}

static void arm64_virt_halt(void)
{
	arm64_halt_loop();
}

static void arm64_virt_reboot(void)
{
	/* PSCI 0.2 SYSTEM_RESET — QEMU virt HVC conduit. */
	arm64_psci_hvc(0x84000009UL);
	arm64_halt_loop();
}

static void arm64_virt_poweroff(void)
{
	/* PSCI 0.2 SYSTEM_OFF — QEMU virt HVC conduit. */
	arm64_psci_hvc(0x84000008UL);
	arm64_halt_loop();
}

/* QEMU virt (default) — selected via arm64_board_get(). */
const struct ir0_platform_ops arm64_virt_platform_ops = {
	.halt = arm64_virt_halt,
	.reboot = arm64_virt_reboot,
	.poweroff = arm64_virt_poweroff,
};

/*
 * Raspberry Pi stub — same WFI halt until PSCI/mailbox power is wired.
 * Selected via arm64_board (rpi4 / rpi5) or ir0_platform_ops_set().
 */
static void arm64_rpi_halt(void)
{
	arm64_halt_loop();
}

static void arm64_rpi_reboot(void)
{
	arm64_halt_loop();
}

static void arm64_rpi_poweroff(void)
{
	arm64_halt_loop();
}

const struct ir0_platform_ops arm64_rpi_platform_ops = {
	.halt = arm64_rpi_halt,
	.reboot = arm64_rpi_reboot,
	.poweroff = arm64_rpi_poweroff,
};

static const struct ir0_platform_ops *g_platform_ops = &arm64_virt_platform_ops;

const struct ir0_platform_ops *ir0_platform_ops_get(void)
{
	return g_platform_ops;
}

void ir0_platform_ops_set(const struct ir0_platform_ops *ops)
{
	if (ops)
		g_platform_ops = ops;
}

void system_halt(void)
{
	ir0_platform_ops_get()->halt();
	for (;;)
		cpu_wait();
}

void system_reboot(void)
{
	ir0_platform_ops_get()->reboot();
	for (;;)
		cpu_wait();
}

void system_poweroff(void)
{
	ir0_platform_ops_get()->poweroff();
	for (;;)
		cpu_wait();
}
