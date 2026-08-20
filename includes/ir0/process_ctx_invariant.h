/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_ctx_invariant.h
 * Description: Class B invariant — KERNEL CS + userspace RIP (pure, host-safe)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Canonical user VA range used by desk/exec ELF loads (x86-64). */
#define IR0_USER_RIP_LO 0x00400000ULL
#define IR0_USER_RIP_HI 0x00007FFFFFFFFFFFULL

/* Match includes/ir0/abi/mmap_contract.h — duplicated for host-safe headers. */
#ifndef IR0_USER_STACK_TOP
#define IR0_USER_STACK_TOP  0x7FFFF000ULL
#define IR0_USER_STACK_SIZE 0x80000ULL
#define IR0_USER_STACK_BASE (IR0_USER_STACK_TOP - IR0_USER_STACK_SIZE)
#endif

/*
 * process_cs_rip_kernel_ret_bad - True when kernel_ret would jmp to user VA.
 *
 * Desk Class B: task.arch.cs ring-0 paired with task.arch.rip in userspace (sync wrote
 * user IP, then process_arm_kernel_syscall_sleep flipped CS without a save).
 */
static inline int process_cs_rip_kernel_ret_bad(uint64_t cs, uint64_t rip)
{
	if ((cs & 3u) != 0u)
		return 0;
	if (rip < IR0_USER_RIP_LO || rip > IR0_USER_RIP_HI)
		return 0;
	return 1;
}

static inline int process_rip_in_user_range(uint64_t rip)
{
	return (rip >= IR0_USER_RIP_LO && rip <= IR0_USER_RIP_HI) ? 1 : 0;
}

/* Reject stack VAs as executable RIP (Class B repair must not jmp to argc slot). */
static inline int process_rip_in_user_stack(uint64_t rip)
{
	return (rip >= IR0_USER_STACK_BASE && rip < IR0_USER_STACK_TOP) ? 1 : 0;
}
