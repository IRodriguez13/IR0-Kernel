/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: eventfd.c
 * Description: eventfd2 counter fd implementation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/eventfd.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/fcntl.h>
#include <ir0/arch_cpu.h>
#include <stdint.h>
#include <string.h>

extern fd_entry_t *get_process_fd_table(void);
extern void fd_slot_note_created(void);
extern void poll_wake_check(void);

#define EFD_MAX 32
#define EFD_MAGIC 0x45464421u /* EFD! */

struct ir0_eventfd
{
	uint32_t magic;
	int in_use;
	int refs;
	uint64_t count;
};

static struct ir0_eventfd g_efd[EFD_MAX];

int ir0_eventfd_is(const void *ptr)
{
	const struct ir0_eventfd *e = ptr;
	uintptr_t base = (uintptr_t)&g_efd[0];
	uintptr_t end = (uintptr_t)&g_efd[EFD_MAX];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_efd[0])) != 0)
		return 0;
	return e && e->magic == EFD_MAGIC && e->in_use;
}

void ir0_eventfd_acquire(struct ir0_eventfd *e)
{
	unsigned long irq_flags;

	if (!e || !ir0_eventfd_is(e))
		return;

	irq_flags = irq_save();
	e->refs++;
	irq_restore(irq_flags);
}

void ir0_eventfd_release(struct ir0_eventfd *e)
{
	unsigned long irq_flags;
	int last;

	if (!e || !ir0_eventfd_is(e))
		return;

	irq_flags = irq_save();
	if (e->refs > 0)
		e->refs--;
	last = (e->refs == 0);
	irq_restore(irq_flags);

	if (!last)
		return;
	memset(e, 0, sizeof(*e));
}

int ir0_eventfd_poll_readable(const struct ir0_eventfd *e)
{
	return (e && ir0_eventfd_is(e) && e->count > 0) ? 1 : 0;
}

int ir0_eventfd_poll_writable(const struct ir0_eventfd *e)
{
	return (e && ir0_eventfd_is(e) && e->count != UINT64_MAX) ? 1 : 0;
}

int64_t ir0_eventfd_read(struct ir0_eventfd *e, void *buf, size_t count, int nonblock)
{
	uint64_t v;

	if (!e || !ir0_eventfd_is(e) || !buf)
		return -EINVAL;
	if (count < sizeof(uint64_t))
		return -EINVAL;
	if (e->count == 0)
		return nonblock ? -EAGAIN : -EAGAIN;
	v = e->count;
	e->count = 0;
	poll_wake_check();
	if (copy_to_user(buf, &v, sizeof(v)) != 0)
		return -EFAULT;
	return (ssize_t)sizeof(v);
}

int64_t ir0_eventfd_write(struct ir0_eventfd *e, const void *buf, size_t count,
			  int nonblock)
{
	uint64_t add = 0;

	(void)nonblock;
	if (!e || !ir0_eventfd_is(e) || !buf)
		return -EINVAL;
	if (count < sizeof(uint64_t))
		return -EINVAL;
	if (copy_from_user(&add, buf, sizeof(add)) != 0)
		return -EFAULT;
	if (add == UINT64_MAX)
		return -EINVAL;
	if (e->count > UINT64_MAX - add)
		return -EAGAIN;
	e->count += add;
	poll_wake_check();
	return (ssize_t)sizeof(add);
}

int64_t sys_eventfd2(unsigned int count, int flags)
{
	struct ir0_eventfd *e = NULL;
	fd_entry_t *tab;
	int i;
	int fd;

	if (!current_process)
		return -ESRCH;
	if (flags & ~(IR0_EFD_CLOEXEC | IR0_EFD_NONBLOCK))
		return -EINVAL;
	for (i = 0; i < EFD_MAX; i++)
	{
		if (!g_efd[i].in_use)
		{
			e = &g_efd[i];
			break;
		}
	}
	if (!e)
		return -ENOMEM;
	memset(e, 0, sizeof(*e));
	e->in_use = 1;
	e->magic = EFD_MAGIC;
	e->refs = 1;
	e->count = count;
	tab = get_process_fd_table();
	if (!tab)
	{
		memset(e, 0, sizeof(*e));
		return -ESRCH;
	}
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!tab[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
	{
		memset(e, 0, sizeof(*e));
		return -EMFILE;
	}
	memset(&tab[fd], 0, sizeof(tab[fd]));
	tab[fd].in_use = true;
	tab[fd].is_eventfd = true;
	tab[fd].vfs_file = e;
	tab[fd].flags = O_RDWR;
	if (flags & IR0_EFD_NONBLOCK)
		tab[fd].flags |= O_NONBLOCK;
	if (flags & IR0_EFD_CLOEXEC)
		tab[fd].fd_flags = FD_CLOEXEC;
	fd_slot_note_created();
	return fd;
}
