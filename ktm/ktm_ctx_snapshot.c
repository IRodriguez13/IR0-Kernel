/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_ctx_snapshot.c
 * Description: Process context dump for fault classification
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>

void ktm_ctx_snapshot(const process_t *p, const char *reason)
{
	if (!p)
	{
		klog_debug_fmt("KTM", "[KTM][CTX] reason=%s proc=(null)",
			       reason ? reason : "(null)");
		return;
	}

	/*
	 * Split in two records on purpose: klog_*_fmt silently drops arguments
	 * past the eighth, and the single 12-argument call this replaced
	 * printed the tail fields as garbage.
	 */
	klog_debug_fmt("KTM",
		       "[KTM][CTX] reason=%s pid=%x comm=%s state=%llx irq_saved=%llx",
		       reason ? reason : "(null)",
		       (unsigned)(uint32_t)p->task.pid,
		       p->comm[0] ? p->comm : "(none)",
		       (unsigned long long)(uint64_t)p->state,
		       (unsigned long long)(uint64_t)p->irq_frame_saved);
	klog_debug_fmt("KTM",
		       "[KTM][CTX] pid=%x rip=%llx rsp=%llx cs=%llx cr3=%llx poll=%llx",
		       (unsigned)(uint32_t)p->task.pid,
		       (unsigned long long)p->task.arch.rip,
		       (unsigned long long)p->task.arch.rsp,
		       (unsigned long long)(uint64_t)p->task.arch.cs,
		       (unsigned long long)process_mm_root(p),
		       (unsigned long long)(uint64_t)(uintptr_t)p->poll_waiter);
}
