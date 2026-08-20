/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exit.c
 * Description: process_exit (to zombie) and process_destroy (reaper teardown policy).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/futex.h>
#include <ir0/copy_user.h>
#include <ir0/ktm/checkpoint.h>

#if CONFIG_ENABLE_NETWORKING
void tcp_wire_on_process_exit(uint32_t pid);
#endif

/*
 * Teardown ownership policy
 * -------------------------
 * process_exit():
 *   - reparent children, release FDs, mark PROCESS_ZOMBIE
 *   - does NOT free page tables, kernel stack, or process_t
 * process_destroy() (reaper only):
 *   - release FDs again (idempotent clears), unmap user pages if owns_pml4,
 *     reclaim page tables, free mmap_list, saved_context, kernel stack, PML4
 *   - caller frees process_t after remove-from-list
 */
#if FASE40_D_AUDIT
void fase40_d_audit_reap_line(const char *stage, process_t *child,
				     pid_t parent_pid, int removed,
				     const char *tag)
{
	if (!child)
		return;

}

void fase40_d_audit_destroy_done(process_t *p,
					const process_reclaim_stats_t *stats,
					uint64_t orphan_frames)
{
	size_t used_frames = 0;

	if (!p || !stats)
		return;

	pmm_stats(NULL, &used_frames, NULL);
}
#endif

__attribute__((noreturn)) void process_exit(int code)
{
	process_t *dying = current_process;
	process_t *parent;
	size_t total_frames = 0;
	size_t used_frames = 0;
	uint64_t vmas = 0;

	if (!dying)
	{
		for (;;)
			cpu_idle();
	}
	process_fase50_trace_proc("process_exit-entry", dying);
	dying->irq_frame_saved = 0;
	dying->signal_defer_catchable = 0;
#if CONFIG_ENABLE_NETWORKING
	tcp_wire_on_process_exit((uint32_t)dying->task.pid);
#endif
	process_exit_robust_list(dying);
	if (IR0_DEBUG_WAIT)
		klog_debug("WAIT", "CLASSIFY ZOMBIE_IRQ_SAVED_CLEARED");

	/*
	 * CLONE_CHILD_CLEARTID / set_tid_address: zero the tid word and wake
	 * joiners (musl pthread_join futex).
	 */
	if (dying->set_tid_ptr)
	{
		int zero = 0;
		int *tidptr = dying->set_tid_ptr;

		dying->set_tid_ptr = NULL;
		if (process_validate_userspace_buffer(tidptr, sizeof(zero)) == 0)
		{
			(void)copy_to_user(tidptr, &zero, sizeof(zero));
			(void)ir0_futex_wake(tidptr, 1);
		}
	}

	/* Before becoming a zombie:
	 * 1. Reparent all children to init (PID 1) to avoid orphaned processes
	 * 2. Clean up any zombie children we were waiting for
	 */
	process_reap_zombies(dying);
	process_reparent_children(dying);

	process_release_fds(dying, "EXIT_CLOSE");

#if IR0_DEBUG_PROC
	process_fase46_proc_log(dying, (int64_t)(uint32_t)code, "EXIT");
	process_fase44_list_checkpoint("exit-before");
	{
		fase_proc_audit_t *fa = fase_audit_get(dying, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_EXITING;
	}
	fase_audit_trace_pid(dying->task.pid, "EXIT");
	fase_audit_ref_emit(dying, "exit");
	process_fase43_proc_audit("exit-before");
#endif

	/* Mark as zombie */
	process_mark_zombie(dying);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);
	if (dying->exit_signal == 0)
		dying->exit_code = code;
	else
		dying->exit_code = 0;
#if IR0_DEBUG_PROC
	if (dying->exit_signal > 0)
	{
		klog_debug_fmt("SIGNAL", "[SIGTERM_AUDIT] process_exit pid=%x exit_signal=%x wait_status=%x", (unsigned)((uint32_t)dying->task.pid), (unsigned)((uint32_t)dying->exit_signal), (unsigned)((uint32_t)process_child_wait_status_word(dying)));
	}
#endif
#if IR0_DEBUG_PROC
	{
		fase_proc_audit_t *fa = fase_audit_get(dying, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_ZOMBIE;
	}
	fase_audit_trace_pid(dying->task.pid, "ZOMBIE");
#endif
	fase_audit_note_proc_exited();
	fase_audit_note_proc_zombie();
#if IR0_DEBUG_PROC
	paging_ir0_mm_checkpoint("exit-before", (int32_t)dying->task.pid);
#endif
	for (struct mmap_region *r = process_mmap_list(dying); r; r = r->next)
		vmas++;
	pmm_stats(&total_frames, &used_frames, NULL);
	if (IR0_DEBUG_PROC)
	{
	}

	process_fase43_proc_audit("exit-after");
#if IR0_DEBUG_PROC
	if (dying->task.pid == 1)
	{
		process_fase44_drain_zombie_children(1);
		process_fase43_live_proc_dump();
		process_fase44_live_summary("init-exit");
	}
	process_fase44_list_checkpoint("exit-after");
#endif

	/* Send SIGCHLD to parent process if it exists */
	if (dying->ppid > 0)
	{
		int parent_state_before = -1;

		parent = process_find_by_pid(dying->ppid);
		if (parent)
			parent_state_before = parent->state;
		if (parent && parent->state != PROCESS_ZOMBIE)
		{
			send_signal(parent->task.pid, SIGCHLD);
			if (parent->state == PROCESS_BLOCKED ||
			    parent->wait_blocked)
				process_wait_wake_blocked_parent(parent, dying);
		}
		else if (!parent || parent->state == PROCESS_ZOMBIE)
		{
			/* Parent is dead or zombie - reparent to init and send SIGCHLD to init */
			dying->ppid = 1;
			parent = process_find_by_pid(1);
			if (parent)
			{
				if (parent_state_before == -1)
					parent_state_before = parent->state;
				send_signal(parent->task.pid, SIGCHLD);
				if (parent->state == PROCESS_BLOCKED)
					process_wait_wake_blocked_parent(parent, dying);
			}
		}
		wait_exit_audit_process_exit(dying, parent, parent_state_before);
	}
	else
	{
		wait_exit_audit_process_exit(dying, NULL, -1);
	}

	/* Remove process from scheduler - it should no longer be scheduled.
	 * The process structure remains in memory as a zombie until reaped
	 * by the parent (via wait()), but it will not consume CPU time.
	 */
	sched_remove_process(dying);
	process_fase50_trace_proc("process_exit-before-schedule", dying);

	/*
	 * kmain keeps the kernel idle task off the RR queue while PID 1 runs;
	 * enqueue it again when a user process exits so sched has a fallback.
	 */
	{
		process_t *p;

		for (p = process_list; p; p = p->next)
		{
			if (p->mode == KERNEL_MODE && p->state != PROCESS_ZOMBIE &&
			    strncmp(p->comm, "idle", sizeof(p->comm)) == 0)
			{
				sched_add_process(p);
				break;
			}
		}
	}

	/* Switch to another process - this will never return to this code.
	 * The zombie process remains in memory with its exit code for the
	 * parent to retrieve via wait().
	 */
	sched_schedule_next();

	/* No runnable task: halt forever (must not sysret to exited user context). */
	for (;;)
		cpu_idle();
}


/*
 * process_destroy - Release per-process resources before freeing a zombie struct.
 * Closes VFS and pipe handles, clears the FD table, and tears down user mappings
 * in this process's page directory (not necessarily the active CR3).
 */
void process_destroy(process_t *p)
{
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;

	ir0_console_purge_waiters_for_process(p);
	ir0_clock_wait_disarm(p);

	process_fase46_proc_log(p, -1, "DESTROY");
	fase_audit_ref_emit(p, "destroy");
	fase_audit_note_proc_destroyed();
	process_fase43_proc_audit("destroy-before");


	process_release_fds(p, "DESTROY");

	if (p->files)
	{
		files_put(p->files);
		p->files = NULL;
	}

	/*
	 * Address space teardown via mm refcount. Capture kernel-mode
	 * kmalloc stack cursor before mm_put frees the mm_struct.
	 */
	{
		uint64_t kstack = 0;

		if (p->mode == KERNEL_MODE)
			kstack = process_stack_start(p);

		if (p->mm)
		{
			mm_put(p->mm);
			p->mm = NULL;
			process_fase43_note_mm_destroyed();
		}

		if (p->mode == KERNEL_MODE && kstack &&
		    kstack != INIT_DEBUG_STACK_BASE)
			kfree((void *)(uintptr_t)kstack);
	}

	if (p->saved_context)
	{
		kfree(p->saved_context);
		p->saved_context = NULL;
	}

	/* Release the private kernel stack (zombie is off-CPU; not in use). */
	process_kernel_stack_free(p);

	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
#if IR0_DEBUG_PMM
#endif
	paging_ir0_mm_checkpoint("destroy-after", (int32_t)p->task.pid);
	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_DESTROYED;
	}
	fase_audit_unbind(p);
	process_fase43_proc_audit("destroy-after");
}

