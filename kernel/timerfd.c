/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: timerfd.c
 * Description: timerfd CLOCK_MONOTONIC MVP.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/timerfd.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/fcntl.h>
#include <ir0/clock.h>
#include <ir0/time.h>
#include <ir0/arch_cpu.h>
#include <string.h>

extern fd_entry_t *get_process_fd_table(void);
extern void fd_slot_note_created(void);
extern void poll_wake_check(void);

#define TFD_MAX 16
#define TFD_MAGIC 0x54464421u /* TFD! */

struct ir0_timerfd
{
	uint32_t magic;
	int in_use;
	int refs;
	uint64_t expire_ms;
	uint64_t interval_ms;
	uint64_t ticks;
};

static struct ir0_timerfd g_tfd[TFD_MAX];

static void timerfd_tick(struct ir0_timerfd *t)
{
	uint64_t now;

	if (!t || !t->in_use || t->expire_ms == 0)
		return;
	now = clock_get_uptime_milliseconds();
	while (t->expire_ms != 0 && now >= t->expire_ms)
	{
		t->ticks++;
		if (t->interval_ms == 0)
		{
			t->expire_ms = 0;
			break;
		}
		t->expire_ms += t->interval_ms;
		if (t->expire_ms <= now && t->interval_ms > 0)
			continue;
		break;
	}
	if (t->ticks)
		poll_wake_check();
}

int ir0_timerfd_is(const void *ptr)
{
	const struct ir0_timerfd *t = ptr;
	uintptr_t base = (uintptr_t)&g_tfd[0];
	uintptr_t end = (uintptr_t)&g_tfd[TFD_MAX];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_tfd[0])) != 0)
		return 0;
	return t && t->magic == TFD_MAGIC && t->in_use;
}

void ir0_timerfd_acquire(struct ir0_timerfd *t)
{
	unsigned long irq_flags;

	if (!t || !ir0_timerfd_is(t))
		return;

	irq_flags = irq_save();
	t->refs++;
	irq_restore(irq_flags);
}

void ir0_timerfd_release(struct ir0_timerfd *t)
{
	unsigned long irq_flags;
	int last;

	if (!t || !ir0_timerfd_is(t))
		return;

	irq_flags = irq_save();
	if (t->refs > 0)
		t->refs--;
	last = (t->refs == 0);
	irq_restore(irq_flags);

	if (!last)
		return;
	memset(t, 0, sizeof(*t));
}

int ir0_timerfd_poll_readable(struct ir0_timerfd *t)
{
	if (!t || !ir0_timerfd_is(t))
		return 0;
	timerfd_tick(t);
	return t->ticks > 0 ? 1 : 0;
}

int64_t ir0_timerfd_read(struct ir0_timerfd *t, void *buf, size_t count, int nonblock)
{
	uint64_t v;

	if (!t || !ir0_timerfd_is(t) || !buf)
		return -EINVAL;
	if (count < sizeof(uint64_t))
		return -EINVAL;
	timerfd_tick(t);
	if (t->ticks == 0)
		return nonblock ? -EAGAIN : -EAGAIN;
	v = t->ticks;
	t->ticks = 0;
	if (copy_to_user(buf, &v, sizeof(v)) != 0)
		return -EFAULT;
	return (ssize_t)sizeof(v);
}

int64_t sys_timerfd_create(int clockid, int flags)
{
	struct ir0_timerfd *t = NULL;
	fd_entry_t *tab;
	int i;
	int fd;

	if (!current_process)
		return -ESRCH;
	if (clockid != IR0_CLOCK_MONOTONIC)
		return -EINVAL;
	if (flags & ~(IR0_TFD_CLOEXEC | IR0_TFD_NONBLOCK))
		return -EINVAL;
	for (i = 0; i < TFD_MAX; i++)
	{
		if (!g_tfd[i].in_use)
		{
			t = &g_tfd[i];
			break;
		}
	}
	if (!t)
		return -ENOMEM;
	memset(t, 0, sizeof(*t));
	t->in_use = 1;
	t->magic = TFD_MAGIC;
	t->refs = 1;
	tab = get_process_fd_table();
	if (!tab)
	{
		memset(t, 0, sizeof(*t));
		return -ESRCH;
	}
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!tab[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
	{
		memset(t, 0, sizeof(*t));
		return -EMFILE;
	}
	memset(&tab[fd], 0, sizeof(tab[fd]));
	tab[fd].in_use = true;
	tab[fd].is_timerfd = true;
	tab[fd].vfs_file = t;
	tab[fd].flags = O_RDWR;
	if (flags & IR0_TFD_NONBLOCK)
		tab[fd].flags |= O_NONBLOCK;
	if (flags & IR0_TFD_CLOEXEC)
		tab[fd].fd_flags = FD_CLOEXEC;
	fd_slot_note_created();
	return fd;
}

static uint64_t timespec_to_ms(const struct timespec *ts)
{
	if (!ts)
		return 0;
	return (uint64_t)ts->tv_sec * 1000ull + (uint64_t)ts->tv_nsec / 1000000ull;
}

static void ms_to_timespec(uint64_t ms, struct timespec *ts)
{
	if (!ts)
		return;
	ts->tv_sec = (time_t)(ms / 1000ull);
	ts->tv_nsec = (long)((ms % 1000ull) * 1000000ull);
}

int64_t sys_timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
			    struct itimerspec *old_value)
{
	fd_entry_t *tab;
	struct ir0_timerfd *t;
	struct itimerspec nv;
	struct itimerspec ov;
	uint64_t now;
	uint64_t value_ms;
	uint64_t interval_ms;

	(void)flags;
	if (!current_process)
		return -ESRCH;
	if (!new_value)
		return -EFAULT;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;
	tab = get_process_fd_table();
	if (!tab || !tab[fd].in_use || !tab[fd].is_timerfd || !tab[fd].vfs_file)
		return -EBADF;
	t = (struct ir0_timerfd *)tab[fd].vfs_file;
	if (!ir0_timerfd_is(t))
		return -EBADF;
	if (copy_from_user(&nv, new_value, sizeof(nv)) != 0)
		return -EFAULT;
	timerfd_tick(t);
	if (old_value)
	{
		memset(&ov, 0, sizeof(ov));
		now = clock_get_uptime_milliseconds();
		if (t->expire_ms > now)
			ms_to_timespec(t->expire_ms - now, &ov.it_value);
		ms_to_timespec(t->interval_ms, &ov.it_interval);
		if (copy_to_user(old_value, &ov, sizeof(ov)) != 0)
			return -EFAULT;
	}
	value_ms = timespec_to_ms(&nv.it_value);
	interval_ms = timespec_to_ms(&nv.it_interval);
	now = clock_get_uptime_milliseconds();
	t->ticks = 0;
	if (value_ms == 0)
		t->expire_ms = 0;
	else
		t->expire_ms = now + value_ms;
	t->interval_ms = interval_ms;
	timerfd_tick(t);
	return 0;
}

int64_t sys_timerfd_gettime(int fd, struct itimerspec *curr_value)
{
	fd_entry_t *tab;
	struct ir0_timerfd *t;
	struct itimerspec ov;
	uint64_t now;

	if (!current_process || !curr_value)
		return -EFAULT;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;
	tab = get_process_fd_table();
	if (!tab || !tab[fd].in_use || !tab[fd].is_timerfd || !tab[fd].vfs_file)
		return -EBADF;
	t = (struct ir0_timerfd *)tab[fd].vfs_file;
	if (!ir0_timerfd_is(t))
		return -EBADF;
	timerfd_tick(t);
	memset(&ov, 0, sizeof(ov));
	now = clock_get_uptime_milliseconds();
	if (t->expire_ms > now)
		ms_to_timespec(t->expire_ms - now, &ov.it_value);
	ms_to_timespec(t->interval_ms, &ov.it_interval);
	if (copy_to_user(curr_value, &ov, sizeof(ov)) != 0)
		return -EFAULT;
	return 0;
}
