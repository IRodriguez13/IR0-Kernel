/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: files_struct.h
 * Description: Shared open-files object (refcount) for fork / CLONE_FILES.
 *
 * Ownership:
 *   - files_create() returns refcount=1
 *   - files_get() / files_put() balance shares (CLONE_FILES)
 *   - files_put() does not close fds — caller must release entries first
 * May sleep: no.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/fd_types.h>
#include <stdint.h>

struct process;

/* Live object cookie; files_put poisons to IR0_FILES_MAGIC_DEAD before free. */
#define IR0_FILES_MAGIC		0x46494C45u /* 'FILE' */
#define IR0_FILES_MAGIC_DEAD	0xDEADF11Eu

typedef struct files_struct
{
	uint32_t magic;
	int refcount;
	fd_entry_t fd_table[MAX_FDS_PER_PROCESS];
} files_struct_t;

/*
 * True if @f is a live kernel files_struct. Rejects NULL, non-heap pointers
 * (garbage like 0x220000000b), bad magic, and dead refcount before any
 * fd_table[] load. IR0 kmalloc uses identity heap — do not use mm_user_va_ok.
 */
int files_struct_live(const files_struct_t *f);

files_struct_t *files_create(void);
files_struct_t *files_get(files_struct_t *f);
void files_put(files_struct_t *f);

void process_files_bind(struct process *p, files_struct_t *f);

/* CLONE_FILES: share parent's table (same memory). Returns 0 or -errno. */
int process_files_share(struct process *child, struct process *parent);

/* fork: duplicate table into child's private files_struct. Returns 0 or -errno. */
int process_files_clone(struct process *child, struct process *parent);
