/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_page_fault.c
 * Description: ARM64 page fault decode stub until EL0 #PF parity exists.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_page_fault.h>
#include <ir0/errno.h>

int page_fault_decode(struct page_fault_info *out, uint64_t errcode,
			   void *irq_frame)
{
	(void)errcode;
	(void)irq_frame;

	if (!out)
		return -EINVAL;

	return -EOPNOTSUPP;
}
