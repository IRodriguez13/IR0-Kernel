/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: time_syscalls.h
 * Description: IR0 kernel header — time syscalls
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <ir0/time.h>

int64_t sys_gettimeofday(struct timeval *tv, void *tz);
int64_t sys_clock_gettime(int clock_id, struct timespec *tp);
int64_t sys_getitimer(int which, struct itimerval *curr_value);
int64_t sys_setitimer(int which, const struct itimerval *new_value,
		      struct itimerval *old_value);
int64_t sys_alarm(unsigned int seconds);

/* Called from clock_tick — fire ITIMER_REAL → SIGALRM. */
void process_itimer_tick(uint64_t now_ms);
