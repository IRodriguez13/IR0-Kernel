/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_task_ops.c
 * Description: ARM64 task context ↔ Linux aarch64 sigcontext / frame transfer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_task_ops.h>
#include <ir0/arch_task.h>
#include <ir0/signals.h>
#include <string.h>

void arch_task_store_sigcontext(struct sigcontext *ctx, const task_t *t)
{
	if (!ctx || !t)
		return;

	memset(ctx, 0, sizeof(*ctx));
	ctx->fault_address = 0;
	ctx->regs[0] = t->arch.x0;
	ctx->regs[1] = t->arch.x1;
	ctx->regs[2] = t->arch.x2;
	ctx->regs[3] = t->arch.x3;
	ctx->regs[4] = t->arch.x4;
	ctx->regs[5] = t->arch.x5;
	ctx->regs[6] = t->arch.x6;
	ctx->regs[7] = t->arch.x7;
	ctx->regs[8] = t->arch.x8;
	ctx->regs[9] = t->arch.x9;
	ctx->regs[10] = t->arch.x10;
	ctx->regs[11] = t->arch.x11;
	ctx->regs[12] = t->arch.x12;
	ctx->regs[13] = t->arch.x13;
	ctx->regs[14] = t->arch.x14;
	ctx->regs[15] = t->arch.x15;
	ctx->regs[16] = t->arch.x16;
	ctx->regs[17] = t->arch.x17;
	ctx->regs[18] = t->arch.x18;
	ctx->regs[19] = t->arch.x19;
	ctx->regs[20] = t->arch.x20;
	ctx->regs[21] = t->arch.x21;
	ctx->regs[22] = t->arch.x22;
	ctx->regs[23] = t->arch.x23;
	ctx->regs[24] = t->arch.x24;
	ctx->regs[25] = t->arch.x25;
	ctx->regs[26] = t->arch.x26;
	ctx->regs[27] = t->arch.x27;
	ctx->regs[28] = t->arch.x28;
	ctx->regs[29] = t->arch.x29;
	ctx->regs[30] = t->arch.x30;
	ctx->sp = t->arch.sp_el0;
	ctx->pc = t->arch.elr_el1;
	ctx->pstate = t->arch.spsr_el1;
	/* __reserved zeroed by memset — null _aarch64_ctx terminator. */
}

void arch_task_load_sigcontext(task_t *t, const struct sigcontext *ctx)
{
	if (!t || !ctx)
		return;

	t->arch.x0 = ctx->regs[0];
	t->arch.x1 = ctx->regs[1];
	t->arch.x2 = ctx->regs[2];
	t->arch.x3 = ctx->regs[3];
	t->arch.x4 = ctx->regs[4];
	t->arch.x5 = ctx->regs[5];
	t->arch.x6 = ctx->regs[6];
	t->arch.x7 = ctx->regs[7];
	t->arch.x8 = ctx->regs[8];
	t->arch.x9 = ctx->regs[9];
	t->arch.x10 = ctx->regs[10];
	t->arch.x11 = ctx->regs[11];
	t->arch.x12 = ctx->regs[12];
	t->arch.x13 = ctx->regs[13];
	t->arch.x14 = ctx->regs[14];
	t->arch.x15 = ctx->regs[15];
	t->arch.x16 = ctx->regs[16];
	t->arch.x17 = ctx->regs[17];
	t->arch.x18 = ctx->regs[18];
	t->arch.x19 = ctx->regs[19];
	t->arch.x20 = ctx->regs[20];
	t->arch.x21 = ctx->regs[21];
	t->arch.x22 = ctx->regs[22];
	t->arch.x23 = ctx->regs[23];
	t->arch.x24 = ctx->regs[24];
	t->arch.x25 = ctx->regs[25];
	t->arch.x26 = ctx->regs[26];
	t->arch.x27 = ctx->regs[27];
	t->arch.x28 = ctx->regs[28];
	t->arch.x29 = ctx->regs[29];
	t->arch.x30 = ctx->regs[30];
	t->arch.sp_el0 = ctx->sp;
	t->arch.elr_el1 = ctx->pc;
	t->arch.spsr_el1 = ctx->pstate;
}

void arch_task_save_irq_user_frame(task_t *t, const uint64_t *frame)
{
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;

	if (!t || !frame)
		return;

	/*
	 * vectors.S exc_entry_frame: x0@0 … x30@240 (31 GPRs).
	 * ELR / SPSR / SP_EL0 live in system registers at exception entry.
	 */
	t->arch.x0 = frame[0];
	t->arch.x1 = frame[1];
	t->arch.x2 = frame[2];
	t->arch.x3 = frame[3];
	t->arch.x4 = frame[4];
	t->arch.x5 = frame[5];
	t->arch.x6 = frame[6];
	t->arch.x7 = frame[7];
	t->arch.x8 = frame[8];
	t->arch.x9 = frame[9];
	t->arch.x10 = frame[10];
	t->arch.x11 = frame[11];
	t->arch.x12 = frame[12];
	t->arch.x13 = frame[13];
	t->arch.x14 = frame[14];
	t->arch.x15 = frame[15];
	t->arch.x16 = frame[16];
	t->arch.x17 = frame[17];
	t->arch.x18 = frame[18];
	t->arch.x19 = frame[19];
	t->arch.x20 = frame[20];
	t->arch.x21 = frame[21];
	t->arch.x22 = frame[22];
	t->arch.x23 = frame[23];
	t->arch.x24 = frame[24];
	t->arch.x25 = frame[25];
	t->arch.x26 = frame[26];
	t->arch.x27 = frame[27];
	t->arch.x28 = frame[28];
	t->arch.x29 = frame[29];
	t->arch.x30 = frame[30];

	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	t->arch.elr_el1 = elr;
	t->arch.spsr_el1 = spsr;
	t->arch.sp_el0 = sp_el0;
}

void arch_task_sync_syscall_soft_mirror(task_t *t,
					const arch_task_syscall_frame_t *sf)
{
	if (!t || !sf)
		return;

	task_set_ip(t, sf->elr);
	task_set_sp(t, sf->sp);
	task_set_flags(t, sf->spsr);
}

void arch_task_apply_syscall_frame(task_t *t,
				   const arch_task_syscall_frame_t *sf,
				   uint64_t x0)
{
	if (!t || !sf)
		return;

	t->arch.x0 = x0;
	t->arch.x1 = sf->x1;
	t->arch.x2 = sf->x2;
	t->arch.x3 = sf->x3;
	t->arch.x4 = sf->x4;
	t->arch.x5 = sf->x5;
	t->arch.x6 = sf->x6;
	t->arch.x7 = sf->x7;
	t->arch.x8 = sf->x8;
	t->arch.x9 = sf->x9;
	t->arch.x10 = sf->x10;
	t->arch.x11 = sf->x11;
	t->arch.x12 = sf->x12;
	t->arch.x13 = sf->x13;
	t->arch.x14 = sf->x14;
	t->arch.x15 = sf->x15;
	t->arch.x16 = sf->x16;
	t->arch.x17 = sf->x17;
	t->arch.x18 = sf->x18;
	t->arch.x19 = sf->x19;
	t->arch.x20 = sf->x20;
	t->arch.x21 = sf->x21;
	t->arch.x22 = sf->x22;
	t->arch.x23 = sf->x23;
	t->arch.x24 = sf->x24;
	t->arch.x25 = sf->x25;
	t->arch.x26 = sf->x26;
	t->arch.x27 = sf->x27;
	t->arch.x28 = sf->x28;
	t->arch.x29 = sf->x29;
	t->arch.x30 = sf->x30;
	task_set_ip(t, sf->elr);
	task_set_sp(t, sf->sp);
	task_set_flags(t, sf->spsr);
	task_set_retval(t, x0);
}

void arch_task_apply_user_segments(task_t *t)
{
	(void)t;
}

void arch_task_apply_kernel_segments(task_t *t)
{
	(void)t;
}

uint64_t *arch_task_retval_slot_addr(task_t *t)
{
	return t ? &t->arch.x0 : NULL;
}
