/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_mm.c
 * Description: x86-64 PML4 user/kernel half helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_mm.h>
#include <mm/paging.h>

unsigned mm_user_root_slots(void)
{
	return 256;
}

unsigned mm_root_slots(void)
{
	return 512;
}

void mm_copy_kernel_half(uint64_t *dst_root, const uint64_t *src_root)
{
	unsigned i;
	unsigned user_slots;
	unsigned total;

	if (!dst_root || !src_root)
		return;

	user_slots = mm_user_root_slots();
	total = mm_root_slots();
	for (i = user_slots; i < total; i++)
	{
		if (src_root[i] & PAGE_PRESENT)
			dst_root[i] = src_root[i];
	}
}

int mm_user_va_ok(uintptr_t addr, size_t size)
{
	uintptr_t end;

	/* ELF load floor … canonical low half (matches historical copy_user). */
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
