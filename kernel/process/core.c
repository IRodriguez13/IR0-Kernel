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
#include <ir0/arch_task_ops.h>
#include <ir0/arch_syscall_frame.h>

static pid_t next_pid = 2;

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
 * irq_save_user_frame - Copy user context from an IRQ stub frame into the
 * current task. Frame layout is ISA-private (decoded in arch_task_ops).
 */
void irq_save_user_frame(uint64_t *frame)
{
	process_t *p;

	if (!frame)
		return;

	p = current_process;
	if (!p || p->mode != USER_MODE)
		return;

	if (!arch_irq_frame_is_user(frame))
		return;

#if CONFIG_DEBUG_ISRABI
	klog_debug_fmt("ISR", "[ISRABI][IRQ_SAVE] pid=%x src_int=%llx src_err=%llx src_rip=%llx src_cs=%llx src_rflags=%llx src_rsp=%llx src_ss=%llx", (unsigned)(current_process ? (uint32_t)current_process->task.pid : 0), (unsigned long long)(frame[0]), (unsigned long long)(frame[1]), (unsigned long long)(frame[2]), (unsigned long long)(frame[3]), (unsigned long long)(frame[4]), (unsigned long long)(frame[5]), (unsigned long long)(frame[6]));
#endif

	arch_task_save_irq_user_frame(&p->task, frame);

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
	arch_process_capture_syscall_frame_at_entry(current_process, frame_base,
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
	arch_task_sync_syscall_soft_mirror(&p->task, sf);
}

void process_capture_syscall_frame(process_t *p)
{
	(void)p;
}

void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax)
{
	arch_task_syscall_frame_t arch_sf;

	if (!task || !sf)
		return;

	syscall_frame_to_arch(sf, &arch_sf);
	arch_task_apply_syscall_frame(task, &arch_sf, rax);
}

void process_syscall_restore_exit_regs(uint64_t *stack_r9_slot)
{
	arch_process_syscall_restore_exit_regs(current_process, stack_r9_slot);
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
	/*
	 * Clear want_kernel_ret only from in-syscall return paths (tty_sleep
	 * poll_ready, pipe, etc.). ir0_console_wake_readers must NOT call this —
	 * async wake races process_after_task_save and must leave the flag alone.
	 */
	p->want_kernel_ret = 0;
}

void process_reset_blocked_syscall_state(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
	p->want_kernel_ret = 0;
	p->syscall_resume_rax = 0;
	p->syscall_interrupted = 0;
	p->wait_status_ptr = NULL;
	p->wait_blocked = 0;
	p->wait_blocked = 0;
	p->wait_target_pid = 0;
	p->wait_options = 0;
	p->wait_resume_child_pid = 0;
	p->poll_waiter = NULL;
	p->clock_wait_armed = 0;
	p->clock_wait_deadline_ms = IR0_CLOCK_WAIT_DISARMED;
}

static void process_apply_kernel_ret_segments(process_t *p)
{
	arch_task_apply_kernel_segments(&p->task);
}

/*
 * process_arm_kernel_syscall_sleep - Linux-like: mark blocked syscall for
 * kernel_ret resume after switch_context save (not via user RIP).
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
	arch_task_apply_user_segments(&p->task);
}


void process_save_user_context_from_irq_frame(uint64_t *gpr_stack)
{
	arch_process_save_user_context_from_irq(gpr_stack);
}

