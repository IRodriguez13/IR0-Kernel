/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_fork.c
 * Description: ARM64 fork return / TLS (TPIDR_EL0) for portable fork.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/arch_fork.h>
#include <ir0/arch_cpu.h>
#include <ir0/errno.h>

int arch_fork_prepare_parent_return(struct process *parent, pid_t child_pid)
{
	if (!parent)
		return 0;

	if (parent->mode == USER_MODE)
	{
		process_apply_syscall_frame_to_task(&parent->task,
						    &parent->syscall_frame,
						    (uint64_t)(uint32_t)child_pid);
		set_tls(process_tls_get(parent));
	}
	else
		task_set_retval(&parent->task, (uint64_t)(uint32_t)child_pid);

	return 0;
}

int arch_fork_prepare_child_return(struct process *child, struct process *parent)
{
	if (!child)
		return 0;

	if (parent && parent->mode == USER_MODE)
		process_apply_syscall_frame_to_task(&child->task,
						    &parent->syscall_frame, 0);
	else
		task_set_retval(&child->task, 0);

	return 0;
}

int arch_process_set_tls(struct process *proc, uint64_t tls)
{
	if (!proc)
		return -EINVAL;
	process_tls_set(proc, tls);
	if (proc == current_process)
		set_tls(tls);
	return 0;
}
