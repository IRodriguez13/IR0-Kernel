/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_mm.c
 * Description: ARM64 root-table helpers (TTBR0-only identity model).
 *
 * IR0 arm64 uses a single TTBR0 root (mmu_early / mm_activate) — no TTBR1
 * high-half kernel yet. Kernel/shared identity is installed by
 * create_process_page_directory() via map_supervisor_identity_low(), not by
 * copying a PML4-style kernel half. COW/unmap walks the full 512-slot root.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_mm.h>

unsigned mm_user_root_slots(void)
{
	/* Entire TTBR0 L0 is process-owned for walk/COW (no TTBR1 split). */
	return 512;
}

unsigned mm_root_slots(void)
{
	return 512;
}

void mm_copy_kernel_half(uint64_t *dst_root, const uint64_t *src_root)
{
	(void)dst_root;
	(void)src_root;
	/*
	 * No root-slot kernel half: TTBR1 not wired. Caller maps supervisor
	 * identity separately (see create_process_page_directory).
	 */
}

int mm_user_va_ok(uintptr_t addr, size_t size)
{
	uintptr_t end;

	/*
	 * Portable mm path: same canonical low-half window as x86-64 until
	 * TTBR0/TTBR1 split lands. Early EL0 probes still use
	 * arm64_mmu_user_buf_ok behind their own wrappers.
	 */
	const uintptr_t user_lo = 0x00400000UL;
	const uintptr_t user_hi = 0x00007FFFFFFFFFFFUL;

	if (addr == 0)
		return 0;
	end = addr + size;
	if (end < addr)
		return 0;
	if (addr < user_lo || end > user_hi)
		return 0;
	return 1;
}
