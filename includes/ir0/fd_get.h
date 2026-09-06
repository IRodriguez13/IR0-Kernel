/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fd_get.h
 * Description: Linux-like fcheck/fget lite — pin files_struct for one fd slot.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/fd_types.h>
#include <ir0/files_struct.h>

struct process;

typedef struct ir0_fd
{
	files_struct_t *files; /* held via files_get; NULL if empty handle */
	fd_entry_t *entry;     /* &files->fd_table[fd] while pinned */
	int fd;
} ir0_fd_t;

/*
 * Lookup @fd in @proc and pin the files_struct (files_get) so the table cannot
 * be freed under the caller. Does not bump per-backend refs (pipe/vfs/…) —
 * that remains install/close/fork ownership. Returns 0 or -errno; on success
 * caller must ir0_fd_put().
 */
int ir0_fd_get(struct process *proc, int fd, ir0_fd_t *out);
void ir0_fd_put(ir0_fd_t *h);

/* True if slot is a redirected stdio (not bare console). */
static inline int fd_entry_is_redirected(const fd_entry_t *e)
{
	if (!e || !e->in_use)
		return 0;
	if (e->is_devfs || e->is_pipe || e->is_socket || e->is_pseudo ||
	    e->is_epoll || e->is_memfd || e->is_eventfd || e->is_timerfd)
		return 1;
	if (e->vfs_file)
		return 1;
	return 0;
}
