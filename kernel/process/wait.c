/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: wait.c
 * Description: wait4/reap/reparent and wait-exit audit; zombie until process_destroy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/ktm/checkpoint.h>

void wait_exit_audit_classify_user_frame(const char *tag, process_t *p)
{
#if !IR0_DEBUG_WAIT
	(void)tag;
	(void)p;
	return;
#else
	uint64_t rip;
	uint64_t rsp;
	uint16_t cs;
	uint16_t ss;

	if (!p)
		return;

	rip = task_get_ip(&p->task);
	rsp = task_get_sp(&p->task);
	cs = task_get_cs(&p->task);
	ss = task_get_ss(&p->task);

	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][FRAME] tag=%s pid=%x comm=%s rip=%llx rsp=%llx cs=%llx ss=%llx rflags=%llx rax=%llx cr3=%llx irq_saved=%llx", tag ? tag : "(null)", (unsigned)((uint32_t)p->task.pid), p->comm, (unsigned long long)(rip), (unsigned long long)(rsp), (unsigned long long)((uint64_t)cs), (unsigned long long)((uint64_t)ss), (unsigned long long)(task_get_flags(&p->task)), (unsigned long long)(task_get_retval(&p->task)), (unsigned long long)(process_mm_root(p)), (unsigned long long)((uint64_t)p->irq_frame_saved));

	if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
	{
		klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RIP");
	}
	if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
	{
		klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RSP");
	}
	if (!task_cs_is_user(&p->task) || (ss & 3u) != 3u)
	{
		klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_CS_SS");
	}
#endif
}

void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
                                         int parent_state_before)
{
#if !IR0_DEBUG_WAIT
	(void)dying;
	(void)parent;
	(void)parent_state_before;
	return;
#else
	klog_debug_fmt("WAIT",
		       "[WAIT_EXIT_AUDIT][process_exit] child_pid=%x "
		       "child_state_before=RUNNING child_state_after=ZOMBIE "
		       "parent_pid=%x parent_state_before=%llx parent_state_after=%llx "
		       "parent_woken=%llx",
		       (unsigned)((uint32_t)dying->task.pid),
		       (unsigned)(parent ? (uint32_t)parent->task.pid : 0),
		       (unsigned long long)((uint64_t)(unsigned int)parent_state_before),
		       (unsigned long long)(parent ? (uint64_t)(unsigned int)parent->state : 0),
		       (unsigned long long)((uint64_t)(parent &&
							parent_state_before == PROCESS_BLOCKED &&
							parent->state == PROCESS_READY
								? 1
								: 0)));

	klog_debug("WAIT",
		   "[WAIT_EXIT_AUDIT][process_exit] child_frees address_space=0 "
		   "page_directory=0 task_struct=0 kernel_stack=0 (zombie until reap)");
	klog_debug("WAIT",
		   "[WAIT_EXIT_AUDIT][process_exit] child_closes fd_table=1 cwd=0 vfs_mm=0");

	if (parent)
	{
		klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_exit] parent_mm=%llx parent_cr3=%llx parent_files=%llx parent_irq_saved=%llx", (unsigned long long)((uint64_t)(uintptr_t)process_pgd(parent)), (unsigned long long)(process_mm_root(parent)), (unsigned long long)((uint64_t)(uintptr_t)parent->files), (unsigned long long)((uint64_t)parent->irq_frame_saved));
		wait_exit_audit_classify_user_frame("parent-at-child-exit", parent);
	}

	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_exit] child_irq_saved=%llx", (unsigned long long)((uint64_t)dying->irq_frame_saved));
	if (dying->irq_frame_saved)
	{
		klog_debug("WAIT",
			   "CLASSIFY EXIT_FREED_PARENT_RESOURCE "
			   "note=child_irq_saved_stale_may_misroute_resume");
	}
#endif
}

void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)wait_pid;
	(void)status;
	return;
#else
	if (!current_process)
		return;

	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_wait] action=block parent_pid=%x wait_pid=%x status_ptr=%llx expected_rax_after_wake=child_pid\n", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)wait_pid), (unsigned long long)((uint64_t)(uintptr_t)status));
	wait_exit_audit_classify_user_frame("parent-before-wait-sleep", current_process);
	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_wait] syscall_frame rip=%llx rsp=%llx", (unsigned long long)(process_syscall_ip(current_process)), (unsigned long long)(process_syscall_sp(current_process)));
#endif
}

void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)reaped_pid;
	(void)status_val;
	(void)status;
	return;
#else
	if (!current_process)
		return;

	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_wait] action=reap parent_pid=%x reaped_pid=%x status_val=%llx status_ptr=%llx return_rax=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)reaped_pid), (unsigned long long)((uint64_t)(unsigned int)status_val), (unsigned long long)((uint64_t)(uintptr_t)status), (unsigned long long)((uint64_t)(unsigned int)reaped_pid));
	wait_exit_audit_classify_user_frame("parent-after-reap", current_process);
#endif
}

process_t *process_find_by_pid(pid_t pid)
{
	process_t *proc;
	uint64_t irq_flags = process_irq_save();

	proc = process_list;
	
	while (proc)
	{
		if (proc->task.pid == pid)
		{
			process_irq_restore(irq_flags);
			return proc;
		}
		proc = proc->next;
	}
	
	process_irq_restore(irq_flags);
	return NULL; /* Not found */
}

int process_remove_from_list(process_t *target)
{
	process_t *scan;
	process_t *prev;
	uint64_t irq_flags;

	if (!target)
		return -EINVAL;

	irq_flags = process_irq_save();
	prev = NULL;
	scan = process_list;

	while (scan)
	{
		if (scan == target)
		{
			if (prev)
				prev->next = scan->next;
			else
				process_list = scan->next;
			scan->next = NULL;
			process_irq_restore(irq_flags);
			return 0;
		}
		prev = scan;
		scan = scan->next;
	}

	process_irq_restore(irq_flags);
	return -ENOENT;
}

/**
 * process_reparent_children - Reparent all children to init (PID 1)
 * @dying_parent: Process that is about to exit
 *
 * When a parent process dies, all its children become orphans.
 * This function reparents them to init (PID 1) so they don't become zombies.
 */
void process_reparent_children(process_t *dying_parent)
{
	process_t *child;
	process_t *init;
	int has_child = 0;

	if (!dying_parent)
		return;

	for (child = process_list; child; child = child->next)
	{
		if (child->ppid == dying_parent->task.pid && child != dying_parent)
		{
			has_child = 1;
			break;
		}
	}
	if (!has_child)
		return;

	init = process_find_by_pid(1);
	if (!init || init == dying_parent)
	{
		/*
		 * No suitable init (early boot, or PID 1 itself exiting).
		 * Detach orphans rather than CRITICAL-spam; KTM can assert later.
		 */
		for (child = process_list; child; child = child->next)
		{
			if (child->ppid == dying_parent->task.pid && child != dying_parent)
			{
				child->ppid = 0;
				fase_audit_note_reparent();
			}
		}
		return;
	}

	child = process_list;
	while (child)
	{
		if (child->ppid == dying_parent->task.pid)
		{
			fase_proc_audit_t *fa = fase_audit_get(child, 0);
			uint8_t audit_st = fa ? fa->fase44_audit_state : 0;

			child->ppid = 1;
			fase_audit_note_reparent();
			fase_audit_destroy_audit(child, dying_parent->task.pid,
					     audit_st, audit_st, 0, "reparent");
#if DEBUG_PROCESS
			klog_debug_fmt("KERN", "[PROCESS] Reparented child PID %x to init (PID 1)\n", (unsigned)((uint32_t)child->task.pid));
#endif
		}
		child = child->next;
	}
	process_fase44_list_checkpoint("reparent-after");
}

void process_reap_zombie_child(process_t *child)
{
	int removed;

	if (!child)
		return;

	removed = process_remove_from_list(child);
	KTM_CHECKPOINT(KTM_CP_PROCESS_REAP);
	if (removed != 0)
	{
		return;
	}
	process_destroy(child);
	kfree(child);
}

/**
 * process_reap_zombies - Automatically reap zombie children of a process
 * @parent: Parent process
 *
 * When a process exits, clean up any zombie children that were waiting for it.
 * Also used by init to periodically clean up zombies.
 */
void process_reap_zombies(process_t *parent)
{
	process_t *child;
	process_t *next;
	
	if (!parent)
		return;
	
	child = process_list;
	
	while (child)
	{
		next = child->next;
		
		/* Check if this is a zombie child of the parent */
		if (child->ppid == parent->task.pid && child->state == PROCESS_ZOMBIE)
		{
#if DEBUG_PROCESS
			klog_debug_fmt("KERN", "[PROCESS] Auto-reaping zombie child PID %x", (unsigned)((uint32_t)child->task.pid));
#endif
			fase_audit_note_reap_event();
			process_fase43_proc_audit("reap-zombie");
			fase_audit_reap_zombie(child, parent->task.pid, "reap-zombie");
		}
		
		child = next;
	}
	process_fase44_list_checkpoint("reap-zombie-after");
}

int process_wait_child_matches_blocked_target(const process_t *parent,
					    pid_t child_pid)
{
	pid_t target;
	int any_child;

	if (!parent || child_pid <= 0 || !parent->wait_blocked)
		return 0;

	target = parent->wait_target_pid;
	any_child = (target == (pid_t)-1 || target == 0);
	if (!any_child && child_pid != target)
		return 0;

	return 1;
}

int process_child_wait_status_word(const process_t *child)
{
	if (!child)
		return 0;
	if (child->exit_signal > 0)
		return child->exit_signal & 0x7f;
	return (child->exit_code & 0xff) << 8;
}

void process_wait_wake_blocked_parent(process_t *parent, process_t *child)
{
	int status_val;
	int *status_ptr;
	int copy_ret;

	if (!parent || !child || !parent->wait_blocked)
		return;

	if (!process_wait_child_matches_blocked_target(parent, child->task.pid))
	{
		return;
	}


	if (!parent->irq_frame_saved)
	{
		/*
		 * Blocked via process_arm_kernel_syscall_sleep: keep kernel CS/SS so
		 * switch_context_x64 resumes with kernel_ret into process_wait, not
		 * user iretq with stale task.arch.rax (placeholder 0 at block time).
		 */
		process_set_sched_state(parent, PROCESS_READY);
		sched_add_process(parent);
		sched_promote_process(parent);
		return;
	}

	status_val = process_child_wait_status_word(child);
	status_ptr = parent->wait_status_ptr;
	if (!status_ptr)
		status_ptr = (int *)(uintptr_t)process_syscall_arg(parent, 1);

	copy_ret = -1;
	if (status_ptr && process_pgd(parent) &&
	    process_validate_userspace_buffer(status_ptr, sizeof(int)) == 0)
	{
		copy_ret = copy_to_user_region_in_directory(process_pgd(parent),
							    (uintptr_t)status_ptr,
							    &status_val,
							    sizeof(int));
	}

#if IR0_DEBUG_PROC
	klog_debug_fmt("SIGNAL", "[SIGTERM_AUDIT] wait_wake parent=%x child=%x status=%x rax=%x", (unsigned)((uint32_t)parent->task.pid), (unsigned)((uint32_t)child->task.pid), (unsigned)((uint32_t)status_val), (unsigned)((uint32_t)child->task.pid));
#endif
	parent->wait_resume_child_pid = child->task.pid;
	parent->syscall_resume_rax = (uint64_t)child->task.pid;
	process_set_sched_state(parent, PROCESS_READY);
	sched_add_process(parent);
	sched_promote_process(parent);
}

void process_reap_zombie_on_wait_resume(process_t *parent, pid_t child_pid)
{
	process_t *child;
	size_t used_frames_before = 0;
	size_t used_frames_after = 0;

	if (!parent || child_pid <= 0)
		return;

	if (!process_wait_child_matches_blocked_target(parent, child_pid))
	{
		return;
	}

	child = process_find_by_pid(child_pid);
	if (!child || child->state != PROCESS_ZOMBIE ||
	    child->ppid != parent->task.pid)
		return;


	pmm_stats(NULL, &used_frames_before, NULL);
	process_fase43_proc_audit("wait-resume-before-reap");
	fase_audit_reap_zombie(child, parent->task.pid, "wait-resume");
	pmm_stats(NULL, &used_frames_after, NULL);
	process_fase44_list_checkpoint("wait-resume-after");
	process_fase43_proc_audit("wait-resume-after-reap");
	if (IR0_DEBUG_PROC)
	{
		if (used_frames_after >= used_frames_before)
			klog_debug_fmt("KERN", "%llx", (unsigned long long)((uint64_t)(used_frames_after - used_frames_before)));
		else
			klog_debug_fmt("KERN", "%llx sign=%s", (unsigned long long)((uint64_t)(used_frames_before - used_frames_after)), used_frames_after <= used_frames_before ? "-" : "+");
		klog_debug("WAIT",
			   used_frames_after <= used_frames_before
			       ? "CLASSIFY PMM_RECLAIM_ON_WAIT_OK"
			       : "CLASSIFY PMM_RECLAIM_ON_WAIT_PARTIAL");
	}
	paging_ir0_mm_checkpoint("wait-resume-after", (int32_t)parent->task.pid);
}

int process_wait(pid_t pid, int *status, int options)
{
	process_t *p;
	int found_child;
	process_t *zombie;
	uint64_t irq_flags;
	process_fase50_trace_proc("process_wait-entry", current_process);
	process_fase43_proc_audit("wait-before");
	process_fase44_list_checkpoint("wait-before");
	/*
	 * wait4 contract (D1.17):
	 *   pid > 0  — block until that child is ZOMBIE, then reap only that pid.
	 *   pid -1/0 — any child of this process (groups not implemented).
	 *   WNOHANG  — 0 if no matching zombie yet (never ECHILD when children exist).
	 *   ECHILD   — no matching child relationship at all.
	 * User-mode block stores wait_target_pid; wake/resume must not complete or
	 * reap a different child (see process_wait_wake_blocked_parent / wait-resume).
	 */
	const int any_child = (pid == (pid_t)-1 || pid == 0);

	if (!current_process) {
		klog_debug("KERN", "[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	/*
	 * wait_options for USER_MODE is seeded in sys_wait4 from pt_regs (rdx).
	 * Do not copy from the stack parameter here — it may already be clobbered.
	 */

	for (;;) {
		const int active_opts = (current_process->mode == USER_MODE)
			? current_process->wait_options
			: options;
		found_child = 0;
		zombie = NULL;
		irq_flags = process_irq_save();

		for (p = process_list; p; p = p->next) {
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
				continue;

			found_child = 1;
			if (p->state == PROCESS_ZOMBIE) {
				zombie = p;
				break;
			}
		}

		if (zombie) {
			int status_val;
			pid_t reaped_pid;
			process_fase50_trace_proc("process_wait-found-zombie", zombie);

			if (status &&
			    process_validate_userspace_buffer(status, sizeof(int)) != 0)
			{
				process_irq_restore(irq_flags);
				return -EFAULT;
			}

			status_val = process_child_wait_status_word(zombie);
			reaped_pid = zombie->task.pid;
			process_irq_restore(irq_flags);

			fase_audit_reap_zombie(zombie, current_process->task.pid, "wait");
			process_fase50_trace_proc("process_wait-after-reap", current_process);
			wait_exit_audit_process_wait_reap(reaped_pid, status_val, status);
			process_fase44_list_checkpoint("wait-after");
			process_fase43_proc_audit("wait-reap");

			if (status)
			{
				if (current_process->mode == KERNEL_MODE)
					*status = status_val;
				else if (copy_to_user(status, &status_val, sizeof(int)) != 0)
					return -EFAULT;
			}
			current_process->wait_status_ptr = NULL;
			current_process->irq_frame_saved = 0;
			current_process->wait_blocked = 0;
			current_process->wait_target_pid = 0;
			current_process->wait_options = 0;
			current_process->wait_resume_child_pid = 0;
			current_process->syscall_resume_rax = 0;
			current_process->coop_resched_resume = 0;
			task_set_retval(&current_process->task, (uint64_t)(uint32_t)reaped_pid);

			if (IR0_DEBUG_PROC)
			{
			}


			return reaped_pid;
		}

		if (!found_child)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] path=echild parent=%x target=%x ret=ECHILD\n", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid));
			}
			if (IR0_DEBUG_PROC)
			{
			}
			process_reset_blocked_syscall_state(current_process);
			return -ECHILD;
		}

		if (active_opts & WNOHANG)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] path=wnohang_alive parent=%x target=%x ret=0 status_write=no\n", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid));
			}
			if (IR0_DEBUG_PROC)
			{
			}
			process_reset_blocked_syscall_state(current_process);
			return 0;
		}

		/*
		 * Arm wait contract before dropping irq: child exit wake is ignored
		 * until wait_blocked is set (process_wait_wake_blocked_parent).
		 */
		if (current_process->mode == USER_MODE)
		{
			wait_exit_audit_process_wait_block(pid, status);
			current_process->wait_status_ptr = status;
			current_process->wait_blocked = 1;
			current_process->wait_target_pid = pid;
			current_process->wait_options = active_opts;
			current_process->wait_resume_child_pid = 0;
			current_process->coop_resched_resume = 0;
			current_process->syscall_resume_rax = 0;
			task_set_retval(&current_process->task, 0);
			/*
			 * Same contract as pipe/TTY/poll: kernel_ret only.
			 * Do NOT call process_arm_blocked_syscall_resume() —
			 * that stages USER CS+RIP with irq_frame_saved and,
			 * combined with deferred want_kernel_ret / skip-save,
			 * yields Class B (KERNEL CS + user RIP) →
			 * "kernel_ret RIP not in .text" while a child blocks
			 * on stdin (desk: ash wait4 + hexdump).
			 */
			current_process->irq_frame_saved = 0;
			process_arm_kernel_syscall_sleep(current_process);
			wait_exit_audit_classify_user_frame("parent-after-wait-arm",
							    current_process);
		}
		else
		{
			current_process->wait_blocked = 1;
			current_process->wait_target_pid = pid;
			current_process->wait_options = active_opts;
			current_process->wait_resume_child_pid = 0;
		}

		zombie = NULL;
		for (p = process_list; p; p = p->next)
		{
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
				continue;
			if (p->state == PROCESS_ZOMBIE)
			{
				zombie = p;
				break;
			}
		}
		process_irq_restore(irq_flags);
		if (zombie)
			continue;

		/*
		 * Child exit may have woken us after irq_restore; do not clobber
		 * READY with BLOCKED (missed wake → stuck with a zombie).
		 */
		if (current_process->state == PROCESS_READY)
			continue;


		process_set_sched_state(current_process, PROCESS_BLOCKED);
		while (current_process->state == PROCESS_BLOCKED)
		{
			ir0_clock_wait_service_runqueue();
			if (current_process->state != PROCESS_BLOCKED)
				break;
		}
		/*
		 * Stay on kernel CS until process_wait returns; syscall_dispatch
		 * restores user segments at sysret. Restoring here while still
		 * inside process_wait lets switch_context user-iret with the
		 * block-time rax=0 placeholder (wait4_block_reap flake).
		 */
	}
}



