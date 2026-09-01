/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_page_fault.c
 * Description: x86-64 #PF decode (CR2 + error code + ISR frame) for portable MM.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_page_fault.h>
#include <ir0/errno.h>

int page_fault_decode(struct page_fault_info *out, uint64_t errcode,
			   void *irq_frame)
{
	uint64_t *frame = (uint64_t *)irq_frame;
	uint64_t fault_addr;

	if (!out)
		return -EINVAL;

	asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

	out->address = (uintptr_t)fault_addr;
	out->ip = frame ? (uintptr_t)frame[2] : 0;
	out->sp = frame ? (uintptr_t)frame[5] : 0;
	out->user = (errcode & 4) != 0;
	out->write = (errcode & 2) != 0;
	out->present = (errcode & 1) != 0;
	out->exec = (errcode & 16) != 0;
	out->reserved = (errcode & 8) != 0;

	return 0;
}
