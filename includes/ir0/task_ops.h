/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: task_ops.h
 * Description: ISA-specific bulk task context operations and sigcontext transfer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/signals.h>
#include <ir0/task.h>
#include <ir0/syscall_frame.h>

/*
 * Same storage as process_t.syscall_frame. Arch backends may open ISA fields;
 * portable code uses arch_syscall_frame_* / process_syscall_*.
 */
typedef arch_syscall_frame_t arch_task_syscall_frame_t;

void task_apply_syscall_frame(task_t *task,
				   const arch_task_syscall_frame_t *sf,
				   uint64_t rax);
void task_sync_syscall_soft_mirror(task_t *task,
					const arch_task_syscall_frame_t *sf);
void task_save_irq_user_frame(task_t *task, const uint64_t *iretq_frame);
void task_apply_kernel_segments(task_t *task);
void task_apply_user_segments(task_t *task);
uint64_t *task_retval_slot_addr(task_t *task);

void task_load_sigcontext(task_t *t, const struct sigcontext *ctx);
void task_store_sigcontext(struct sigcontext *ctx, const task_t *t);
