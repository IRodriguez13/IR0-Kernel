/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: time_syscalls.c
 * Description: time syscalls (gettimeofday, clock_gettime, setitimer)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "time_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/syscalls_kernel.h>
#include <ir0/process.h>
#include <ir0/errno.h>
#include <ir0/clock.h>
#include <ir0/copy_user.h>
#include <ir0/validation.h>
#include <ir0/signals.h>
#include <ir0/time.h>
#include <string.h>

extern process_t *process_list;

static uint64_t timeval_to_ms(const struct timeval *tv)
{
	uint64_t ms;

	if (!tv || (tv->tv_sec == 0 && tv->tv_usec == 0))
		return 0;
	if (tv->tv_sec < 0 || tv->tv_usec < 0)
		return 0;
	ms = (uint64_t)tv->tv_sec * 1000UL;
	ms += (uint64_t)tv->tv_usec / 1000UL;
	if (ms == 0 && (tv->tv_sec > 0 || tv->tv_usec > 0))
		ms = 1; /* sub-ms → one tick */
	return ms;
}

static void ms_to_timeval(uint64_t ms, struct timeval *tv)
{
	if (!tv)
		return;
	tv->tv_sec = (time_t)(ms / 1000UL);
	tv->tv_usec = (suseconds_t)((ms % 1000UL) * 1000UL);
}

static void itimer_fill_old(process_t *p, uint64_t now, struct itimerval *old)
{
	uint64_t rem = 0;

	memset(old, 0, sizeof(*old));
	if (p->it_real_expire_ms != 0)
	{
		if (p->it_real_expire_ms > now)
			rem = p->it_real_expire_ms - now;
		else
			rem = 1;
	}
	ms_to_timeval(rem, &old->it_value);
	ms_to_timeval(p->it_real_interval_ms, &old->it_interval);
}

/*
 * process_itimer_tick - Fire ITIMER_REAL expiries from the clock tick.
 * Called from clock_tick(); may wake blocked tasks via send_signal().
 */
void process_itimer_tick(uint64_t now_ms)
{
	process_t *p;

	for (p = process_list; p; p = p->next)
	{
		if (p->it_real_expire_ms == 0)
			continue;
		if (now_ms < p->it_real_expire_ms)
			continue;

		if (p->it_real_interval_ms != 0)
		{
			/* Catch up like timerfd: advance by whole intervals. */
			do
			{
				p->it_real_expire_ms += p->it_real_interval_ms;
			} while (p->it_real_expire_ms <= now_ms &&
				 p->it_real_interval_ms != 0);
		}
		else
			p->it_real_expire_ms = 0;

		(void)send_signal((int)p->task.pid, SIGALRM);
	}
}

int64_t sys_gettimeofday(struct timeval *tv, void *tz)
{
	uint64_t uptime_ms;
	time_t sec;
	suseconds_t usec;

	(void)tz;

	if (!current_process || !tv)
		return -EFAULT;
	if (validate_userspace_buffer(tv, sizeof(struct timeval)) != 0)
		return -EFAULT;

	uptime_ms = clock_get_uptime_milliseconds();
	if (clock_realtime_available())
	{
		sec = clock_get_current_time();
		/* current_time advances on second boundaries; residual from uptime. */
		usec = (suseconds_t)((uptime_ms % 1000) * 1000);
	}
	else
	{
		/* Honest fallback: no wall clock — boot-relative (same as MONOTONIC). */
		sec = (time_t)(uptime_ms / 1000);
		usec = (suseconds_t)((uptime_ms % 1000) * 1000);
	}
	tv->tv_sec = sec;
	tv->tv_usec = usec;
	return 0;
}

int64_t sys_clock_gettime(int clock_id, struct timespec *tp)
{
	uint64_t uptime_ms;

	if (!current_process || !tp)
		return -EFAULT;

	if (validate_userspace_buffer(tp, sizeof(struct timespec)) != 0)
		return -EFAULT;

	uptime_ms = clock_get_uptime_milliseconds();

	/*
	 * CLOCK_REALTIME (0): RTC_UTC + monotonic elapsed when available.
	 * CLOCK_MONOTONIC (1), MONOTONIC_RAW (4), BOOTTIME (7): uptime since boot.
	 */
	if (clock_id == 0)
	{
		if (clock_realtime_available())
		{
			tp->tv_sec = clock_get_current_time();
			tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
			return 0;
		}
		/* No RTC: still return boot-relative so date(1) is not ENOSYS. */
		tp->tv_sec = (time_t)(uptime_ms / 1000);
		tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
		return 0;
	}

	if (clock_id != 1 && clock_id != 4 && clock_id != 7)
		return -EINVAL;

	tp->tv_sec = (time_t)(uptime_ms / 1000);
	tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
	return 0;
}

int64_t sys_getitimer(int which, struct itimerval *curr_value)
{
	struct itimerval old;
	uint64_t now;

	if (!current_process)
		return -ESRCH;
	if (which != ITIMER_REAL)
		return -EINVAL;
	if (!curr_value)
		return -EFAULT;
	if (validate_userspace_buffer(curr_value, sizeof(struct itimerval)) != 0)
		return -EFAULT;

	now = clock_get_uptime_milliseconds();
	itimer_fill_old(current_process, now, &old);
	if (copy_to_user(curr_value, &old, sizeof(old)) != 0)
		return -EFAULT;
	return 0;
}

int64_t sys_setitimer(int which, const struct itimerval *new_value,
		      struct itimerval *old_value)
{
	struct itimerval kval;
	struct itimerval old;
	uint64_t now;
	uint64_t value_ms;
	uint64_t interval_ms;

	if (!current_process)
		return -ESRCH;
	if (which != ITIMER_REAL)
		return -EINVAL;

	now = clock_get_uptime_milliseconds();
	itimer_fill_old(current_process, now, &old);

	if (old_value)
	{
		if (validate_userspace_buffer(old_value, sizeof(struct itimerval)) != 0)
			return -EFAULT;
		if (copy_to_user(old_value, &old, sizeof(old)) != 0)
			return -EFAULT;
	}

	if (!new_value)
		return 0;

	if (validate_userspace_buffer((void *)new_value, sizeof(struct itimerval)) != 0)
		return -EFAULT;
	if (copy_from_user(&kval, new_value, sizeof(kval)) != 0)
		return -EFAULT;

	if (kval.it_value.tv_usec >= 1000000 || kval.it_value.tv_usec < 0 ||
	    kval.it_interval.tv_usec >= 1000000 || kval.it_interval.tv_usec < 0 ||
	    kval.it_value.tv_sec < 0 || kval.it_interval.tv_sec < 0)
		return -EINVAL;

	value_ms = timeval_to_ms(&kval.it_value);
	interval_ms = timeval_to_ms(&kval.it_interval);

	if (value_ms == 0)
	{
		current_process->it_real_expire_ms = 0;
		current_process->it_real_interval_ms = 0;
	}
	else
	{
		current_process->it_real_expire_ms = now + value_ms;
		current_process->it_real_interval_ms = interval_ms;
	}
	return 0;
}

/*
 * alarm(2) — one-shot ITIMER_REAL in whole seconds (musl may use setitimer).
 * Returns previous remaining seconds (rounded up), like Linux.
 */
int64_t sys_alarm(unsigned int seconds)
{
	struct itimerval old;
	struct itimerval neu;
	uint64_t now;
	uint64_t rem_ms;
	unsigned int prev;

	if (!current_process)
		return -ESRCH;

	now = clock_get_uptime_milliseconds();
	itimer_fill_old(current_process, now, &old);
	rem_ms = timeval_to_ms(&old.it_value);
	prev = (unsigned int)((rem_ms + 999UL) / 1000UL);

	memset(&neu, 0, sizeof(neu));
	neu.it_value.tv_sec = (time_t)seconds;
	if (seconds == 0)
	{
		current_process->it_real_expire_ms = 0;
		current_process->it_real_interval_ms = 0;
	}
	else
	{
		current_process->it_real_expire_ms = now + (uint64_t)seconds * 1000UL;
		current_process->it_real_interval_ms = 0;
	}
	(void)neu;
	return (int64_t)prev;
}
