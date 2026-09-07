/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched.h
 * Description: Portable scheduler facade — no <sched/...> includes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef struct process process_t;

void sched_add_process(process_t *proc);
void sched_remove_process(process_t *proc);
void sched_schedule_next(void);
const char *sched_active_policy_name(void);
int sched_count_runnable(void);
void sched_promote_process(process_t *proc);

void sched_try_preempt_blocked(void);
int sched_user_return_take_switch(void);
void sched_need_resched_user_return(void);
