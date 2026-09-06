/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the root for full license information.
 *
 * File: arch_pf_debug.c
 * Description: arm64 page-fault debug stubs (x86 frame layout lives on x86-64).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_pf_debug.h>

void pf_debug_stack_adjacent(uint64_t *frame, uint64_t fault_addr,
				 const struct page_fault_info *info,
				 process_t *p)
{
	(void)frame;
	(void)fault_addr;
	(void)info;
	(void)p;
}

void pf_debug_memmove_fault(uint64_t *frame, uint64_t fault_addr,
				const struct page_fault_info *info,
				process_t *p)
{
	(void)frame;
	(void)fault_addr;
	(void)info;
	(void)p;
}
