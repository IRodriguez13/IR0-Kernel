/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal_irq.h
 * Description: ISA hooks for IRQ-frame signal delivery and handler entry.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/task.h>

struct sigcontext;
struct arch_syscall_frame;
typedef struct arch_syscall_frame arch_syscall_frame_t;

/* Instruction pointer / stack from an ISA-specific sigcontext (rip/pc, rsp/sp). */
uint64_t sigcontext_ip(const struct sigcontext *ctx);
uint64_t sigcontext_sp(const struct sigcontext *ctx);

/*
 * Fill @ctx from a captured syscall frame (musl syscall insn while blocked).
 * @retval is the interrupted syscall return (typically -EINTR).
 */
void signal_fill_sigcontext_from_syscall_frame(struct sigcontext *ctx,
						    const arch_syscall_frame_t *sf,
						    uint64_t retval);

void signal_fill_sigcontext_from_irq_frame(struct sigcontext *ctx,
						const uint64_t *frame);

/*
 * Point the IRQ return path at @handler with SysV args (sig / info / ucontext).
 * @new_rsp must already be computed by portable policy.
 */
void signal_redirect_irq_frame(uint64_t *frame, void *handler, int sig,
				    uint64_t new_rsp, uint64_t info_addr,
				    uint64_t uctx_addr, int sa_siginfo);

/* Read user SP from an IRQ frame (for stack-window policy). */
uint64_t irq_frame_sp(const uint64_t *frame);

/* Async delivery: set IP/SP and first arg for a user signal handler. */
void signal_prepare_task_handler(task_t *t, void *handler, int sig,
				      uint64_t frame_sp);
