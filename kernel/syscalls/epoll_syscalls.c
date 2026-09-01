/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: epoll_syscalls.c
 * Description: Minimal epoll_create1/ctl/wait + pselect6 (poll-backed).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "epoll_syscalls.h"
#include "io_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/poll.h>
#include <ir0/process.h>
#include <ir0/files_struct.h>
#include <ir0/time.h>
#include <ir0/clock.h>
#include <string.h>

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLL_MAX_SLOTS 16
#define EPOLL_MAX_INTEREST 32

struct epoll_event
{
	uint32_t events;
	uint64_t data;
} __attribute__((packed));

struct epoll_interest
{
	int fd;
	uint32_t events;
	uint64_t data;
	int in_use;
};

struct epoll_state
{
	int in_use;
	struct epoll_interest interest[EPOLL_MAX_INTEREST];
};

static struct epoll_state g_epoll[EPOLL_MAX_SLOTS];

static int epoll_check_ready(struct pollfd *fds, unsigned int nfds)
{
	int count = 0;
	unsigned int i;

	if (!current_process)
		return 0;
	for (i = 0; i < nfds; i++)
	{
		fds[i].revents = 0;
		if (fds[i].fd < 0)
			continue;
		if ((fds[i].events & POLLIN) &&
		    fd_can_read_for(current_process, fds[i].fd))
			fds[i].revents |= POLLIN;
		if ((fds[i].events & POLLOUT) &&
		    fd_can_write_for(current_process, fds[i].fd))
			fds[i].revents |= POLLOUT;
		if (fds[i].revents)
			count++;
	}
	return count;
}

static struct epoll_state *epoll_from_fd(int epfd)
{
	fd_entry_t *tab;

	if (!current_process || epfd < 0 || epfd >= MAX_FDS_PER_PROCESS)
		return NULL;
	tab = process_fd_table(current_process);
	if (!tab || !tab[epfd].in_use || !tab[epfd].is_epoll || !tab[epfd].vfs_file)
		return NULL;
	return (struct epoll_state *)tab[epfd].vfs_file;
}

void epoll_release_fd(void *epoll_state)
{
	struct epoll_state *ep = (struct epoll_state *)epoll_state;

	if (!ep)
		return;
	memset(ep, 0, sizeof(*ep));
}

int64_t sys_epoll_create1(int flags)
{
	int slot = -1;
	int fd;
	fd_entry_t *tab;

	(void)flags;
	if (!current_process)
		return -ESRCH;
	for (int i = 0; i < EPOLL_MAX_SLOTS; i++)
	{
		if (!g_epoll[i].in_use)
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return -EMFILE;

	tab = process_fd_table(current_process);
	if (!tab)
		return -ESRCH;
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!tab[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
		return -EMFILE;

	memset(&g_epoll[slot], 0, sizeof(g_epoll[slot]));
	g_epoll[slot].in_use = 1;
	memset(&tab[fd], 0, sizeof(tab[fd]));
	tab[fd].in_use = true;
	tab[fd].is_epoll = true;
	tab[fd].vfs_file = &g_epoll[slot];
	tab[fd].flags = 0;
	fd_slot_note_created();
	return fd;
}

	int64_t sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	struct epoll_state *ep;
	struct epoll_event ev;
	fd_entry_t *tab;
	int free_i = -1;

	ep = epoll_from_fd(epfd);
	if (!ep)
		return -EBADF;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;
	tab = process_fd_table(current_process);
	if (!tab || !tab[fd].in_use)
		return -EBADF;
	if (fd == epfd)
		return -EINVAL;

	if (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD)
	{
		if (!event || validate_userspace_buffer(event, sizeof(ev)) != 0)
			return -EFAULT;
		if (copy_from_user(&ev, event, sizeof(ev)) != 0)
			return -EFAULT;
	}

	for (int i = 0; i < EPOLL_MAX_INTEREST; i++)
	{
		if (!ep->interest[i].in_use)
		{
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (ep->interest[i].fd == fd)
		{
			if (op == EPOLL_CTL_DEL)
			{
				ep->interest[i].in_use = 0;
				return 0;
			}
			if (op == EPOLL_CTL_ADD)
				return -EEXIST;
			if (op == EPOLL_CTL_MOD)
			{
				ep->interest[i].events = ev.events;
				ep->interest[i].data = ev.data;
				return 0;
			}
			return -EINVAL;
		}
	}

	if (op == EPOLL_CTL_DEL)
		return -ENOENT;
	if (op != EPOLL_CTL_ADD)
		return -EINVAL;
	if (free_i < 0)
		return -ENOSPC;
	ep->interest[free_i].in_use = 1;
	ep->interest[free_i].fd = fd;
	ep->interest[free_i].events = ev.events;
	ep->interest[free_i].data = ev.data;
	return 0;
}

int64_t sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
		       int timeout)
{
	struct epoll_state *ep;
	struct pollfd pfds[EPOLL_MAX_INTEREST];
	unsigned int nfds = 0;
	int map[EPOLL_MAX_INTEREST];
	int ready;
	int out = 0;
	uint64_t expire;

	ep = epoll_from_fd(epfd);
	if (!ep)
		return -EBADF;
	if (maxevents <= 0)
		return -EINVAL;
	if (!events ||
	    validate_userspace_buffer(events,
				      (size_t)maxevents * sizeof(struct epoll_event)) != 0)
		return -EFAULT;

	for (int i = 0; i < EPOLL_MAX_INTEREST; i++)
	{
		if (!ep->interest[i].in_use)
			continue;
		pfds[nfds].fd = ep->interest[i].fd;
		pfds[nfds].events = 0;
		if (ep->interest[i].events & EPOLLIN)
			pfds[nfds].events |= POLLIN;
		if (ep->interest[i].events & EPOLLOUT)
			pfds[nfds].events |= POLLOUT;
		pfds[nfds].revents = 0;
		map[nfds] = i;
		nfds++;
	}

	if (nfds == 0)
		return 0;

	expire = (timeout < 0) ? (uint64_t)-1
			       : (clock_get_uptime_milliseconds() + (uint64_t)timeout);

	for (;;)
	{
		ready = epoll_check_ready(pfds, nfds);
		if (ready > 0 || timeout == 0)
			break;
		if (timeout >= 0 && expire != (uint64_t)-1 &&
		    clock_get_uptime_milliseconds() >= expire)
			break;
		if (current_process->signal_pending != 0)
			return -EINTR;
		{
			int64_t ret = syscall_sleep_ms_locked(50);

			if (ret < 0)
				return ret;
		}
	}

	for (unsigned int i = 0; i < nfds && out < maxevents; i++)
	{
		struct epoll_event ev;
		int ii;

		if (pfds[i].revents == 0)
			continue;
		ii = map[i];
		ev.events = 0;
		if (pfds[i].revents & POLLIN)
			ev.events |= EPOLLIN;
		if (pfds[i].revents & POLLOUT)
			ev.events |= EPOLLOUT;
		if (pfds[i].revents & POLLERR)
			ev.events |= EPOLLERR;
		ev.data = ep->interest[ii].data;
		if (copy_to_user(&events[out], &ev, sizeof(ev)) != 0)
			return -EFAULT;
		out++;
	}
	return out;
}

int64_t sys_epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
			int timeout, const void *sigmask, size_t sigsetsize)
{
	(void)sigmask;
	(void)sigsetsize;
	return sys_epoll_wait(epfd, events, maxevents, timeout);
}

int64_t sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds,
		     fd_set *exceptfds, const struct timespec *timeout,
		     const void *sigmask)
{
	(void)sigmask;

	if (timeout)
	{
		struct timespec ts;
		int timeout_ms;

		if (validate_userspace_buffer((void *)timeout, sizeof(ts)) != 0)
			return -EFAULT;
		if (copy_from_user(&ts, timeout, sizeof(ts)) != 0)
			return -EFAULT;
		if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
			return -EINVAL;
		timeout_ms = (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
		return io_select_timeout_ms(nfds, readfds, writefds, exceptfds,
					    timeout_ms, 1);
	}
	return io_select_timeout_ms(nfds, readfds, writefds, exceptfds, -1, 0);
}
