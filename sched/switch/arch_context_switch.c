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

#include <ir0/arch_switch.h>
#include <ir0/task.h>
#include <ir0/arch_task.h>
#include <ir0/process.h>
#include <ir0/klog.h>
#include <ir0/oops.h>
#include <ir0/vga.h>
#include <ir0/ktm/klog.h>

/*
 * Called from switch_context_x64 when kernel_ret RIP is outside kernel .text.
 */
void switch_report_bad_ret(uint64_t rip, task_t *task)
{
	process_t *p = task ? task_to_process(task) : current_process;
	uint64_t cs = task ? (uint64_t)task_get_cs(task) : 0;
	uint64_t rsp = task ? task_get_sp(task) : 0;

	/*
	 * Must be visible at default serial level: a nested #DF during panic
	 * dump used to erase the only clue and blame dump_stack_trace.
	 */
	klog_notice_fmt("CTX",
			"CLASSIFY KERNEL_RET_BAD_RIP rip=%llx cs=%llx rsp=%llx "
			"task=%llx pid=%x",
			(unsigned long long)rip,
			(unsigned long long)cs,
			(unsigned long long)rsp,
			(unsigned long long)((uint64_t)(uintptr_t)task),
			(unsigned)(p ? (uint32_t)p->task.pid : 0));
	print("[CTX] CLASSIFY KERNEL_RET_BAD_RIP rip=");
	print_hex64(rip);
	print(" cs=");
	print_hex64(cs);
	print(" pid=");
	print_hex((uintptr_t)(p ? (uint32_t)p->task.pid : 0));
	print("\n");

	panic_note_exception_frame(
		0 /* software */, 0, rip, cs, 0, rsp, 0, 0,
		p ? (uint32_t)p->task.pid : 0,
		p ? p->comm : "(none)");

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
