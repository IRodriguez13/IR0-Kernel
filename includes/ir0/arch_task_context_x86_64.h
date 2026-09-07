/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_task_context_x86_64.h
 * Description: x86-64 CPU register snapshot for per-task context switching.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct arch_task_context
{
	uint64_t rax;      /* +0x00 */
	uint64_t rbx;      /* +0x08 */
	uint64_t rcx;      /* +0x10 */
	uint64_t rdx;      /* +0x18 */
	uint64_t rsi;      /* +0x20 */
	uint64_t rdi;      /* +0x28 */
	uint64_t r8;       /* +0x30 */
	uint64_t r9;       /* +0x38 */
	uint64_t r10;      /* +0x40 */
	uint64_t r11;      /* +0x48 */
	uint64_t r12;      /* +0x50 */
	uint64_t r13;      /* +0x58 */
	uint64_t r14;      /* +0x60 */
	uint64_t r15;      /* +0x68 */
	uint64_t rsp;      /* +0x70 */
	uint64_t rbp;      /* +0x78 */
	uint64_t rip;      /* +0x80 */
	uint64_t rflags;   /* +0x88 */
	uint16_t cs;       /* +0x90 */
	uint16_t ds;       /* +0x92 */
	uint16_t es;       /* +0x94 */
	uint16_t fs;       /* +0x96 */
	uint16_t gs;       /* +0x98 */
	uint16_t ss;       /* +0x9A */
	uint16_t padding1; /* +0x9C */
	uint16_t padding2; /* +0x9E — must stay 16-bit for switch_x64.asm offsets */
	uint64_t cr0;      /* +0xA0 */
	uint64_t cr2;      /* +0xA8 */
	uint64_t cr3;      /* +0xB0 */
	uint64_t cr4;      /* +0xB8 */
	uint64_t dr0;      /* +0xC0 */
	uint64_t dr1;      /* +0xC8 */
	uint64_t dr2;      /* +0xD0 */
	uint64_t dr3;      /* +0xD8 */
	uint64_t dr6;      /* +0xE0 */
	uint64_t dr7;      /* +0xE8 */
} arch_task_context_t;

_Static_assert(offsetof(arch_task_context_t, rip) == 0x80,
	       "switch_x64.asm RIP offset");
_Static_assert(offsetof(arch_task_context_t, cr3) == 0xB0,
	       "switch_x64.asm CR3 offset");
_Static_assert(offsetof(arch_task_context_t, ss) == 0x9A,
	       "switch_x64.asm SS offset");

#include <ir0/asm_offsets.h>

#define TASK_CONTEXT_ASSERT_LAYOUT(task_type) \
	_Static_assert(offsetof(task_type, arch.cr3) == IR0_TASK_ARCH_CR3_OFFSET, \
		       "x86 task CR3 offset out of sync"); \
	_Static_assert(offsetof(task_type, arch.rip) == IR0_TASK_ARCH_RIP_OFFSET, \
		       "x86 task RIP offset out of sync"); \
	_Static_assert(offsetof(task_type, arch.ss) == IR0_TASK_ARCH_SS_OFFSET, \
		       "x86 task SS offset out of sync")

#define PROCESS_CONTEXT_ASSERT_LAYOUT(process_type) \
	_Static_assert(offsetof(process_type, task) == 0, \
		       "process task must remain first"); \
	_Static_assert(offsetof(process_type, fs_base) == IR0_PROC_FS_BASE_OFFSET, \
		       "x86 process FS base offset out of sync")
