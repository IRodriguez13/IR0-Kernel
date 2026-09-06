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
#include <mm/allocator.h>
#include <string.h>

int files_struct_live(const files_struct_t *f)
{
	uintptr_t p = (uintptr_t)f;
	uintptr_t end;

	if (!f)
		return 0;

	/* IR0 kmalloc identity window — see mm/allocator.h. */
	if (p < (uintptr_t)SIMPLE_HEAP_START)
		return 0;
	end = p + sizeof(*f);
	if (end < p || end > (uintptr_t)SIMPLE_HEAP_END)
		return 0;

	if (f->magic != IR0_FILES_MAGIC)
		return 0;
	if (f->refcount <= 0)
		return 0;
	return 1;
}

files_struct_t *files_create(void)
{
	files_struct_t *f;

	f = kmalloc_try(sizeof(*f));
	if (!f)
		return NULL;

	memset(f, 0, sizeof(*f));
	f->magic = IR0_FILES_MAGIC;
	f->refcount = 1;
	return f;
}

files_struct_t *files_get(files_struct_t *f)
{
	uint64_t irq_flags;

	if (!files_struct_live(f))
		return NULL;

	irq_flags = process_irq_save();
	f->refcount++;
	process_irq_restore(irq_flags);
	return f;
}

void files_put(files_struct_t *f)
{
	uint64_t irq_flags;
	int last;

	if (!f)
		return;

	/*
	 * Refuse userspace / dead / corrupt pointers before touching refcount.
	 * Already-freed objects are not safely detectable after kfree.
	 */
	if (!files_struct_live(f))
		return;

	irq_flags = process_irq_save();
	if (f->refcount <= 0)
	{
		process_irq_restore(irq_flags);
		panic("files_put: refcount underflow");
		return;
	}

	f->refcount--;
	last = (f->refcount == 0);
	process_irq_restore(irq_flags);

	if (!last)
		return;

	f->magic = IR0_FILES_MAGIC_DEAD;
	memset(f->fd_table, 0xA5, sizeof(f->fd_table));
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

			pseudo_fd_bind_acquire(bind);
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
	if (!files_struct_live(f))
		return -EINVAL;

	if (!files_get(f))
		return -EINVAL;
	process_files_bind(child, f);
	return 0;
}

int process_files_clone(process_t *child, process_t *parent)
{
	files_struct_t *nf;

	if (!child || !parent || !files_struct_live(parent->files))
		return -EINVAL;

	nf = files_create();
	if (!nf)
		return -ENOMEM;

	memcpy(nf->fd_table, parent->files->fd_table, sizeof(nf->fd_table));
	if (process_files_acquire_entries(nf) != 0)
	{
		files_put(nf);
		return -ENOMEM;
	}

	process_files_bind(child, nf);
	return 0;
}
