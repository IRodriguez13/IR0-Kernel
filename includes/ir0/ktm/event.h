/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: event.h
 * Description: KTM typed event model (single source of truth for traces).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>
#include <stdint.h>
#include <stddef.h>

enum ktm_event_type
{
	KTM_EVENT_INFO = 1,
	KTM_EVENT_WARN,
	KTM_EVENT_ERROR,

	KTM_EVENT_TEST_BEGIN,
	KTM_EVENT_TEST_END,
	KTM_EVENT_ASSERT_PASS,
	KTM_EVENT_ASSERT_FAIL,

	KTM_EVENT_PROCESS_CREATE,
	KTM_EVENT_PROCESS_EXIT,
	KTM_EVENT_PROCESS_REAP,

	KTM_EVENT_FRAME_ALLOC,
	KTM_EVENT_FRAME_FREE,
	KTM_EVENT_PAGE_FAULT,

	KTM_EVENT_BLOCK,
	KTM_EVENT_WAKE,
	KTM_EVENT_CONTEXT_SWITCH,

	KTM_EVENT_CHECKPOINT,
	KTM_EVENT_FAULT_INJECTED,
	KTM_EVENT_PANIC,
	KTM_EVENT_SUITE_END,

	KTM_EVENT_CASE_BEGIN = 21,
	KTM_EVENT_CASE_END,
	KTM_EVENT_USER_ASSERT,

	/* Pipe / IPC typed events (ipc.pipe_lifecycle). */
	KTM_EVENT_PIPE_CREATE = 30,
	KTM_EVENT_PIPE_EOF,
	KTM_EVENT_PIPE_EPIPE,
	KTM_EVENT_PIPE_WAKE,
	/*
	 * End refcount transitions. EOF and EPIPE only say the count reached
	 * zero; these say who took it there, which is the question when a
	 * reader sees EOF while a writer still holds the fd.
	 */
	KTM_EVENT_PIPE_END_ACQUIRE,
	KTM_EVENT_PIPE_END_CLOSE,
	/*
	 * Transfer outcome, including the -EAGAIN that sends a reader to
	 * sleep. Without it the ring shows a reader woken with bytes buffered
	 * but not what its next read actually returned.
	 */
	KTM_EVENT_PIPE_READ,
	KTM_EVENT_PIPE_WRITE,
	/* Resume gate returned to ring 3 instead of continuing the syscall. */
	KTM_EVENT_CTX_USER_IRET,
	/* Kernel stack headroom record low: a0=bytes left, a1=slot, a2=rsp. */
	KTM_EVENT_STACK_LOW
};

enum ktm_subsystem
{
	KTM_SUBSYS_CORE = 0,
	KTM_SUBSYS_MM,
	KTM_SUBSYS_PROC,
	KTM_SUBSYS_SCHED,
	KTM_SUBSYS_VFS,
	KTM_SUBSYS_IPC,
	KTM_SUBSYS_NET,
	KTM_SUBSYS_TEST
};

typedef struct ktm_event
{
	uint64_t sequence;
	uint64_t timestamp;
	uint32_t cpu;
	int32_t pid;
	uint16_t type;
	uint16_t subsystem;
	uint64_t arg0;
	uint64_t arg1;
	uint64_t arg2;
	uint64_t arg3;
} ktm_event_t;

#if CONFIG_KTM
void ktm_event_emit4(uint16_t type, uint16_t subsystem,
		     uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
#else
static inline void ktm_event_emit4(uint16_t type, uint16_t subsystem,
				   uint64_t arg0, uint64_t arg1,
				   uint64_t arg2, uint64_t arg3)
{
	(void)type;
	(void)subsystem;
	(void)arg0;
	(void)arg1;
	(void)arg2;
	(void)arg3;
}
#endif

/* Legacy string tags → typed INFO events (compat). */
void ktm_event_emit(const char *tag);
void ktm_event_emit_pid(const char *tag, uint32_t pid);

/* Ring consumer for /dev/ktm read + poll. */
int ktm_event_copy_out(ktm_event_t *dst, size_t max_events);
/*
 * Print the last @max_events ring entries (0 = all) without moving the
 * consumer cursor. @subsys_mask is a bitmask of (1u << ktm_subsystem); 0
 * means every subsystem. Filtering matters because scheduler events vastly
 * outnumber the rest. Diagnostic path for failing smokes.
 */
void ktm_event_ring_dump(size_t max_events, uint32_t subsys_mask);
int ktm_event_pending(void);
void ktm_event_ring_reset_cursor(void);
