/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: statfs.h
 * Description: Linux x86-64 statfs(2) layout for BusyBox df / musl statvfs.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Match Linux uapi asm-generic/statfs.h on 64-bit (__fsword_t = long). */
struct ir0_statfs
{
	long f_type;
	long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	struct
	{
		int val[2];
	} f_fsid;
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

/* MINIX v1 magic (same as Linux MINIX_SUPER_MAGIC). */
#define IR0_MINIX_SUPER_MAGIC 0x137F
#define IR0_TMPFS_MAGIC       0x01021994
#define IR0_9P_MAGIC          0x01021997
/* Linux PROC_SUPER_MAGIC / SYSFS_MAGIC; devfs reports as tmpfs like devtmpfs. */
#define IR0_PROC_SUPER_MAGIC  0x9fa0
#define IR0_SYSFS_MAGIC       0x62656572
/* IR0-specific: "IR0H". /heart has no Linux counterpart. */
#define IR0_HEARTFS_MAGIC     0x49523048

int vfs_statfs(const char *path, struct ir0_statfs *buf);
