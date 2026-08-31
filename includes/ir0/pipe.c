/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pipe.c
 * Description: IPC pipes — FASE49 FD/pipe lifetime + EOF/EPIPE semantics
 */

#include "pipe.h"
#include <ir0/kmem.h>
#include <ir0/arch_cpu.h>
#include <ir0/errno.h>
#include <ir0/ktm/event.h>
#include <ir0/ktm/fault.h>
#include <string.h>
#include <config.h>
static uint64_t fase48_pipe_created;
static uint64_t fase48_pipe_destroyed;
static uint64_t fase49_next_pipe_id = 1;

static void fase49_pipe_line(pipe_t *pipe, const char *event)
{
	if (!pipe || !event)
		return;

}

void pipe_fase49_fd_trace(uint32_t pid, int fd, pipe_t *pipe, int end,
			  int refcount, const char *op)
{
	(void)pid;
	(void)fd;
	(void)pipe;
	(void)end;
	(void)refcount;
	(void)op;
}

void pipe_fase48_get_stats(uint64_t *created, uint64_t *destroyed)
{
	if (created)
		*created = fase48_pipe_created;
	if (destroyed)
		*destroyed = fase48_pipe_destroyed;
}

pipe_t *pipe_create(void)
{
	pipe_t *pipe;

	if (KTM_FAULT_HIT("pipe.create"))
		return NULL;

	pipe = kmalloc_try(sizeof(pipe_t));
	if (!pipe)
		return NULL;

	memset(pipe, 0, sizeof(*pipe));
	pipe->pipe_id = fase49_next_pipe_id++;
	fase48_pipe_created++;
	fase49_pipe_line(pipe, "CREATE");
	ktm_event_emit4(KTM_EVENT_PIPE_CREATE, KTM_SUBSYS_IPC, pipe->pipe_id, 0, 0, 0);
	return pipe;
}

/*
 * Register one open fd slot on @end (0 read, 1 write).
 * Called from pipe2 install, dup/dup2, and fork inheritance.
 */
void pipe_acquire_end(pipe_t *pipe, int end)
{
	if (!pipe)
		return;

	if (end != 0 && end != 1)
		return;

	pipe->fd_refs++;
	if (end == 0)
	{
		pipe->readers++;
		/* Reopen after last closer: named FIFOs stay allocated. */
		if (pipe->named)
			pipe->closed_read = 0;
	}
	else
	{
		pipe->writers++;
		if (pipe->named)
			pipe->closed_write = 0;
	}

	fase49_pipe_line(pipe, "ACQUIRE");
	ktm_event_emit4(KTM_EVENT_PIPE_END_ACQUIRE, KTM_SUBSYS_IPC,
			pipe->pipe_id, (uint64_t)(uint32_t)end,
			(uint64_t)(uint32_t)pipe->readers,
			(uint64_t)(uint32_t)pipe->writers);
}

void pipe_acquire(pipe_t *pipe)
{
	if (!pipe)
		return;

	pipe->fd_refs++;
	fase49_pipe_line(pipe, "ACQUIRE");
}

int pipe_read(pipe_t *pipe, void *buf, size_t count)
{
	if (!pipe || !buf)
		return -EINVAL;

	/*
	 * Sampling count, draining the ring and updating count must be one
	 * critical section. `pipe->count -= n` is a read-modify-write: preempted
	 * between the load and the store it silently drops a concurrent writer's
	 * increment, count ends up larger than the bytes actually queued, and the
	 * next reader hands userspace that much stale ring content — NUL runs on a
	 * fresh pipe, old bytes on a reused one. Linux serialises the same window
	 * with the pipe mutex; IR0 IPC uses the irq-save pattern (kernel/ipc.c).
	 * Bounded by PIPE_SIZE (4 KiB), so the hold time stays comparable.
	 */
	unsigned long irq_flags = irq_save();
	size_t to_read;
	char *dest = (char *)buf;
	size_t bytes_read = 0;
	uint32_t writers_snapshot;
	uint32_t readers_snapshot;
	size_t count_snapshot;

	if (pipe->count == 0)
	{
		writers_snapshot = (uint32_t)pipe->writers;
		readers_snapshot = (uint32_t)pipe->readers;
		count_snapshot = pipe->count;
		irq_restore(irq_flags);

		if ((int)writers_snapshot <= 0)
		{
			fase49_pipe_line(pipe, "EOF");
			ktm_event_emit4(KTM_EVENT_PIPE_EOF, KTM_SUBSYS_IPC,
					pipe->pipe_id,
					(uint64_t)readers_snapshot,
					(uint64_t)writers_snapshot,
					(uint64_t)count_snapshot);
			return 0;
		}
		ktm_event_emit4(KTM_EVENT_PIPE_READ, KTM_SUBSYS_IPC,
				pipe->pipe_id, (uint64_t)(int64_t)-EAGAIN,
				(uint64_t)writers_snapshot,
				(uint64_t)count_snapshot);
		return -EAGAIN;
	}

	to_read = (count < pipe->count) ? count : pipe->count;

	while (bytes_read < to_read)
	{
		dest[bytes_read] = pipe->buffer[pipe->read_pos];
		pipe->read_pos = (pipe->read_pos + 1) % PIPE_SIZE;
		bytes_read++;
	}

	pipe->count -= bytes_read;
	writers_snapshot = (uint32_t)pipe->writers;
	count_snapshot = pipe->count;
	irq_restore(irq_flags);

	ktm_event_emit4(KTM_EVENT_PIPE_READ, KTM_SUBSYS_IPC, pipe->pipe_id,
			(uint64_t)bytes_read,
			(uint64_t)writers_snapshot,
			(uint64_t)count_snapshot);
	return (int)bytes_read;
}

int pipe_write(pipe_t *pipe, const void *buf, size_t count)
{
	if (!pipe || !buf)
		return -EINVAL;

	/* Same critical section as pipe_read: see the comment there. */
	unsigned long irq_flags = irq_save();
	size_t space;
	size_t to_write;
	const char *src = (const char *)buf;
	size_t bytes_written = 0;
	uint32_t readers_snapshot;
	uint32_t writers_snapshot;
	size_t count_snapshot;

	if (pipe->readers <= 0)
	{
		readers_snapshot = (uint32_t)pipe->readers;
		writers_snapshot = (uint32_t)pipe->writers;
		count_snapshot = pipe->count;
		irq_restore(irq_flags);

		ktm_event_emit4(KTM_EVENT_PIPE_EPIPE, KTM_SUBSYS_IPC, pipe->pipe_id,
				(uint64_t)readers_snapshot,
				(uint64_t)writers_snapshot,
				(uint64_t)count_snapshot);
		return -EPIPE;
	}

	if (pipe->count >= PIPE_SIZE)
	{
		irq_restore(irq_flags);
		return -EAGAIN;
	}

	space = PIPE_SIZE - pipe->count;
	to_write = (count < space) ? count : space;

	while (bytes_written < to_write)
	{
		pipe->buffer[pipe->write_pos] = src[bytes_written];
		pipe->write_pos = (pipe->write_pos + 1) % PIPE_SIZE;
		bytes_written++;
	}

	pipe->count += bytes_written;
	readers_snapshot = (uint32_t)pipe->readers;
	count_snapshot = pipe->count;
	irq_restore(irq_flags);

	ktm_event_emit4(KTM_EVENT_PIPE_WRITE, KTM_SUBSYS_IPC, pipe->pipe_id,
			(uint64_t)bytes_written,
			(uint64_t)readers_snapshot,
			(uint64_t)count_snapshot);

	return (int)bytes_written;
}

void pipe_close_end(pipe_t *pipe, int end)
{
	int last = 0;

	if (!pipe)
		return;

	if (end != 0 && end != 1)
		return;

	if (end == 0)
	{
		if (pipe->readers > 0)
			pipe->readers--;
		if (!pipe->closed_read)
			pipe->closed_read = 1;
	}
	else
	{
		if (pipe->writers > 0)
			pipe->writers--;
		if (!pipe->closed_write)
			pipe->closed_write = 1;
	}

	/*
	 * Free only on the 1→0 transition. fd_refs<=0 must not call kfree again
	 * (stale pointer after a prior last-close → double-free panic).
	 * Named FIFOs are owned by the inode table — never free here.
	 */
	if (pipe->fd_refs > 0)
	{
		pipe->fd_refs--;
		last = (pipe->fd_refs == 0);
	}

	fase49_pipe_line(pipe, "CLOSE");
	ktm_event_emit4(KTM_EVENT_PIPE_END_CLOSE, KTM_SUBSYS_IPC, pipe->pipe_id,
			(uint64_t)(uint32_t)end,
			(uint64_t)(uint32_t)pipe->readers,
			(uint64_t)(uint32_t)pipe->writers);

	/*
	 * Wake waiters while pipe_t is still alive. Callers must not
	 * pipe_wake_all() after this returns when last==1 (object may be gone).
	 */
	{
		extern void pipe_wake_all(pipe_t *p);

		pipe_wake_all(pipe);
	}

	if (last && !pipe->named)
	{
		fase49_pipe_line(pipe, "DESTROY");
		fase48_pipe_destroyed++;
		kfree(pipe);
	}
}

void pipe_fase49_note_read_sleep(pipe_t *pipe)
{
	fase49_pipe_line(pipe, "READ_SLEEP");
}

void pipe_fase49_note_read_wake(pipe_t *pipe)
{
	fase49_pipe_line(pipe, "READ_WAKE");
	if (pipe)
		ktm_event_emit4(KTM_EVENT_PIPE_WAKE, KTM_SUBSYS_IPC, pipe->pipe_id, 0,
				(uint64_t)(uint32_t)pipe->writers,
				(uint64_t)pipe->count);
}

void pipe_fase49_note_write_wake(pipe_t *pipe)
{
	fase49_pipe_line(pipe, "WRITE_WAKE");
	if (pipe)
		ktm_event_emit4(KTM_EVENT_PIPE_WAKE, KTM_SUBSYS_IPC, pipe->pipe_id, 1,
				(uint64_t)(uint32_t)pipe->readers,
				(uint64_t)pipe->count);
}

void pipe_abort_unopened(pipe_t *pipe)
{
	if (!pipe)
		return;

	fase49_pipe_line(pipe, "DESTROY");
	fase48_pipe_destroyed++;
	kfree(pipe);
}

extern void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
				uint64_t *blocked_readers, uint64_t *blocked_writers);

void pipe_fase49_classify(void)
{
	uint64_t created = 0;
	uint64_t destroyed = 0;
	uint64_t fd_created = 0;
	uint64_t fd_destroyed = 0;
	uint64_t blocked_readers = 0;
	uint64_t blocked_writers = 0;
	const char *cls;

	pipe_fase48_get_stats(&created, &destroyed);
	fase48_fd_get_stats(&fd_created, &fd_destroyed, &blocked_readers,
			    &blocked_writers);

	if (created == destroyed)
		cls = "PIPE_READY";
	else
		cls = "PIPE_REF_LEAK";
	(void)cls;
	(void)fd_created;
	(void)fd_destroyed;
	(void)blocked_readers;
	(void)blocked_writers;
}
