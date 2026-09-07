/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: core.c
 * Description: Process list, PID allocation, init, and syscall-frame resume helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/process_ctx_invariant.h>
#include <ir0/task_ops.h>
#include <ir0/syscall_frame.h>
#include <ir0/ktm/event.h>

static pid_t next_pid = 2;

void process_sched_state_trace(const process_t *p, process_state_t prev,
			       process_state_t next, void *caller)
{
	if (!p)
		return;

	/*
	 * The event's own pid field records who is running (the waker); arg0
	 * records who is affected (the sleeper). A lost wakeup is exactly the
	 * case where those differ and the WAKE lands before the BLOCK.
	 */
	ktm_event_emit4(next == PROCESS_BLOCKED ? KTM_EVENT_BLOCK
						: KTM_EVENT_WAKE,
			KTM_SUBSYS_SCHED,
			(uint64_t)(uint32_t)p->task.pid,
			(uint64_t)prev, (uint64_t)next,
			(uint64_t)(uintptr_t)caller);
}

static void syscall_frame_to_arch(const syscall_user_frame_t *sf,
				  arch_task_syscall_frame_t *out)
{
	if (!sf || !out)
		return;

	*out = *sf;
}

int process_task_kernel_ret_rip_bad(const task_t *t)
{
	if (!t)
		return 0;
	return process_cs_rip_kernel_ret_bad((uint64_t)task_get_cs(t), task_get_ip(t));
}

uint64_t process_list_count(void)
{
	process_t *p;
	uint64_t n = 0;

	for (p = process_list; p; p = p->next)
		n++;
	return n;
}

uint64_t process_list_count_user(void)
{
	process_t *p;
	uint64_t n = 0;

	for (p = process_list; p; p = p->next)
	{
		if (p->mode == USER_MODE && p->state != PROCESS_ZOMBIE)
			n++;
	}
	return n;
}

void process_fase50_trace_proc(const char *stage, process_t *p)
{
	(void)stage;
	(void)p;
}

process_t *current_process = NULL;
process_t *process_list = NULL;


void process_init(void)
{
	current_process = NULL;
	process_list = NULL;
	ir0_debug_trap_init();
	/* First spawned process is /sbin/init (PID 1). */
	next_pid = 1;
}


pid_t process_get_next_pid(void)
{
	uint64_t irq_flags = process_irq_save();
	pid_t pid = next_pid++;
	process_irq_restore(irq_flags);
	return pid;
}

/* Highest PID already handed out (0 if none). */
pid_t process_last_assigned_pid(void)
{
	uint64_t irq_flags = process_irq_save();
	pid_t last = (next_pid > 1) ? (pid_t)(next_pid - 1) : 0;

	process_irq_restore(irq_flags);
	return last;
}

/*
 * KTM boot scenarios (and similar early probes) may advance next_pid.
 * runit-init / BusyBox init require getpid()==1. Restore the allocator so the
 * first userspace spawn is PID 1 when that slot is free.
 */
void process_prepare_pid1_for_init(void)
{
	uint64_t irq_flags;

	if (process_find_by_pid(1))
		return;

	irq_flags = process_irq_save();
	next_pid = 1;
	process_irq_restore(irq_flags);
}

process_t *process_get_current(void)
{
	return current_process;
}

/*
 * Copy user context from an opaque ISA exception frame into the current task.
 * Frame classification and decoding remain architecture-owned.
 */
void process_save_user_exception_frame(void *frame)
{
	process_t *p;

	if (!frame)
		return;

	p = current_process;
	if (!p || p->mode != USER_MODE)
		return;

	if (!exception_frame_is_user(frame))
		return;

#if CONFIG_DEBUG_ISRABI
	klog_debug_fmt("ISR", "[ISRABI][IRQ_SAVE] pid=%x source=opaque_arch_frame",
		       (unsigned)(current_process
			? (uint32_t)current_process->task.pid : 0));
#endif

	task_save_user_exception_frame(&p->task, frame);

#if CONFIG_DEBUG_ISRABI
	klog_debug_fmt("ISR", "[ISRABI][IRQ_SAVE] task_rip=%llx task_rsp=%llx task_cs=%llx task_ss=%llx task_rflags=%llx", (unsigned long long)(task_get_ip(&p->task)), (unsigned long long)(task_get_sp(&p->task)), (unsigned long long)((uint64_t)task_get_cs(&p->task)), (unsigned long long)((uint64_t)task_get_ss(&p->task)), (unsigned long long)(task_get_flags(&p->task)));
#endif
}

pid_t process_get_pid(void)
{
	return current_process ? process_pid(current_process) : 0;
}

pid_t process_get_ppid(void)
{
	return current_process ? current_process->ppid : 0;
}

process_t *get_process_list(void)
{
	return process_list;
}



int process_validate_userspace_buffer(const void *buf, size_t size)
{
	if (!current_process)
		return -ESRCH;

	if (current_process->mode == KERNEL_MODE)
	{
		uint64_t addr = (uint64_t)buf;

		if (addr >= process_stack_start(current_process) &&
		    addr + size <= process_stack_start(current_process) + process_stack_size(current_process))
			return 0;
		if (process_heap_start(current_process) > 0 &&
		    addr >= process_heap_start(current_process) &&
		    addr + size <= process_heap_end(current_process))
			return 0;
		if (is_user_address(buf, size))
			return 0;
		return 0;
	}

	if (!is_user_address(buf, size))
		return -EFAULT;

	return 0;
}

/*
 * process_capture_syscall_frame_at_entry - Snapshot user GPRs at syscall entry.
 * Layout decode is ISA-private (arch_syscall_frame).
 */
void process_capture_syscall_frame_at_entry(uint64_t *frame_base, uint64_t rip_hw)
{
	syscall_capture_frame_at_entry(current_process, frame_base,
						    rip_hw);
}

/*
 * Soft mirror of syscall_frame → task while CS is still user (Class B safe).
 */
void process_sync_task_user_ip_from_syscall_frame(process_t *p)
{
	syscall_user_frame_t *sf;

	if (!p || p->mode != USER_MODE)
		return;
	if (!task_cs_is_user(&p->task) || p->want_kernel_ret)
		return;

	sf = &p->syscall_frame;
	task_sync_syscall_soft_mirror(&p->task, sf);
}

void process_capture_syscall_frame(process_t *p)
{
	/*
	 * Capture is at syscall entry (syscall_capture_frame_at_entry).
	 * Dispatch still calls this; keep it as a documented no-op.
	 */
	(void)p;
}

void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax)
{
	arch_task_syscall_frame_t arch_sf;

	if (!task || !sf)
		return;

	syscall_frame_to_arch(sf, &arch_sf);
	task_apply_syscall_frame(task, &arch_sf, rax);
}

void process_syscall_restore_exit_regs(uint64_t *stack_r9_slot)
{
	syscall_restore_exit_regs(current_process, stack_r9_slot);
}

void process_arm_blocked_syscall_resume(process_t *p, uint64_t rax)
{
	if (!p || p->mode != USER_MODE)
		return;

	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame, rax);
	p->syscall_resume_rax = rax;
	p->irq_frame_saved = 1;
}

/*
 * process_arm_coop_resched_resume - Arm a cooperative in-syscall reschedule to
 * resume via the saved syscall_frame (fresh iretq) instead of kernel_ret on the
 * shared global syscall stack. Unlike wait4, there is no zombie child to reap,
 * so coop_resched_resume tells switch_to to skip the reap step.
 * Only valid for syscall-insn tasks (syscall_frame_fresh).
 */
void process_arm_coop_resched_resume(process_t *p, uint64_t rax)
{
	if (!p || p->mode != USER_MODE)
		return;

	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame, rax);
	p->syscall_resume_rax = rax;
	p->coop_resched_resume = 1;
	p->irq_frame_saved = 1;
}

/*
 * process_clear_in_thread_syscall_block - Drop irq_frame_saved after blocking
 * syscalls that resume inside the syscall handler (poll/pipe read loops), not
 * via switch_to_user_task.
 */
void process_clear_in_thread_syscall_block(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
	p->want_kernel_ret = 0;
	p->kernel_syscall_sleep = 0;
	process_kernel_sleep_interrupted_clear(p);
	p->syscall_block_nr = 0;
}

void process_reset_blocked_syscall_state(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
	p->want_kernel_ret = 0;
	p->kernel_syscall_sleep = 0;
	process_kernel_sleep_interrupted_clear(p);
	p->syscall_resume_rax = 0;
	p->syscall_entry_nr = 0;
	p->syscall_block_nr = 0;
	p->syscall_frame_fresh = 0;
	p->syscall_interrupted = 0;
	process_wait_state_init(p);
	p->poll_waiter = NULL;
	p->clock_wait_armed = 0;
	p->clock_wait_deadline_ms = IR0_CLOCK_WAIT_DISARMED;
}

static void process_apply_kernel_ret_segments(process_t *p)
{
	task_apply_kernel_segments(&p->task);
}

void process_kernel_sleep_capture_syscall_frame(process_t *p)
{
	if (!p || !p->syscall_frame_fresh)
		return;

	p->kernel_sleep_syscall_frame = p->syscall_frame;
	p->syscall_block_nr = p->syscall_entry_nr;
	/*
	 * wait4(-1) is valid; only normalize read(0) snapshots so a stale
	 * block frame cannot restart login read with rdi=-1 → #PF at ~0x3f.
	 */
	if (p->syscall_block_nr == 0u &&
	    (int64_t)syscall_frame_arg(&p->kernel_sleep_syscall_frame, 0) < 0)
		syscall_frame_set_arg(&p->kernel_sleep_syscall_frame, 0, 0);
}

/*
 * process_arm_kernel_syscall_sleep - Prepare a blocked syscall for
 * kernel_ret resume after switch_context save (not via user RIP).
 *
 * Name: "arm" is the English verb (prepare/enable), not the ARM ISA.
 *
 * User regs live in syscall_frame (pt_regs). If task.arch.rip still looks like
 * userspace (stale from a prior iretq), only set want_kernel_ret — never pair
 * KERNEL_CS with that RIP. Outgoing save stores kernel [rsp] + CPU CS;
 * process_after_task_save clears the flag. If rip is already kernel .text,
 * apply KERNEL CS immediately.
 */
void process_arm_kernel_syscall_sleep(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	/*
	 * Outlives want_kernel_ret, which process_after_task_save clears once
	 * the segments are applied. The resume gate runs later still and needs
	 * to know this task must re-enter its syscall rather than iretq.
	 */
	p->kernel_syscall_sleep = 1;

	/*
	 * Snapshot the syscall entry frame at block time. Delivery must not
	 * trust syscall_frame later — kstack GPR residue and handler redirect
	 * can leave rdi=-1 and double keystrokes after SIGCHLD (line editor).
	 */
	if (p->syscall_frame_fresh)
		process_kernel_sleep_capture_syscall_frame(p);
	else if (p->syscall_block_nr != 0u &&
		 p->syscall_block_nr != p->syscall_entry_nr)
	{
		/*
		 * Prior syscall left a stale block snap (e.g. wait4) while we
		 * block again in read — drop it so sigreturn cannot restart
		 * the wrong syscall.
		 */
		memset(&p->kernel_sleep_syscall_frame, 0,
		       sizeof(p->kernel_sleep_syscall_frame));
		p->syscall_block_nr = 0;
	}

	if (process_rip_in_user_range(task_get_ip(&p->task)))
	{
		p->want_kernel_ret = 1;
		return;
	}

	process_apply_kernel_ret_segments(p);
	p->want_kernel_ret = 0;
}

/*
 * process_after_task_save - Linux-like post-switch save epilogue.
 *
 * Called from switch_context_x64 after prev GPRs/RIP/CS were written from the
 * CPU (kernel return address + ring-0 CS). Honour want_kernel_ret so the next
 * resume takes kernel_ret, not user iretq with a stale frame.
 */
void process_after_task_save(task_t *prev)
{
	process_t *p;

	if (!prev)
		return;

	p = task_to_process(prev);
	if (!p || p->mode != USER_MODE || !p->want_kernel_ret)
		return;

	if (process_rip_in_user_range(task_get_ip(prev)))
		return;

	process_apply_kernel_ret_segments(p);
	p->want_kernel_ret = 0;
}

void process_restore_user_task_segments(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	p->want_kernel_ret = 0;
	p->kernel_syscall_sleep = 0;
	task_apply_user_segments(&p->task);
}
