/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: host_fase48_stub.c
 * Description: No-op fase48 FD stats + KTM emit stubs for host-linked pipe.c
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

void fd_slot_note_created(void)
{
}

void fd_slot_stats_get(uint64_t *created, uint64_t *destroyed,
			 uint64_t *blocked_readers, uint64_t *blocked_writers)
{
	if (created)
		*created = 0;
	if (destroyed)
		*destroyed = 0;
	if (blocked_readers)
		*blocked_readers = 0;
	if (blocked_writers)
		*blocked_writers = 0;
}

void ktm_event_emit4(uint16_t type, uint16_t subsystem,
		     uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	(void)type;
	(void)subsystem;
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

#include <stdbool.h>

bool ktm_fault_should_fail(const char *name)
{
	(void)name;
	return false;
}

/* Kernel wake lives in io_syscalls.c; host needs a no-op for pipe_close_end. */
struct pipe;
void pipe_wake_all(struct pipe *pipe)
{
	(void)pipe;
}
