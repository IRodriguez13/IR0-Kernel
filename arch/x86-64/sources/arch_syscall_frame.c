/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_syscall_frame.c
 * Description: x86-64 syscall/IRQ frame capture and kernel task stack setup.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_syscall_frame.h>
#include <ir0/arch_task.h>
#include <ir0/arch_task_ops.h>
#include <ir0/errno.h>
#include <kernel/process.h>
#include <config.h>

extern uint64_t fase29_entry_rip;

void syscall_capture_frame_at_entry(struct process *p,
						 uint64_t *frame_base,
						 uint64_t rip_hw)
{
	syscall_user_frame_t *sf;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	/*
	 * frame_base[7] is the user RIP (rcx) at syscall entry
	 * (syscall_insn_entry_64.asm). fase29_entry_rip is from a prior sysret.
	 */
	(void)fase29_entry_rip;
	sf->rip = frame_base[7];
	if (!sf->rip && rip_hw)
		sf->rip = rip_hw;
	sf->rflags = frame_base[6];
	sf->rsp = frame_base[8];
	sf->rbx = frame_base[0];
	sf->rbp = frame_base[1];
	sf->r12 = frame_base[2];
	sf->r13 = frame_base[3];
	sf->r14 = frame_base[4];
	sf->r15 = frame_base[5];
	sf->rdi = frame_base[-1];
	sf->rsi = frame_base[-2];
	sf->rdx = frame_base[-3];
	sf->r10 = frame_base[-4];
	sf->r8 = frame_base[-5];
	sf->r9 = frame_base[-6];
	p->syscall_frame_fresh = 1;
	process_sync_task_user_ip_from_syscall_frame(p);
}

void syscall_restore_exit_regs(struct process *p,
					    uint64_t *stack_r9_slot)
{
	const syscall_user_frame_t *sf;

	if (!stack_r9_slot || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	stack_r9_slot[0] = sf->r9;
	stack_r9_slot[1] = sf->r8;
	stack_r9_slot[2] = sf->r10;
	stack_r9_slot[3] = sf->rdx;
	stack_r9_slot[4] = sf->rsi;
	stack_r9_slot[5] = sf->rdi;
	stack_r9_slot[6] = sf->rbx;
	stack_r9_slot[7] = sf->rbp;
	stack_r9_slot[8] = sf->r12;
	stack_r9_slot[9] = sf->r13;
	stack_r9_slot[10] = sf->r14;
	stack_r9_slot[11] = sf->r15;
	stack_r9_slot[12] = sf->rflags;
	stack_r9_slot[13] = sf->rip;
	p->fork_resync_syscall_stack = 0;
}

int irq_frame_is_user(const uint64_t *iretq_frame)
{
	if (!iretq_frame)
		return 0;
	return ((iretq_frame[3] & 3U) == 3U) ? 1 : 0;
}

void syscall_save_user_context_from_irq(uint64_t *gpr_stack)
{
	/*
	 * gpr_stack = saved-RAX; iretq frame begins 15 qwords above
	 * (isr_common_stub_64 / sched_resched.c).
	 */
	if (!gpr_stack)
		return;
	irq_save_user_frame(gpr_stack + 15);
}
