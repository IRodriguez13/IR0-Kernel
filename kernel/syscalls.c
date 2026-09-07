/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.c
 * Description: Syscall bootstrap glue (dispatch lives in kernel/syscalls/)
 */

#include "syscalls.h"
#include "syscalls/syscall_dispatch.h"
#include <ir0/console.h>
#include <ir0/copy_user.h>
#include <ir0/input_backend.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/clock.h>
#include <ir0/ktm/klog.h>

#define MAX_STDIN_WAITERS 8
static process_t *stdin_waiters[MAX_STDIN_WAITERS];

int64_t syscalls_read_stdio_stdin(void *buf, size_t count)
{
	char kbuf[256];
	size_t max_read = (count < sizeof(kbuf)) ? count : sizeof(kbuf);
	int64_t ret;

	if (!current_process || !buf)
		return -EFAULT;

	ret = ir0_console_read(kbuf, max_read, 0);
	if (ret <= 0)
		return ret;
	if ((size_t)ret > max_read)
	{
		klog_notice_fmt("TTY",
			      "stdin read clamped: ret=%lld max=%zu\n",
			      (long long)ret, max_read);
		ret = (int64_t)max_read;
	}
	if (copy_to_user(buf, kbuf, (size_t)ret) != 0)
		return -EFAULT;
	return ret;
}

void stdin_wake_check(void)
{
	if (!stdin_wake_check_nosched())
		return;
	/*
	 * Never sched_schedule_next() from here: keyboard IRQ calls this while
	 * current may be userspace or idle. Mid-IRQ switch_to saves [rsp] into
	 * prev->task.rip (kernel C / stack junk) → later kernel_ret/#UD into
	 * BSS (desk panic after top/TTY). Defer via resched flags; ISR exit
	 * consumes the request after interrupt return; idle_poll yields too.
	 */
	clock_request_sched_resched();
}

int stdin_wake_check_nosched(void)
{
	int woke_stdin = 0;
	int woke_tty = 0;
	int i;

	if (!input_kbd_has_data() && !ir0_console_input_ready())
		return 0;

	woke_tty = ir0_console_wake_readers();

	for (i = 0; i < MAX_STDIN_WAITERS; i++)
	{
		if (stdin_waiters[i])
		{
			stdin_waiters[i]->state = PROCESS_READY;
			stdin_waiters[i] = NULL;
			woke_stdin = 1;
		}
	}

	return (woke_stdin || woke_tty) ? 1 : 0;
}

void syscalls_init(void)
{
	process_t *real_current;
	process_t *real_list;

	syscall_table_init();
	klog_info("SYSCALL", "syscalls_init: using REAL process management");

	real_current = current_process;
	real_list = get_process_list();

	klog_debug_fmt("SYSCALL", "Real current_process = %x",
		       (unsigned)(uintptr_t)real_current);
	klog_debug_fmt("SYSCALL", "Real process_list = %x",
		       (unsigned)(uintptr_t)real_list);
}
