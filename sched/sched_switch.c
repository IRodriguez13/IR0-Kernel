/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_switch.c
 * Description: Shared context-switch path for scheduler backends.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "sched_switch.h"

#include <ir0/arch_port.h>
#include <ir0/context.h>
#include <ir0/switch.h>
#include <ir0/oops.h>
#include <ir0/signals.h>
#include <ir0/sched.h>
#include <stdint.h>

static inline uint64_t sched_switch_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void sched_switch_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

void sched_context_switch_to(process_t *next)
{
	static int first = 1;
	process_t *prev;
	uint64_t irq_flags;
	int should_handle_signals = 0;

	if (!next)
		return;

	irq_flags = sched_switch_irq_save();
	prev = current_process;

	if (!first && prev == next)
	{
		sched_switch_irq_restore(irq_flags);
		return;
	}

	if (prev && prev->state == PROCESS_RUNNING)
	{
		should_handle_signals = 1;
		process_set_sched_state(prev, PROCESS_READY);
	}

	if (should_handle_signals)
		handle_signals();

	process_set_sched_state(next, PROCESS_RUNNING);
	current_process = next;

	if (signals_should_handle_on_run(next))
		handle_signals();

	if (first)
	{
		first = 0;
		set_current_kernel_stack(next);
		first_switch_to(next);
		panic("Returned from first context switch");
	}

	if (prev && next)
	{
		/*
		 * IRQ user preempt already filled prev->task from the iretq frame
		 * (sched_irq_preempt_from_frame). Do not let switch_context_x64
		 * overwrite that with kernel CS + [rsp] (mid-ISR), or a later
		 * process_arm_kernel_syscall_sleep leaves KERNEL_CS + user RIP.
		 */
		if (sched_context_switch_take_skip_prev_save())
		{
			switch_save_user_rsp(prev);
			switch_to(NULL, &next->task);
		}
		else
			switch_to(&prev->task, &next->task);
	}

	sched_switch_irq_restore(irq_flags);
}
