/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exec.c
 * Description: Exec-path helpers: close FD_CLOEXEC fds before image replace.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

void process_exec_close_cloexec(process_t *p)
{
	fd_entry_t *table;
	int i;

	if (!p)
		return;

	table = process_fd_table(p);
	if (!table)
		return;

	for (i = 3; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &table[i];

		if (!e->in_use)
			continue;
		if (!(e->fd_flags & FD_CLOEXEC))
			continue;
		(void)process_close_fd(p, i);
	}
}

