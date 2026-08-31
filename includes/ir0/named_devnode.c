/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: named_devnode.c
 * Description: In-memory character/block device nodes for mknod paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "named_devnode.h"
#include <ir0/errno.h>
#include <ir0/path.h>
#include <ir0/types.h>
#include <ir0/arch_port.h>
#include <string.h>

#define NAMED_DEVNODE_MAX 32

#ifdef TEST_HOST
static inline uint64_t named_devnode_irq_save(void)
{
	return 0;
}

static inline void named_devnode_irq_restore(uint64_t flags)
{
	(void)flags;
}
#else
static inline uint64_t named_devnode_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void named_devnode_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}
#endif

struct named_devnode_entry
{
	char path[256];
	mode_t mode;
	dev_t rdev;
	int in_use;
};

static struct named_devnode_entry named_devnodes[NAMED_DEVNODE_MAX];

static struct named_devnode_entry *named_devnode_find(const char *path)
{
	char norm[256];
	int i;

	if (!path)
		return NULL;

	if (normalize_path(path, norm, sizeof(norm)) != 0)
		return NULL;

	for (i = 0; i < NAMED_DEVNODE_MAX; i++)
	{
		if (named_devnodes[i].in_use &&
		    strcmp(named_devnodes[i].path, norm) == 0)
			return &named_devnodes[i];
	}
	return NULL;
}

int named_devnode_create(const char *path, mode_t mode, dev_t rdev)
{
	struct named_devnode_entry *slot = NULL;
	uint64_t irq_flags;
	char norm[256];
	int i;

	if (!path || path[0] != '/')
		return -EINVAL;
	if (!S_ISCHR(mode) && !S_ISBLK(mode))
		return -EINVAL;
	if (normalize_path(path, norm, sizeof(norm)) != 0)
		return -ENAMETOOLONG;

	irq_flags = named_devnode_irq_save();

	if (named_devnode_find(norm))
	{
		named_devnode_irq_restore(irq_flags);
		return -EEXIST;
	}

	for (i = 0; i < NAMED_DEVNODE_MAX; i++)
	{
		if (!named_devnodes[i].in_use)
		{
			slot = &named_devnodes[i];
			break;
		}
	}
	if (!slot)
	{
		named_devnode_irq_restore(irq_flags);
		return -ENOSPC;
	}

	strncpy(slot->path, norm, sizeof(slot->path) - 1);
	slot->path[sizeof(slot->path) - 1] = '\0';
	slot->mode = mode;
	slot->rdev = rdev;
	slot->in_use = 1;

	named_devnode_irq_restore(irq_flags);
	return 0;
}

int named_devnode_stat(const char *path, stat_t *buf)
{
	struct named_devnode_entry *e;
	uint64_t irq_flags;

	if (!path || !buf)
		return -EINVAL;

	irq_flags = named_devnode_irq_save();
	e = named_devnode_find(path);
	if (!e)
	{
		named_devnode_irq_restore(irq_flags);
		return -ENOENT;
	}

	memset(buf, 0, sizeof(*buf));
	buf->st_mode = e->mode;
	buf->st_rdev = e->rdev;
	buf->st_nlink = 1;
	named_devnode_irq_restore(irq_flags);
	return 0;
}

int named_devnode_unlink(const char *path)
{
	struct named_devnode_entry *e;
	uint64_t irq_flags;

	if (!path)
		return -EINVAL;

	irq_flags = named_devnode_irq_save();
	e = named_devnode_find(path);
	if (!e)
	{
		named_devnode_irq_restore(irq_flags);
		return -ENOENT;
	}

	e->in_use = 0;
	e->path[0] = '\0';
	named_devnode_irq_restore(irq_flags);
	return 0;
}

int named_devnode_lookup(const char *path, mode_t *mode, dev_t *rdev)
{
	struct named_devnode_entry *e;
	uint64_t irq_flags;

	if (!path)
		return -EINVAL;

	irq_flags = named_devnode_irq_save();
	e = named_devnode_find(path);
	if (!e)
	{
		named_devnode_irq_restore(irq_flags);
		return -ENOENT;
	}

	if (mode)
		*mode = e->mode;
	if (rdev)
		*rdev = e->rdev;
	named_devnode_irq_restore(irq_flags);
	return 0;
}
