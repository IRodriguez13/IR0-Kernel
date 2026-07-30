/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_context_switch.c
 * Description: Portable switch_to() dispatcher — ISA body in arch_switch.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/context.h>
#include <ir0/arch_switch.h>
#include <ir0/task.h>
#include <ir0/arch_task.h>
#include <ir0/process.h>
#include <ir0/klog.h>
#include <ir0/oops.h>
#include <ir0/paging.h>

/*
 * Called from switch_context_x64 when kernel_ret RIP is outside kernel .text.
 */
void arch_report_bad_kernel_ret_rip(uint64_t rip, task_t *task)
{
	process_t *p = task ? task_to_process(task) : current_process;

	klog_debug_fmt("CTX", "CLASSIFY KERNEL_RET_BAD_RIP rip=%llx task=%llx proc=%llx current=%llx", (unsigned long long)(rip), (unsigned long long)((uint64_t)(uintptr_t)task), (unsigned long long)((uint64_t)(uintptr_t)p), (unsigned long long)((uint64_t)(uintptr_t)current_process));
	if (p)
	{
		klog_debug_fmt("KERN", " pid=%x cs=%llx saved_arg0=%llx wait_blocked=%llx wait_target=%llx irq_frame=%llx sf_rip=%llx", (unsigned)((uint32_t)p->task.pid), (unsigned long long)((uint64_t)task_get_cs(&p->task)), (unsigned long long)(task_get_arg0(&p->task)), (unsigned long long)((uint64_t)p->wait_blocked), (unsigned long long)((uint64_t)(int64_t)p->wait_target_pid), (unsigned long long)((uint64_t)p->irq_frame_saved), (unsigned long long)(process_syscall_ip(p)));
	}
	/*
	 * Shared from switch_x64 .bad_ret_rip and .bad_user_iret_frame.
	 * Message names the Class B / kernel_ret failure mode; user-iret
	 * bound failures hit the same helper (see switch_x64.asm).
	 */
	panicex("kernel_ret RIP not in .text", PANIC_KERNEL_BUG, __FILE__, __LINE__,
		__func__);
}

void switch_to(task_t *prev, task_t *next)
{
	arch_switch_to(prev, next);
}

uint64_t arch_get_current_page_directory(void)
{
	return get_current_page_directory();
}
