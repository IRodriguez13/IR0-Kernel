/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: files_struct.c
 * Description: files_struct refcount and process bind/share/clone helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/files_struct.h>
#include <ir0/errno.h>
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <string.h>

files_struct_t *files_create(void)
{
	files_struct_t *f;

	f = kmalloc_try(sizeof(*f));
	if (!f)
		return NULL;

	memset(f, 0, sizeof(*f));
	f->refcount = 1;
	return f;
}

files_struct_t *files_get(files_struct_t *f)
{
	if (!f)
		return NULL;
	f->refcount++;
	return f;
}

void files_put(files_struct_t *f)
{
	if (!f)
		return;

	if (f->refcount <= 0)
	{
		panic("files_put: refcount underflow");
		return;
	}

	f->refcount--;
	if (f->refcount > 0)
		return;

	kfree(f);
}

void process_files_bind(process_t *p, files_struct_t *f)
{
	if (!p)
		return;
	p->files = f;
}

static int process_files_acquire_entries(files_struct_t *f)
{
	int i;

	if (!f)
		return -EINVAL;

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &f->fd_table[i];

		if (!e->in_use)
			continue;
		if (e->is_pipe && e->vfs_file)
			pipe_acquire_end((pipe_t *)e->vfs_file, e->pipe_end);
		else if (e->is_socket && e->vfs_file)
		{
			if (sock_stream_is(e->vfs_file))
				sock_stream_acquire((struct sock_stream *)e->vfs_file);
			else if (sock_icmp_is(e->vfs_file))
				sock_icmp_acquire((struct sock_icmp *)e->vfs_file);
			else if (!sock_stream_is_slot(e->vfs_file))
				sock_udp_acquire((struct sock_udp *)e->vfs_file);
		}
		else if (e->is_devfs)
		{
			devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

			if (node)
				node->ref_count++;
			if (e->vfs_file &&
			    devfs_node_wants_text_snap(e->dev_device_id))
				devfs_text_snap_acquire(
					(devfs_text_snap_t *)e->vfs_file);
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			bind->refs++;
		}
		else if (e->is_epoll)
		{
			/* Share epoll interest list with parent (MVP). */
		}
		else if (e->is_memfd && e->vfs_file)
			ir0_memfd_acquire((struct ir0_memfd *)e->vfs_file);
		else if (e->is_eventfd && e->vfs_file)
			ir0_eventfd_acquire((struct ir0_eventfd *)e->vfs_file);
		else if (e->is_timerfd && e->vfs_file)
			ir0_timerfd_acquire((struct ir0_timerfd *)e->vfs_file);
		else if (e->vfs_file)
			vfs_file_acquire((struct vfs_file *)e->vfs_file);
	}
	return 0;
}

int process_files_share(process_t *child, process_t *parent)
{
	files_struct_t *f;

	if (!child || !parent)
		return -EINVAL;

	f = parent->files;
	if (!f)
		return -EINVAL;

	(void)files_get(f);
	process_files_bind(child, f);
	return 0;
}

int process_files_clone(process_t *child, process_t *parent)
{
	files_struct_t *nf;

	if (!child || !parent || !parent->files)
		return -EINVAL;

	nf = files_create();
	if (!nf)
		return -ENOMEM;

	memcpy(nf->fd_table, parent->files->fd_table, sizeof(nf->fd_table));
	if (process_files_acquire_entries(nf) != 0)
	{
		kfree(nf);
		return -ENOMEM;
	}

	process_files_bind(child, nf);
	return 0;
}
