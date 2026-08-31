/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: named_devnode.h
 * Description: In-memory character/block device nodes (mknod outside /dev)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/stat.h>
#include <ir0/types.h>

/*
 * Device nodes created by mknod(2) on paths that are not under /dev. The
 * node itself only records the (major,minor); the behaviour comes from the
 * devfs node claiming that rdev, so `mknod /tmp/n c 1 3` reads and writes
 * like /dev/null. Kept apart from the named-FIFO table because the open
 * path routes them differently and a FIFO must never be mistaken for a
 * device.
 */
int named_devnode_create(const char *path, mode_t mode, dev_t rdev);
int named_devnode_stat(const char *path, stat_t *buf);
int named_devnode_unlink(const char *path);
/* Returns 0 and fills @mode/@rdev when @path names a device node. */
int named_devnode_lookup(const char *path, mode_t *mode, dev_t *rdev);
