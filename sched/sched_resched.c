/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_resched.c
 * Description: Reschedule helpers shared by console wake and idle poll.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sched.h>
#include <ir0/console.h>
#include <ir0/process.h>
#include <ir0/arch_port.h>
#include <ir0/clock.h>
#include <ktm.h>

void sched_try_preempt_blocked(void)
{
	if (!current_process || current_process->state != PROCESS_BLOCKED)
		return;
	if (sched_count_runnable() == 0)
		return;
	sched_schedule_next();
}

int sched_user_return_take_switch(void)
{
	if (!current_process || current_process->state == PROCESS_BLOCKED)
		return 0;
	if (sched_count_runnable() <= 1)
		return 0;
	if (!clock_take_sched_resched_pending())
		return 0;
	return 1;
}

void sched_need_resched_user_return(void)
{
	if (sched_user_return_take_switch())
		sched_schedule_next();
}
