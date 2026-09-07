/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal_syscall_resume.h
 * Description: Linux-like resume after signal interrupted kernel_syscall_sleep.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/errno.h>
#include <ir0/syscall_frame.h>

/* A captured negative read descriptor is never a valid restart argument. */
static inline int signal_syscall_read_fd_suspicious(uint64_t val)
{
	return (int64_t)val < 0;
}

/* read(2) arg1 is the userspace buffer pointer. */
static inline int signal_syscall_user_ptr_suspicious(uint64_t val)
{
	return (int64_t)val < 0 || val < 0x1000ul;
}

static inline int signal_syscall_arg_suspicious(uint64_t val)
{
	return signal_syscall_read_fd_suspicious(val);
}

/*
 * read(2) block snapshot must not carry wait4(-1) into SA_RESTART resume
 * (observed #PF cr2=0x3f in runit login after shell logout).
 */
static inline void signal_blocked_syscall_frame_sanitize(arch_syscall_frame_t *sf,
							 uint32_t block_nr)
{
	if (!sf)
		return;

	if (block_nr != 0u)
		return;

	if (signal_syscall_read_fd_suspicious(sf->rdi))
		sf->rdi = 0;
}

static inline uint64_t signal_repair_sigcontext_syscall_arg(
	uint64_t ctx_val,
	uint64_t snap_val,
	uint64_t task_val)
{
	if (!signal_syscall_user_ptr_suspicious(ctx_val))
		return ctx_val;
	if (!signal_syscall_user_ptr_suspicious(snap_val))
		return snap_val;
	if (!signal_syscall_user_ptr_suspicious(task_val))
		return task_val;
	return ctx_val;
}

static inline uint64_t signal_repair_sigcontext_read_fd(
	uint64_t ctx_val,
	uint64_t snap_val,
	uint64_t task_val)
{
	if (!signal_syscall_read_fd_suspicious(ctx_val))
		return ctx_val;
	if (!signal_syscall_read_fd_suspicious(snap_val))
		return snap_val;
	if (!signal_syscall_read_fd_suspicious(task_val))
		return task_val;
	return ctx_val;
}

/*
 * Resume userspace after rt_sigreturn when the signal interrupted a blocking
 * syscall (TTY read, pipe, poll). Linux keeps the saved syscall-entry GPRs on
 * -EINTR; only SA_RESTART restarts at the syscall insn with __NR in rax.
 */
static inline void signal_resume_blocked_syscall_frame(
	arch_syscall_frame_t *out,
	uint64_t *resume_rax,
	const arch_syscall_frame_t *block_sf,
	int restart,
	uint32_t block_nr)
{
	arch_syscall_frame_t snap;

	if (!out || !resume_rax || !block_sf)
		return;

	snap = *block_sf;
	signal_blocked_syscall_frame_sanitize(&snap, block_nr);
	*out = snap;
	if (restart)
	{
		*resume_rax = (uint64_t)block_nr;
		syscall_frame_arm_restart(out);
	}
	else
		*resume_rax = (uint64_t)(int64_t)(-EINTR);
}

static inline int signal_blocked_syscall_is_console_read(uint32_t block_nr,
							 int64_t arg0_fd)
{
	return block_nr == 0u && arg0_fd == 0;
}
