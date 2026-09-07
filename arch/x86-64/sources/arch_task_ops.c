/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_task_ops.c
 * Description: x86-64 task context ↔ sigcontext / syscall / IRQ frame transfer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_task_ops.h>
#include <ir0/debug_trap.h>
#include <config.h>

uint64_t task_initial_user_status(void)
{
	return ir0_rflags_sanitize_user(RFLAGS_IF);
}

uint64_t task_initial_kernel_status(void)
{
	return RFLAGS_IF;
}

void task_store_sigcontext(struct sigcontext *ctx, const task_t *t)
{
	if (!ctx || !t)
		return;

	ctx->r15 = t->arch.r15;
	ctx->r14 = t->arch.r14;
	ctx->r13 = t->arch.r13;
	ctx->r12 = t->arch.r12;
	ctx->rbp = t->arch.rbp;
	ctx->rbx = t->arch.rbx;
	ctx->r11 = t->arch.r11;
	ctx->r10 = t->arch.r10;
	ctx->r9 = t->arch.r9;
	ctx->r8 = t->arch.r8;
	ctx->rax = t->arch.rax;
	ctx->rcx = t->arch.rcx;
	ctx->rdx = t->arch.rdx;
	ctx->rsi = t->arch.rsi;
	ctx->rdi = t->arch.rdi;
	ctx->orig_rax = 0;
	ctx->rip = t->arch.rip;
	ctx->cs = t->arch.cs;
	ctx->rflags = t->arch.rflags;
	ctx->rsp = t->arch.rsp;
	ctx->ss = t->arch.ss;
}

void task_load_sigcontext(task_t *t, const struct sigcontext *ctx)
{
	if (!t || !ctx)
		return;

	t->arch.r15 = ctx->r15;
	t->arch.r14 = ctx->r14;
	t->arch.r13 = ctx->r13;
	t->arch.r12 = ctx->r12;
	t->arch.rbp = ctx->rbp;
	t->arch.rbx = ctx->rbx;
	t->arch.r11 = ctx->r11;
	t->arch.r10 = ctx->r10;
	t->arch.r9 = ctx->r9;
	t->arch.r8 = ctx->r8;
	t->arch.rax = ctx->rax;
	t->arch.rcx = ctx->rcx;
	t->arch.rdx = ctx->rdx;
	t->arch.rsi = ctx->rsi;
	t->arch.rdi = ctx->rdi;
	t->arch.rip = ctx->rip;
	t->arch.cs = (uint16_t)ctx->cs;
	t->arch.rflags = ctx->rflags;
	t->arch.rsp = ctx->rsp;
	t->arch.ss = (uint16_t)ctx->ss;
}

void task_save_user_exception_frame(task_t *t, const void *opaque_frame)
{
	const uint64_t *frame = opaque_frame;

	if (!t || !frame)
		return;

	task_set_ip(t, frame[2]);
	task_set_flags(t, ir0_rflags_sanitize_user((frame[4] | 2ULL) | RFLAGS_IF));
	task_set_sp(t, frame[5]);

	t->arch.rax = frame[-1];
	t->arch.rcx = frame[-2];
	t->arch.rdx = frame[-3];
	t->arch.rbx = frame[-4];
	t->arch.rbp = frame[-5];
	t->arch.rsi = frame[-6];
	t->arch.rdi = frame[-7];
	t->arch.r8 = frame[-8];
	t->arch.r9 = frame[-9];
	t->arch.r10 = frame[-10];
	t->arch.r11 = frame[-11];
	t->arch.r12 = frame[-12];
	t->arch.r13 = frame[-13];
	t->arch.r14 = frame[-14];
	t->arch.r15 = frame[-15];

	if ((frame[3] & 3U) == 3U)
	{
#if defined(USER_CODE_SEL) && defined(USER_DATA_SEL)
		task_set_cs(t, (uint16_t)USER_CODE_SEL);
		task_set_ss(t, (uint16_t)USER_DATA_SEL);
#else
		task_set_cs(t, 0x1B);
		task_set_ss(t, 0x23);
#endif
	}
	else
	{
		task_set_cs(t, (uint16_t)frame[3]);
		task_set_ss(t, (uint16_t)frame[6]);
	}
}

void task_sync_syscall_soft_mirror(task_t *t,
					const arch_task_syscall_frame_t *sf)
{
	uint64_t rflags;

	if (!t || !sf)
		return;

	task_set_ip(t, sf->rip);
	task_set_sp(t, sf->rsp);
	rflags = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	task_set_flags(t, rflags);
	t->arch.rcx = sf->rip;
	t->arch.r11 = rflags;

	/*
	 * Mirror the whole user GPR set, not just RIP/RSP/RFLAGS. A previous
	 * in-kernel context save leaves callee-saved registers holding kernel
	 * values (RBP points into the kstack); resuming through
	 * .user_iretq_resume would hand those to ring 3. RAX is excluded — it
	 * carries the syscall return value.
	 */
	t->arch.rbx = sf->rbx;
	t->arch.rbp = sf->rbp;
	t->arch.r12 = sf->r12;
	t->arch.r13 = sf->r13;
	t->arch.r14 = sf->r14;
	t->arch.r15 = sf->r15;
	t->arch.rdi = sf->rdi;
	t->arch.rsi = sf->rsi;
	t->arch.rdx = sf->rdx;
	t->arch.r10 = sf->r10;
	t->arch.r8 = sf->r8;
	t->arch.r9 = sf->r9;
}

void task_apply_syscall_frame(task_t *t,
				   const arch_task_syscall_frame_t *sf,
				   uint64_t rax)
{
	uint64_t rflags;

	if (!t || !sf)
		return;

	rflags = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	task_set_ip(t, sf->rip);
	task_set_sp(t, sf->rsp);
	task_set_flags(t, rflags);
	task_set_retval(t, rax);
	t->arch.rbx = sf->rbx;
	t->arch.rbp = sf->rbp;
	t->arch.r12 = sf->r12;
	t->arch.r13 = sf->r13;
	t->arch.r14 = sf->r14;
	t->arch.r15 = sf->r15;
	t->arch.rdi = sf->rdi;
	t->arch.rsi = sf->rsi;
	t->arch.rdx = sf->rdx;
	t->arch.r10 = sf->r10;
	t->arch.r8 = sf->r8;
	t->arch.r9 = sf->r9;
	t->arch.rcx = sf->rip;
	t->arch.r11 = rflags;
	task_apply_user_segments(t);
}

void task_apply_user_segments(task_t *t)
{
	task_set_user_segments(t);
}

void task_apply_kernel_segments(task_t *t)
{
	task_set_kernel_segments(t);
}

uint64_t *task_retval_slot_addr(task_t *t)
{
	return t ? &t->arch.rax : NULL;
}
