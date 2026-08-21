/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_syscall_frame.h
 * Description: ISA syscall-frame type + capture/restore hooks for portable core.
 *
 * Storage: arch_syscall_frame_t is selected from arch_syscall_frame_{x86_64,arm64}.h.
 * Semantic accessors live with the layout. Decode/fill stays in arch backends.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/task.h>

#if defined(ARCH_ARM64) || defined(__aarch64__)
#include <ir0/arch_syscall_frame_arm64.h>
#else
#include <ir0/arch_syscall_frame_x86_64.h>
#endif

/* Legacy alias — same type; do not invent a second frame layout. */
typedef arch_syscall_frame_t syscall_user_frame_t;

struct process;

/*
 * Populate process->syscall_frame from the architecture syscall entry stack.
 * No-op on ISAs without a wired entry path.
 */
void arch_process_capture_syscall_frame_at_entry(struct process *p,
						 uint64_t *frame_base,
						 uint64_t rip_hw);

/*
 * Write process->syscall_frame back onto the syscall exit stack (RESTORE_ALL).
 */
void arch_process_syscall_restore_exit_regs(struct process *p,
					    uint64_t *stack_r9_slot);

/*
 * Save user GPRs from an IRQ stub stack into current_process (preempt path).
 * @gpr_stack: pointer to the saved-RAX slot (ISA-defined layout).
 */
void arch_process_save_user_context_from_irq(uint64_t *gpr_stack);

/* 1 if @iretq_frame is a userspace interrupt frame. */
int arch_irq_frame_is_user(const uint64_t *iretq_frame);

/*
 * Build a kernel task stack frame for create_task() / idle-style tasks.
 * Returns 0 or -errno.
 */
int arch_task_setup_kernel_stack(task_t *task, void *stack_base,
				 size_t stack_size, void (*entry)(void *));
