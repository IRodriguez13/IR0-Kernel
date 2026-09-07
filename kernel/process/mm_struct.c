/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_struct.c
 * Description: mm_struct refcount and process bind/share helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/mm_struct.h>
#include <ir0/errno.h>
#include <string.h>

mm_struct_t *mm_create(void)
{
	mm_struct_t *mm;

	mm = kmalloc_try(sizeof(*mm));
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	mm->refcount = 1;
	return mm;
}

mm_struct_t *mm_get(mm_struct_t *mm)
{
	uint64_t irq_flags;

	if (!mm)
		return NULL;

	irq_flags = process_irq_save();
	mm->refcount++;
	process_irq_restore(irq_flags);
	return mm;
}

void mm_put(mm_struct_t *mm)
{
	uint64_t irq_flags;
	int last;

	if (!mm)
		return;

	irq_flags = process_irq_save();
	if (mm->refcount <= 0)
	{
		process_irq_restore(irq_flags);
		panic("mm_put: refcount underflow");
		return;
	}

	mm->refcount--;
	last = (mm->refcount == 0);
	process_irq_restore(irq_flags);

	if (!last)
		return;

	if (mm->page_directory && mm->owns_tables)
	{
		process_unmap_user_pages_all(mm->page_directory, NULL);
		paging_reclaim_user_tables(mm->page_directory);
		paging_ir0_mm_note_root_freed((uintptr_t)mm->page_directory);
		kfree_aligned(mm->page_directory);
		mm->page_directory = NULL;
	}

	{
		struct mmap_region *r = mm->mmap_list;
		struct mmap_region *next;

		while (r)
		{
			next = r->next;
			kfree(r);
			r = next;
		}
		mm->mmap_list = NULL;
	}
	kfree(mm);
}

void process_mm_bind(process_t *p, mm_struct_t *mm)
{
	if (!p)
		return;
	p->mm = mm;
}

int process_mm_share(process_t *child, process_t *parent)
{
	mm_struct_t *mm;

	if (!child || !parent)
		return -EINVAL;

	mm = parent->mm;
	if (!mm)
		return -EINVAL;

	(void)mm_get(mm);
	process_mm_bind(child, mm);
	return 0;
}

void process_mm_set_mmap_list(process_t *p, struct mmap_region *list)
{
	if (!p || !p->mm)
		return;
	p->mm->mmap_list = list;
}

int process_mm_owns_tables(const process_t *p)
{
	mm_struct_t *mm;

	if (!p)
		return 0;
	mm = p->mm;
	if (!mm)
		return 0;
	return (mm->owns_tables && mm->refcount == 1) ? 1 : 0;
}

int process_mm_ok(const process_t *p)
{
	return (p && p->mm) ? 1 : 0;
}
