/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_signal.c
 * Description: x86-64 IRQ-frame ↔ sigcontext and handler redirect.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_signal.h>
#include <ir0/arch_task.h>
#include <ir0/arch_syscall_frame.h>
#include <ir0/signals.h>
#include <config.h>
#include <string.h>

uint64_t sigcontext_ip(const struct sigcontext *ctx)
{
	return ctx ? ctx->rip : 0;
}

uint64_t sigcontext_sp(const struct sigcontext *ctx)
{
	return ctx ? ctx->rsp : 0;
}

void signal_fill_sigcontext_from_syscall_frame(struct sigcontext *ctx,
						    const struct arch_syscall_frame *sf,
						    uint64_t retval)
{
	if (!ctx || !sf)
		return;

	memset(ctx, 0, sizeof(*ctx));
	ctx->rip = sf->rip;
	ctx->rsp = sf->rsp;
	ctx->rflags = sf->rflags ? sf->rflags : (uint64_t)RFLAGS_IF;
	ctx->rax = retval;
	ctx->rdi = sf->rdi;
	ctx->rsi = sf->rsi;
	ctx->rdx = sf->rdx;
	ctx->r10 = sf->r10;
	ctx->r8 = sf->r8;
	ctx->r9 = sf->r9;
	ctx->rbx = sf->rbx;
	ctx->rbp = sf->rbp;
	ctx->r12 = sf->r12;
	ctx->r13 = sf->r13;
	ctx->r14 = sf->r14;
	ctx->r15 = sf->r15;
#if defined(USER_CODE_SEL) && defined(USER_DATA_SEL)
	ctx->cs = (uint64_t)USER_CODE_SEL;
	ctx->ss = (uint64_t)USER_DATA_SEL;
#endif
}

void signal_fill_syscall_frame_from_sigcontext(struct arch_syscall_frame *sf,
						    const struct sigcontext *ctx)
{
	if (!sf || !ctx)
		return;

	sf->rip = ctx->rip;
	sf->rsp = ctx->rsp;
	sf->rflags = ctx->rflags ? ctx->rflags : (uint64_t)RFLAGS_IF;
	sf->rdi = ctx->rdi;
	sf->rsi = ctx->rsi;
	sf->rdx = ctx->rdx;
	sf->r10 = ctx->r10;
	sf->r8 = ctx->r8;
	sf->r9 = ctx->r9;
	sf->rbx = ctx->rbx;
	sf->rbp = ctx->rbp;
	sf->r12 = ctx->r12;
	sf->r13 = ctx->r13;
	sf->r14 = ctx->r14;
	sf->r15 = ctx->r15;
}

void signal_fill_sigcontext_from_irq_frame(struct sigcontext *ctx,
						const uint64_t *frame)
{
	if (!ctx || !frame)
		return;

	ctx->r15 = frame[-15];
	ctx->r14 = frame[-14];
	ctx->r13 = frame[-13];
	ctx->r12 = frame[-12];
	ctx->r11 = frame[-11];
	ctx->r10 = frame[-10];
	ctx->r9 = frame[-9];
	ctx->r8 = frame[-8];
	ctx->rdi = frame[-7];
	ctx->rsi = frame[-6];
	ctx->rbp = frame[-5];
	ctx->rbx = frame[-4];
	ctx->rdx = frame[-3];
	ctx->rcx = frame[-2];
	ctx->rax = frame[-1];
	ctx->orig_rax = 0;
	ctx->rip = frame[2];
	ctx->cs = frame[3];
	ctx->rflags = frame[4];
	ctx->rsp = frame[5];
	ctx->ss = frame[6];
}

uint64_t irq_frame_sp(const uint64_t *frame)
{
	return frame ? frame[5] : 0;
}

void signal_redirect_irq_frame(uint64_t *frame, void *handler, int sig,
				    uint64_t new_rsp, uint64_t info_addr,
				    uint64_t uctx_addr, int sa_siginfo)
{
	if (!frame || !handler)
		return;

	frame[2] = (uint64_t)(uintptr_t)handler;
	frame[5] = new_rsp;
	frame[-7] = (uint64_t)(uint32_t)sig;
	if (sa_siginfo)
	{
		frame[-6] = info_addr;
		frame[-3] = uctx_addr;
	}
	else
	{
		frame[-6] = 0;
		frame[-3] = 0;
	}
}

void signal_prepare_task_handler(task_t *t, void *handler, int sig,
				      uint64_t frame_sp)
{
	if (!t || !handler)
		return;

	task_set_sp(t, frame_sp);
	task_set_ip(t, (uint64_t)(uintptr_t)handler);
	task_set_arg0(t, (uint64_t)(uint32_t)sig);
}
