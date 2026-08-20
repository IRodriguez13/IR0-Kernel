/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_syscall_frame_x86_64.h
 * Description: Linux x86-64 syscall user-frame (pt_regs subset) and accessors.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Layout matches syscall_insn_entry_64.asm capture. Portable code must use
 * arch_syscall_frame_ip/sp/flags/arg — not these GPR names.
 */
typedef struct arch_syscall_frame
{
	uint64_t rip;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t r10;
	uint64_t r8;
	uint64_t r9;
} arch_syscall_frame_t;

static inline uint64_t arch_syscall_frame_ip(const arch_syscall_frame_t *sf)
{
	return sf ? sf->rip : 0;
}

static inline uint64_t arch_syscall_frame_sp(const arch_syscall_frame_t *sf)
{
	return sf ? sf->rsp : 0;
}

static inline uint64_t arch_syscall_frame_flags(const arch_syscall_frame_t *sf)
{
	return sf ? sf->rflags : 0;
}

static inline void arch_syscall_frame_set_ip(arch_syscall_frame_t *sf, uint64_t ip)
{
	if (sf)
		sf->rip = ip;
}

static inline void arch_syscall_frame_set_sp(arch_syscall_frame_t *sf, uint64_t sp)
{
	if (sf)
		sf->rsp = sp;
}

static inline void arch_syscall_frame_set_flags(arch_syscall_frame_t *sf,
						uint64_t flags)
{
	if (sf)
		sf->rflags = flags;
}

/* Linux x86-64 syscall ABI: 0=rdi, 1=rsi, 2=rdx, 3=r10, 4=r8, 5=r9. */
static inline uint64_t arch_syscall_frame_arg(const arch_syscall_frame_t *sf,
					      unsigned n)
{
	if (!sf)
		return 0;
	switch (n)
	{
	case 0:
		return sf->rdi;
	case 1:
		return sf->rsi;
	case 2:
		return sf->rdx;
	case 3:
		return sf->r10;
	case 4:
		return sf->r8;
	case 5:
		return sf->r9;
	default:
		return 0;
	}
}

static inline void arch_syscall_frame_set_arg(arch_syscall_frame_t *sf,
					      unsigned n, uint64_t v)
{
	if (!sf)
		return;
	switch (n)
	{
	case 0:
		sf->rdi = v;
		break;
	case 1:
		sf->rsi = v;
		break;
	case 2:
		sf->rdx = v;
		break;
	case 3:
		sf->r10 = v;
		break;
	case 4:
		sf->r8 = v;
		break;
	case 5:
		sf->r9 = v;
		break;
	default:
		break;
	}
}
