/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pipe.h
 * Description: Basic IPC pipes implementation
 */

#ifndef _IR0_PIPE_H
#define _IR0_PIPE_H

#include <stdint.h>
#include <stddef.h>

#define PIPE_SIZE 4096

typedef struct pipe
{
	char buffer[PIPE_SIZE];
	size_t read_pos;
	size_t write_pos;
	size_t count;
	uint64_t pipe_id;
	int fd_refs;
	int readers;
	int writers;
	int closed_read;
	int closed_write;
	/*
	 * Named FIFO (mkfifo / runsv supervise): pipe_t outlives FD closes and is
	 * freed only from named_fifo_unlink / slot recycle. Anonymous pipes keep
	 * named==0 and free on last pipe_close_end.
	 */
	int named;
} pipe_t;

pipe_t *pipe_create(void);
int pipe_read(pipe_t *pipe, void *buf, size_t count);
int pipe_write(pipe_t *pipe, const void *buf, size_t count);
void pipe_close_end(pipe_t *pipe, int end);
void pipe_acquire(pipe_t *pipe);
void pipe_acquire_end(pipe_t *pipe, int end);

void pipe_stats_get(uint64_t *created, uint64_t *destroyed);
void pipe_ktm_note_read_sleep(pipe_t *pipe);
void pipe_ktm_note_read_wake(pipe_t *pipe);
void pipe_ktm_note_write_wake(pipe_t *pipe);
void pipe_abort_unopened(pipe_t *pipe);
void pipe_ipc_lifecycle_audit(void);

#endif /* _IR0_PIPE_H */
