/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pipe.c
 * Description: IPC pipes — refcount, EOF/EPIPE, KTM lifecycle events
 */

#include <ir0/pipe.h>
#include <ir0/spinlock.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/ktm/event.h>
#include <ir0/ktm/fault.h>
#include <string.h>
#include <config.h>

static uint64_t pipe_stats_created;
static uint64_t pipe_stats_destroyed;
static uint64_t pipe_next_id = 1;

void pipe_stats_get(uint64_t *created, uint64_t *destroyed)
{
	if (created)
		*created = pipe_stats_created;
	if (destroyed)
		*destroyed = pipe_stats_destroyed;
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
	{
		ir0_spinlock_t lock;

		ir0_spin_lock(&lock);
		pipe->pipe_id = pipe_next_id++;
		ir0_spin_unlock(&lock);
	}
	pipe_stats_created++;
	ktm_event_emit4(KTM_EVENT_PIPE_CREATE, KTM_SUBSYS_IPC, pipe->pipe_id, 0, 0, 0);
	return pipe;
}

/*
 * Register one open fd slot on @end (0 read, 1 write).
 * Called from pipe2 install, dup/dup2, and fork inheritance.
 */
void pipe_acquire_end(pipe_t *pipe, int end)
{
	ir0_spinlock_t lock;

	if (!pipe)
		return;

	if (end != 0 && end != 1)
		return;

	ir0_spin_lock(&lock);
	pipe->fd_refs++;
	if (end == 0)
	{
		pipe->readers++;
		if (pipe->named)
			pipe->closed_read = 0;
	}
	else
	{
		pipe->writers++;
		if (pipe->named)
			pipe->closed_write = 0;
	}
	ir0_spin_unlock(&lock);

	ktm_event_emit4(KTM_EVENT_PIPE_END_ACQUIRE, KTM_SUBSYS_IPC,
			pipe->pipe_id, (uint64_t)(uint32_t)end,
			(uint64_t)(uint32_t)pipe->readers,
			(uint64_t)(uint32_t)pipe->writers);
}

void pipe_acquire(pipe_t *pipe)
{
	ir0_spinlock_t lock;

	if (!pipe)
		return;

	ir0_spin_lock(&lock);
	pipe->fd_refs++;
	ir0_spin_unlock(&lock);
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
	 * with the pipe mutex; IR0 IPC uses ir0_spinlock (UP: irq-save).
	 * Bounded by PIPE_SIZE (4 KiB), so the hold time stays comparable.
	 */
	ir0_spinlock_t lock;

	ir0_spin_lock(&lock);
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
		ir0_spin_unlock(&lock);

		if ((int)writers_snapshot <= 0)
		{
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
	ir0_spin_unlock(&lock);

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
	ir0_spinlock_t lock;

	ir0_spin_lock(&lock);
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
		ir0_spin_unlock(&lock);

		ktm_event_emit4(KTM_EVENT_PIPE_EPIPE, KTM_SUBSYS_IPC, pipe->pipe_id,
				(uint64_t)readers_snapshot,
				(uint64_t)writers_snapshot,
				(uint64_t)count_snapshot);
		return -EPIPE;
	}

	space = PIPE_SIZE - pipe->count;
	if (space == 0)
	{
		ir0_spin_unlock(&lock);
		return -EAGAIN;
	}

	/*
	 * Linux pipe(7): writes of ≤ PIPE_BUF are atomic. If the whole
	 * request does not fit, do not emit a short write — return -EAGAIN
	 * so blocking callers wait and O_NONBLOCK surfaces EAGAIN.
	 * Requests larger than PIPE_BUF may partial-write (Linux).
	 */
	if (count <= (size_t)PIPE_BUF && space < count)
	{
		ir0_spin_unlock(&lock);
		return -EAGAIN;
	}

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
	ir0_spin_unlock(&lock);

	ktm_event_emit4(KTM_EVENT_PIPE_WRITE, KTM_SUBSYS_IPC, pipe->pipe_id,
			(uint64_t)bytes_written,
			(uint64_t)readers_snapshot,
			(uint64_t)count_snapshot);

	return (int)bytes_written;
}

void pipe_close_end(pipe_t *pipe, int end)
{
	int last = 0;
	ir0_spinlock_t lock;

	if (!pipe)
		return;

	if (end != 0 && end != 1)
		return;

	ir0_spin_lock(&lock);
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

	if (pipe->fd_refs > 0)
	{
		pipe->fd_refs--;
		last = (pipe->fd_refs == 0);
	}
	ir0_spin_unlock(&lock);

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
		pipe_stats_destroyed++;
		kfree(pipe);
	}
}

void pipe_ktm_note_read_sleep(pipe_t *pipe)
{
	if (!pipe)
		return;

	ktm_event_emit4(KTM_EVENT_BLOCK, KTM_SUBSYS_IPC, pipe->pipe_id, 0,
			(uint64_t)(uint32_t)pipe->writers, pipe->count);
}

void pipe_ktm_note_read_wake(pipe_t *pipe)
{
	if (!pipe)
		return;

	ktm_event_emit4(KTM_EVENT_PIPE_WAKE, KTM_SUBSYS_IPC, pipe->pipe_id, 0,
			(uint64_t)(uint32_t)pipe->writers,
			(uint64_t)pipe->count);
}

void pipe_ktm_note_write_wake(pipe_t *pipe)
{
	if (!pipe)
		return;

	ktm_event_emit4(KTM_EVENT_PIPE_WAKE, KTM_SUBSYS_IPC, pipe->pipe_id, 1,
			(uint64_t)(uint32_t)pipe->readers,
			(uint64_t)pipe->count);
}

void pipe_abort_unopened(pipe_t *pipe)
{
	if (!pipe)
		return;

	pipe_stats_destroyed++;
	kfree(pipe);
}

extern void fd_slot_stats_get(uint64_t *created, uint64_t *destroyed,
			      uint64_t *blocked_readers, uint64_t *blocked_writers);

void pipe_ipc_lifecycle_audit(void)
{
	uint64_t created = 0;
	uint64_t destroyed = 0;
	uint64_t fd_created = 0;
	uint64_t fd_destroyed = 0;
	uint64_t blocked_readers = 0;
	uint64_t blocked_writers = 0;
	int pipe_ok;
	int fd_ok;

	pipe_stats_get(&created, &destroyed);
	fd_slot_stats_get(&fd_created, &fd_destroyed, &blocked_readers,
			  &blocked_writers);

	pipe_ok = (created == destroyed);
	fd_ok = (fd_created == fd_destroyed);

	ktm_event_emit4(KTM_EVENT_CHECKPOINT, KTM_SUBSYS_IPC,
			pipe_ok ? 1ULL : 0ULL,
			fd_ok ? 1ULL : 0ULL,
			created, destroyed);
	(void)blocked_readers;
	(void)blocked_writers;
}
