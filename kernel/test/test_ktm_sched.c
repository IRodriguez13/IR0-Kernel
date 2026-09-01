/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_ktm_sched.c
 * Description: KTM — scheduler / blocked-syscall contract tests (in-kernel)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include <ir0/ktm.h>
#include <ir0/process.h>
#include <ir0/syscalls_kernel.h>
#include <errno.h>
#include <ir0/signals.h>
#include <string.h>

void ktest_process_reset_blocked_syscall_state(void)
{
	process_t p;

	KTEST_BEGIN("process_reset_blocked_syscall_state");

	memset(&p, 0, sizeof(p));
	p.irq_frame_saved = 1;
	p.poll_resume_via_arch = 1;
	p.syscall_resume_rax = 99;
	p.wait_status_ptr = (int *)(uintptr_t)0x1000;
	p.poll_waiter = (void *)(uintptr_t)0x2000;

	process_reset_blocked_syscall_state(&p);

	KASSERT_EQ(p.irq_frame_saved, 0);
	KASSERT_EQ(p.poll_resume_via_arch, 0);
	KASSERT_EQ(p.syscall_resume_rax, 0);
	KASSERT(p.wait_status_ptr == NULL);
	KASSERT(p.poll_waiter == NULL);

	KTEST_END();
}

/*
 * pause(2) returning -EINTR is deliberately not covered in-kernel. Posting a
 * pending signal and calling sys_pause() from the ktest process does reach the
 * interrupt path, but that path then runs handle_signals(), and every signal
 * that satisfies the predicate without a userspace handler has a default
 * action of terminate: the runner process dies mid-suite instead of the call
 * returning. Covering this needs a userspace smoke that installs a handler
 * first, the way BusyBox does.
 */