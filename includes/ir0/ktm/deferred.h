/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: deferred.h
 * Description: Observation of paths too hot to emit KTM events from
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>

/*
 * Some paths cannot afford ktm_event_emit4. The context switch is the case
 * that forced this: emitting from the resume gate (interrupts off, mid CR3
 * change, kernel stack in flux) took smoke-pipeline-stress from one failure
 * in six to two in four and introduced SIGSEGVs that were not there before —
 * the instrumentation was changing the very ordering it was meant to observe.
 *
 * Recording is therefore split from emitting. ktm_deferred_record does a
 * handful of plain stores into a fixed array: no calls, no locks, no
 * formatting, no allocation. ktm_deferred_flush turns those rows into real
 * events later, from a context that can afford it (the dump ioctl).
 *
 * Single CPU, so the array needs no locking; the writer runs with interrupts
 * off and the flusher runs in process context.
 *
 * It is a ring that keeps the newest rows. The flush is triggered by a
 * failure, so the rows adjacent to it are the ones worth having; a
 * fill-once buffer would instead be full of boot-time history, and a
 * high-frequency producer like demand paging would crowd out the rare
 * rows entirely.
 */

#define KTM_DEFERRED_CAP 256

/* Kinds of deferred observation. Extend rather than overloading @a0..@a2. */
enum ktm_deferred_kind
{
	KTM_DEFERRED_RESUME_GATE = 1,
	KTM_DEFERRED_PAGE_FAULT = 2,
	KTM_DEFERRED_STACK_LOW = 3
};

void ktm_deferred_record(uint32_t kind, uint32_t pid, uint64_t a0, uint64_t a1,
			 uint64_t a2);
void ktm_deferred_flush(void);
