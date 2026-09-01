/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault.h
 * Description: ISA-neutral page fault decode facade; portable MM policy consumes
 *              struct page_fault_info instead of reading CR2/FAR directly.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>

struct page_fault_info
{
	uintptr_t address;
	uintptr_t ip;
	uintptr_t sp;
	int user;      /* 1 = fault in user mode */
	int write;
	int present;
	int exec;
	int reserved;
};

/*
 * Decode fault from arch entry context (CR2/FAR, error code, IRQ frame).
 * x86-64: errcode is #PF error code; frame is ISR stack (rip at [2], rsp at [5]).
 * Returns 0 or -errno (-EINVAL, -EOPNOTSUPP on stub ISAs).
 */
int page_fault_decode(struct page_fault_info *out, uint64_t errcode,
		      void *irq_frame);

/*
 * Portable demand paging, COW, and SIGSEGV policy (mm/page_fault.c).
 * irq_frame is ISA-specific; required for signal delivery on x86-64.
 */
void mm_page_fault_handle(const struct page_fault_info *info, void *irq_frame);
