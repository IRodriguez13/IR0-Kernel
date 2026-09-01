/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_signal.c
 * Description: ARM64 exception-frame ↔ Linux aarch64 sigcontext delivery.
 *
 * Frame layout matches vectors.S exc_entry_frame (x0@0 … x30@240).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_signal.h>
#include <ir0/arch_task.h>
#include <ir0/arch_syscall_frame.h>
#include <ir0/signals.h>
#include <stdint.h>
#include <string.h>

uint64_t sigcontext_ip(const struct sigcontext *ctx)
{
	return ctx ? ctx->pc : 0;
}

uint64_t sigcontext_sp(const struct sigcontext *ctx)
{
	return ctx ? ctx->sp : 0;
}

void signal_fill_sigcontext_from_syscall_frame(struct sigcontext *ctx,
						    const struct arch_syscall_frame *sf,
						    uint64_t retval)
{
	if (!ctx || !sf)
		return;

	memset(ctx, 0, sizeof(*ctx));
	ctx->regs[0] = retval;
	ctx->regs[1] = sf->x1;
	ctx->regs[2] = sf->x2;
	ctx->regs[3] = sf->x3;
	ctx->regs[4] = sf->x4;
	ctx->regs[5] = sf->x5;
	ctx->regs[6] = sf->x6;
	ctx->regs[7] = sf->x7;
	ctx->regs[8] = sf->x8;
	ctx->regs[9] = sf->x9;
	ctx->regs[10] = sf->x10;
	ctx->regs[11] = sf->x11;
	ctx->regs[12] = sf->x12;
	ctx->regs[13] = sf->x13;
	ctx->regs[14] = sf->x14;
	ctx->regs[15] = sf->x15;
	ctx->regs[16] = sf->x16;
	ctx->regs[17] = sf->x17;
	ctx->regs[18] = sf->x18;
	ctx->regs[19] = sf->x19;
	ctx->regs[20] = sf->x20;
	ctx->regs[21] = sf->x21;
	ctx->regs[22] = sf->x22;
	ctx->regs[23] = sf->x23;
	ctx->regs[24] = sf->x24;
	ctx->regs[25] = sf->x25;
	ctx->regs[26] = sf->x26;
	ctx->regs[27] = sf->x27;
	ctx->regs[28] = sf->x28;
	ctx->regs[29] = sf->x29;
	ctx->regs[30] = sf->x30;
	ctx->sp = sf->sp;
	ctx->pc = sf->elr;
	ctx->pstate = sf->spsr;
}

void signal_fill_sigcontext_from_irq_frame(struct sigcontext *ctx,
						const uint64_t *frame)
{
	uint64_t far;
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;
	unsigned i;

	if (!ctx || !frame)
		return;

	memset(ctx, 0, sizeof(*ctx));
	for (i = 0; i < 31; i++)
		ctx->regs[i] = frame[i];

	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	ctx->fault_address = far;
	ctx->pc = elr;
	ctx->pstate = spsr;
	ctx->sp = sp_el0;
}

uint64_t irq_frame_sp(const uint64_t *frame)
{
	uint64_t sp_el0;

	(void)frame;
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	return sp_el0;
}

void signal_redirect_irq_frame(uint64_t *frame, void *handler, int sig,
				    uint64_t new_rsp, uint64_t info_addr,
				    uint64_t uctx_addr, int sa_siginfo)
{
	uint64_t elr = (uint64_t)(uintptr_t)handler;

	if (!frame || !handler)
		return;

	/* AAPCS64: x0=signum, x1=siginfo*, x2=ucontext* for SA_SIGINFO. */
	frame[0] = (uint64_t)(uint32_t)sig;
	if (sa_siginfo)
	{
		frame[1] = info_addr;
		frame[2] = uctx_addr;
	}
	else
	{
		frame[1] = 0;
		frame[2] = 0;
	}

	__asm__ volatile("msr elr_el1, %0" :: "r"(elr) : "memory");
	__asm__ volatile("msr sp_el0, %0" :: "r"(new_rsp) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void signal_prepare_task_handler(task_t *t, void *handler, int sig,
				      uint64_t frame_sp)
{
	if (!t || !handler)
		return;

	task_set_sp(t, frame_sp);
	task_set_ip(t, (uint64_t)(uintptr_t)handler);
	task_set_retval(t, (uint64_t)(uint32_t)sig);
	t->arch.x1 = 0;
	t->arch.x2 = 0;
}
