/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_syscall_frame_arm64.h
 * Description: Linux AArch64 syscall user-frame and semantic accessors.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Snapshot of EL0 GPRs + exception return state at SVC entry.
 * Layout matches vectors.S exc_entry_frame (x0@0 … x30@240) plus ELR/SPSR/SP.
 * Portable code must use syscall_frame_ip/sp/flags/arg.
 *
 * Linux AAPCS64 syscall ABI: number in x8, args x0–x5, retval in x0.
 */
typedef struct arch_syscall_frame
{
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29;
	uint64_t x30;
	uint64_t sp;
	uint64_t elr;
	uint64_t spsr;
} arch_syscall_frame_t;

_Static_assert(offsetof(arch_syscall_frame_t, x30) == 30 * sizeof(uint64_t),
	       "x0..x30 packed for vectors.S exc_entry_frame");
_Static_assert(offsetof(arch_syscall_frame_t, sp) == 31 * sizeof(uint64_t),
	       "sp follows x30");

static inline uint64_t syscall_frame_ip(const arch_syscall_frame_t *sf)
{
	return sf ? sf->elr : 0;
}

static inline uint64_t syscall_frame_sp(const arch_syscall_frame_t *sf)
{
	return sf ? sf->sp : 0;
}

static inline uint64_t syscall_frame_flags(const arch_syscall_frame_t *sf)
{
	return sf ? sf->spsr : 0;
}

static inline void syscall_frame_set_ip(arch_syscall_frame_t *sf, uint64_t ip)
{
	if (sf)
		sf->elr = ip;
}

static inline void syscall_frame_set_sp(arch_syscall_frame_t *sf, uint64_t sp)
{
	if (sf)
		sf->sp = sp;
}

static inline void syscall_frame_set_flags(arch_syscall_frame_t *sf,
						uint64_t flags)
{
	if (sf)
		sf->spsr = flags;
}

/* Linux AArch64 syscall ABI: args x0–x5. */
static inline uint64_t syscall_frame_arg(const arch_syscall_frame_t *sf,
					      unsigned n)
{
	if (!sf)
		return 0;
	switch (n)
	{
	case 0:
		return sf->x0;
	case 1:
		return sf->x1;
	case 2:
		return sf->x2;
	case 3:
		return sf->x3;
	case 4:
		return sf->x4;
	case 5:
		return sf->x5;
	default:
		return 0;
	}
}

static inline void syscall_frame_set_arg(arch_syscall_frame_t *sf,
					      unsigned n, uint64_t v)
{
	if (!sf)
		return;
	switch (n)
	{
	case 0:
		sf->x0 = v;
		break;
	case 1:
		sf->x1 = v;
		break;
	case 2:
		sf->x2 = v;
		break;
	case 3:
		sf->x3 = v;
		break;
	case 4:
		sf->x4 = v;
		break;
	case 5:
		sf->x5 = v;
		break;
	default:
		break;
	}
}

static inline void syscall_frame_arm_restart(arch_syscall_frame_t *sf)
{
	(void)sf;
}
