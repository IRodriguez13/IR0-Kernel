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
	if (!out || !resume_rax || !block_sf)
		return;

	*out = *block_sf;
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
