/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fdtable.c
 * Description: Process fd_table init, release on exit/destroy, and fork duplicate.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>

void process_release_fds(process_t *p, const char *pipe_trace_op)
{
	fd_entry_t *table;
	int i;

	if (!p || !p->files)
		return;

	/*
	 * Shared files_struct (CLONE_FILES): other processes still use the table;
	 * only the last reference closes underlying handles.
	 */
	if (p->files->refcount != 1)
		return;

	table = p->files->fd_table;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &table[i];

		if (!e->in_use)
			continue;

		if (e->is_pipe && e->vfs_file)
		{
			pipe_t *pip = (pipe_t *)e->vfs_file;
			int refs_before = pip ? pip->fd_refs : -1;

			if (DEBUG_FASE50)
			{
				klog_debug_fmt("KERN", "%x fd=%llx refs_before=%llx end=%llx", (unsigned)((uint32_t)p->task.pid), (unsigned long long)((uint64_t)i), (unsigned long long)((uint64_t)refs_before), (unsigned long long)((uint64_t)e->pipe_end));
			}
			/* pipe_close_end wakes waiters then frees on last ref. */
			pipe_close_end(pip, e->pipe_end);
			e->vfs_file = NULL;
		}
		else if (i <= 2)
			goto clear_fd;
		else if (e->is_socket && e->vfs_file)
		{
			if (sock_stream_is(e->vfs_file))
				sock_stream_release((struct sock_stream *)e->vfs_file);
			else if (sock_icmp_is(e->vfs_file))
				sock_icmp_release((struct sock_icmp *)e->vfs_file);
			else if (!sock_stream_is_slot(e->vfs_file))
				sock_udp_release((struct sock_udp *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_devfs)
		{
			devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

			if (e->vfs_file &&
			    devfs_node_wants_text_snap(e->dev_device_id))
			{
				devfs_text_snap_release(
					(devfs_text_snap_t *)e->vfs_file);
				e->vfs_file = NULL;
			}
			if (node)
				devfs_close_node(node);
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			if (bind->refs > 0)
				bind->refs--;
			if (bind->refs == 0)
			{
				(void)pseudo_fs_release_ops(
					(const pseudo_fs_ops_t *)bind->ops,
					bind->ctx, bind->dynamic);
				kfree(bind);
			}
			e->vfs_file = NULL;
		}
		else if (e->is_epoll && e->vfs_file)
		{
			extern void epoll_release_fd(void *epoll_state);

			epoll_release_fd(e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_memfd && e->vfs_file)
		{
			ir0_memfd_release((struct ir0_memfd *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_eventfd && e->vfs_file)
		{
			ir0_eventfd_release((struct ir0_eventfd *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_timerfd && e->vfs_file)
		{
			ir0_timerfd_release((struct ir0_timerfd *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->vfs_file)
		{
			vfs_close((struct vfs_file *)e->vfs_file);
			e->vfs_file = NULL;
		}

clear_fd:
		e->in_use = false;
		e->is_pipe = false;
		e->is_socket = false;
		e->is_devfs = false;
		e->is_pseudo = false;
		e->is_epoll = false;
		e->is_memfd = false;
		e->is_eventfd = false;
		e->is_timerfd = false;
		e->dev_device_id = 0;
		e->pipe_end = -1;
		e->path[0] = '\0';
		e->flags = 0;
		e->fd_flags = 0;
		e->offset = 0;
	}
}

int process_duplicate_fd_table(process_t *parent, process_t *child)
{
	return process_files_clone(child, parent);
}

void process_init_fd_table(process_t *process)
{
	fd_entry_t *table;
	int i;

	if (!process)
		return;

	if (!process->files)
	{
		process->files = files_create();
		if (!process->files)
			return;
	}

	table = process->files->fd_table;

	/* Initialize all FDs as unused */
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		table[i].in_use = false;
		table[i].path[0] = '\0';
		table[i].flags = 0;
		table[i].fd_flags = 0;
		table[i].offset = 0;
		table[i].vfs_file = NULL;
		table[i].is_pipe = false;
		table[i].is_socket = false;
		table[i].is_devfs = false;
		table[i].is_pseudo = false;
		table[i].is_epoll = false;
		table[i].is_memfd = false;
		table[i].is_eventfd = false;
		table[i].is_timerfd = false;
		table[i].dev_device_id = 0;
	}

	/* Setup standard streams */
	table[0].in_use = true;
	strncpy(table[0].path, "/dev/stdin",
		sizeof(table[0].path) - 1);

	table[1].in_use = true;
	strncpy(table[1].path, "/dev/stdout",
		sizeof(table[1].path) - 1);

	table[2].in_use = true;
	strncpy(table[2].path, "/dev/stderr",
		sizeof(table[2].path) - 1);
}
