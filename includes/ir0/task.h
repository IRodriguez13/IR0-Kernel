/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: task.h
 * Description: Per-task CPU context (canonical facade; sched/ includes this)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

#if defined(ARCH_ARM64) || defined(__aarch64__)
#include <ir0/arch_task_context_arm64.h>
#else
#include <ir0/arch_task_context_x86_64.h>
#endif

/*
 * Scheduler-facing task state (canonical). Numeric values for READY/RUNNING/
 * SLEEPING/TERMINATED stay stable; STOPPED is additive.
 * Compatibility macros TASK_* keep call sites compiling during migration.
 */
typedef enum
{
	TASK_SCHED_RUNNABLE = 0,
	TASK_SCHED_RUNNING = 1,
	TASK_SCHED_SLEEPING = 2,
	TASK_SCHED_TERMINATED = 3,
	TASK_SCHED_STOPPED = 4
} task_sched_state_t;

typedef task_sched_state_t task_state_t;

#define TASK_READY TASK_SCHED_RUNNABLE
#define TASK_RUNNING TASK_SCHED_RUNNING
#define TASK_BLOCKED TASK_SCHED_SLEEPING
#define TASK_TERMINATED TASK_SCHED_TERMINATED

/*
 * Proceso / hilo del kernel: registros guardados y metadatos mínimos.
 * La política de planificación usa priority y la lista next.
 */
typedef struct task
{
	arch_task_context_t arch;

	pid_t pid;
	uint8_t priority;   /* 0-255, mayor = más prioridad */
	task_state_t state;
	struct task *next;  /* Lista de tareas */

	void *stack_base;
	uint32_t stack_size;
	void (*entry)(void *);
	void *entry_arg;

	uint32_t context_switches;
	uint64_t total_runtime;
	uint64_t last_run_time;

} task_t;

TASK_CONTEXT_ASSERT_LAYOUT(task_t);

#include <ir0/arch_task.h>

#define MAX_TASKS 256
#define DEFAULT_STACK_SIZE (4 * 1024)

#define TASK_INIT(name, prio)              \
	{                                      \
		.pid = 0,                          \
		.priority = (prio),                \
		.state = TASK_READY,               \
		.context_switches = 0,             \
		.total_runtime = 0,                \
		.next = NULL,                      \
	}

#define task_is_ready(t) ((t)->state == TASK_READY)
#define task_is_running(t) ((t)->state == TASK_RUNNING)
#define task_is_blocked(t) ((t)->state == TASK_BLOCKED)
#define task_is_terminated(t) ((t)->state == TASK_TERMINATED)
