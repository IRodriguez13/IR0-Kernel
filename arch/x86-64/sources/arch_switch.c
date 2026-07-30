/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_switch.c
 * Description: x86-64 switch_to implementation (TSS, syscall resume, iretq).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_switch.h>
#include <ir0/task.h>
#include <ir0/arch_task.h>
#include <ir0/process.h>
#include <ir0/process_ctx_invariant.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/klog.h>
#include <ir0/debug_runtime.h>
#include <ir0/ktm/klog.h>
#include <ir0/oops.h>
#include <ir0/ktm/fault.h>
#include <config.h>
#include <ir0/paging.h>
#include <pmm.h>

extern void switch_context_x64(task_t *prev, task_t *next);
extern uint64_t get_current_page_directory(void);
extern uint64_t kernel_syscall_stack_top;
extern uint64_t user_rsp_save;
extern void tss_set_rsp0(uint64_t rsp0);


void arch_set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	kernel_syscall_stack_top = proc->kstack_top;
	tss_set_rsp0(proc->kstack_top);
	user_rsp_save = proc->saved_user_rsp;
}

void arch_switch_save_user_rsp(struct process *prev)
{
	process_t *proc = (process_t *)prev;

	if (proc)
		proc->saved_user_rsp = user_rsp_save;
}

static void arch_fixup_user_task_for_iretq(process_t *proc)
{
	const syscall_user_frame_t *sf;
	uint64_t rip;

	if (!proc || proc->mode != USER_MODE)
		return;

	/*
	 * wait4 blocked via process_arm_kernel_syscall_sleep: task_get_cs(task) is ring-0
	 * and resume must use switch_context_x64 kernel_ret into process_wait,
	 * not syscall_frame user iretq with placeholder rax=0.
	 */
	if (proc->wait_blocked && !proc->irq_frame_saved)
		return;

	if (proc->wait_target_pid != 0 && proc->wait_resume_child_pid <= 0 &&
	    !proc->irq_frame_saved)
		return;

	if ((task_get_cs(&proc->task) & 3u) == 0)
		return;

	rip = task_get_ip(&proc->task);
	if (rip >= 0x00400000ULL && rip <= 0x00007FFFFFFFFFFFULL)
		return;

	sf = &proc->syscall_frame;
	if (!process_rip_in_user_range(process_syscall_ip(proc)))
		return;

	process_apply_syscall_frame_to_task(&proc->task, sf, task_get_retval(&proc->task));
}

static void wait_exit_audit_ctx_resume(process_t *prev_proc, process_t *next_proc,
                                       task_t *next)
{
#if !IR0_DEBUG_WAIT
	(void)prev_proc;
	(void)next_proc;
	(void)next;
	return;
#else
	klog_print("WAIT CTX prev_pid=");
	klog_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
	klog_print(" prev_state=");
	klog_hex64(prev_proc ? (uint64_t)prev_proc->state : 0);
	klog_print(" prev_irq_saved=");
	klog_hex64(prev_proc ? (uint64_t)prev_proc->irq_frame_saved : 0);
	klog_print(" next_pid=");
	klog_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
	klog_print(" next_state=");
	klog_hex64(next_proc ? (uint64_t)next_proc->state : 0);
	klog_print(" next_irq_saved=");
	klog_hex64(next_proc ? (uint64_t)next_proc->irq_frame_saved : 0);
	klog_print(" next_cr3=");
	klog_hex64(next ? task_mm_root(next) : 0);
	klog_print(" active_cr3=");
	klog_hex64(get_current_page_directory());
	klog_print("\n");

	if (prev_proc && prev_proc->state == PROCESS_ZOMBIE)
	{
		klog_info("WAIT", "CLASSIFY SCHED_SELECTED_ZOMBIE note=prev_is_zombie_on_switch");
	}
	if (prev_proc && prev_proc->irq_frame_saved &&
	    (!next_proc || next_proc->state != PROCESS_BLOCKED))
	{
		klog_info("WAIT", "CLASSIFY WAITPID_PARENT_CONTEXT_CORRUPT reason=prev_irq_saved_but_next_not_blocked");
	}
	if (prev_proc && prev_proc->irq_frame_saved && next_proc &&
	    next_proc->irq_frame_saved == 0)
	{
		klog_info("WAIT", "CLASSIFY WAITPID_PARENT_CONTEXT_CORRUPT reason=resume_triggered_by_prev_irq_not_next");
	}

	if (next_proc)
	{
		uint64_t rip = task_get_ip(&next_proc->task);
		uint64_t rsp = task_get_sp(&next_proc->task);
		uint16_t cs = task_get_cs(&next_proc->task);
		uint16_t ss = task_get_ss(&next_proc->task);

		klog_print("WAIT CTX next_user_frame rip=");
		klog_hex64(rip);
		klog_print(" rsp=");
		klog_hex64(rsp);
		klog_print(" cs=");
		klog_hex64((uint64_t)cs);
		klog_print(" ss=");
		klog_hex64((uint64_t)ss);
		klog_print(" rflags=");
		klog_hex64(task_get_flags(&next_proc->task));
		klog_print(" rax=");
		klog_hex64(task_get_retval(&next_proc->task));
		klog_print("\n");

		if (task_mm_root(next) == 0 && process_pgd(next_proc))
		{
			klog_info("WAIT", "CLASSIFY PARENT_CR3_BAD reason=task_cr3_zero");
		}
		if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
		{
			klog_info("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RIP");
		}
		if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
		{
			klog_info("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RSP");
		}
		if (!task_cs_is_user(&next_proc->task) || (ss & 3u) != 3u)
		{
			klog_info("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_CS_SS");
		}
	}
#endif
}

void arch_switch_to(task_t *prev, task_t *next)
{
process_t *prev_proc;
    process_t *next_proc = NULL;

    if (next)
    {
        next_proc = task_to_process(next);
        if (task_mm_root(next) == 0 && process_pgd(next_proc))
            task_set_mm_root(next, (uint64_t)(uintptr_t)process_pgd(next_proc));
    }

    prev_proc = prev ? task_to_process(prev) : NULL;

    /*
     * Per-process kernel stack handoff. Snapshot the outgoing task's live user
     * RSP shadow, then point the kernel entry stacks (+ user RSP shadow) at the
     * incoming task. Covers all resume paths below (switch_to_user_task,
     * kernel_ret, user iretq) since every one funnels through here.
     */
    if (prev_proc)
        prev_proc->saved_user_rsp = user_rsp_save;
    arch_set_current_kernel_stack(next_proc);

    /*
     * wait4 in progress without a staged child pid: force kernel resume.
     * Preserve irq_frame_saved when wait_blocked (syscall_frame sleep) so
     * child-exit wake can stage wait_resume_child_pid before user iret.
     * Do not arm ring-0 CS when task.rip is still userspace — that creates
     * KERNEL_CS+user RIP and a later iretq with stale GPRs hangs the desk.
     */
    if (next_proc && next_proc->mode == USER_MODE &&
        next_proc->wait_resume_child_pid <= 0 &&
        (next_proc->wait_blocked || next_proc->wait_target_pid != 0) &&
        !next_proc->coop_resched_resume)
    {
        uint64_t nrip = task_get_ip(&next_proc->task);

        if (nrip < 0x00400000ULL || nrip > 0x00007FFFFFFFFFFFULL)
            process_arm_kernel_syscall_sleep(next_proc);
        if (!next_proc->wait_blocked)
        {
            next_proc->irq_frame_saved = 0;
            next_proc->coop_resched_resume = 0;
        }
    }


    /*
     * Syscall-block resume (wait4): resume the task we are switching TO when
     * it blocked with a saved user frame.  Never key off prev->irq_frame_saved
     * (stale timer IRQ flags on exiting/zombie tasks misroute resume).
     */
    if (next_proc && next_proc->irq_frame_saved)
    {
        const int wait_sleep_no_child =
            (next_proc->wait_blocked || next_proc->wait_target_pid != 0) &&
            next_proc->wait_resume_child_pid <= 0 &&
            !next_proc->coop_resched_resume;

        /*
         * wait4 blocked with no reaped child yet — kernel_ret into process_wait,
         * never user-iret with placeholder syscall_resume_rax=0. Keep
         * irq_frame_saved when wait_blocked so wake can stage the child pid.
         */
        if (wait_sleep_no_child)
        {
            uint64_t nrip = task_get_ip(&next_proc->task);

            if (nrip < 0x00400000ULL || nrip > 0x00007FFFFFFFFFFFULL)
                process_arm_kernel_syscall_sleep(next_proc);
        }
        else if (next_proc->syscall_resume_rax == 0 &&
                 !(next_proc->wait_blocked &&
                   next_proc->wait_resume_child_pid > 0))
        {
            /*
             * Stale syscall-frame resume (wait4 placeholder rax=0, coop
             * reschedule with rax=0). Continue in kernel instead of
             * iretq with rax=0.
             *
             * Pipe/TTY/poll must NOT arm blocked_resume(rax=0); they use
             * process_arm_kernel_syscall_sleep only (portable kernel_ret).
             * This branch is x86 wait/coop legacy — do not extend to new
             * in-syscall sleepers.
             */
            next_proc->irq_frame_saved = 0;
            next_proc->coop_resched_resume = 0;
        }
        else
        {
        syscall_user_frame_t *frame = &next_proc->syscall_frame;

        wait_exit_audit_ctx_resume(prev_proc, next_proc, next);
#if IR0_DEBUG_WAIT
        klog_info("WAIT", "CLASSIFY RESUME_GATE_USES_NEXT_FIXED");
        klog_debug("WAIT", "CTX resume_path=switch_to_user_task");
#endif
        /*
         * Tear down the zombie child mm only after switching CR3 to the
         * waiting parent.  Reaping while the exiting child's CR3 is still
         * active unmaps the running page tables and faults mid-destroy.
         */
        if (next && task_mm_root(next))
            load_page_directory(task_mm_root(next));
        /*
         * Cooperative reschedule resume carries a syscall retval in
         * syscall_resume_rax, not a child pid — skip the wait4 zombie reap so
         * a retval that happens to match a zombie pid cannot reap it.
         */
        if (!next_proc->coop_resched_resume)
        {
            pid_t resume_child = next_proc->wait_resume_child_pid;

            if (resume_child <= 0)
                resume_child = (pid_t)next_proc->syscall_resume_rax;


            process_reap_zombie_on_wait_resume(next_proc, resume_child);
        }
        {
            uint64_t resume_rax = next_proc->syscall_resume_rax;

            if (!next_proc->coop_resched_resume && next_proc->wait_blocked &&
                next_proc->wait_resume_child_pid > 0)
                resume_rax = (uint64_t)next_proc->wait_resume_child_pid;

            process_apply_syscall_frame_to_task(&next_proc->task, frame,
                                                resume_rax);
        }
        next_proc->wait_status_ptr = NULL;
        next_proc->wait_blocked = 0;
        next_proc->wait_target_pid = 0;
        next_proc->wait_options = 0;
        next_proc->wait_resume_child_pid = 0;
        next_proc->irq_frame_saved = 0;
        next_proc->coop_resched_resume = 0;
        if (next)
        {
#if IR0_DEBUG_WAIT
            {
                uint64_t active_cr3_before = get_current_page_directory();
                uint64_t active_cr3_after_expected = next ? task_mm_root(next) : 0;
                uint64_t next_task_cr3 = next ? task_mm_root(next) : 0;
                uint64_t current_before = (uint64_t)(uintptr_t)current_process;
                uint64_t frame_addr = (uint64_t)(uintptr_t)frame;
                uint64_t next_proc_addr = (uint64_t)(uintptr_t)next_proc;
                uint64_t next_proc_end = next_proc_addr + sizeof(process_t);
                int frame_in_next_proc = (frame_addr >= next_proc_addr &&
                                          frame_addr < next_proc_end);
                int frame_in_kernel = (frame_addr < 0x00400000ULL ||
                                       frame_addr > 0x00007FFFFFFFFFFFULL);

                klog_print("CTX RESUME prev_pid=");
                klog_hex32(prev_proc ? (uint32_t)prev_proc->task.pid : 0);
                klog_print(" next_pid=");
                klog_hex32(next_proc ? (uint32_t)next_proc->task.pid : 0);
                klog_print(" current=");
                klog_hex64(current_before);
                klog_print(" active_cr3_before=");
                klog_hex64(active_cr3_before);
                klog_print(" active_cr3_pre_iret=");
                klog_hex64(get_current_page_directory());
                klog_print(" active_cr3_after_expected=");
                klog_hex64(active_cr3_after_expected);
                klog_print(" next_task_cr3=");
                klog_hex64(next_task_cr3);
                klog_print(" frame=");
                klog_hex64(frame_addr);
                klog_print(" frame_in_kernel=");
                klog_print(frame_in_kernel ? "1" : "0");
                klog_print(" frame_in_next_proc=");
                klog_print(frame_in_next_proc ? "1" : "0");
                klog_print(" frame_rip=");
                klog_hex64(frame ? frame->rip : 0);
                klog_print(" frame_rsp=");
                klog_hex64(frame ? frame->rsp : 0);
                klog_print(" frame_cs=");
                klog_hex64(next_proc ? task_get_cs(&next_proc->task) : 0);
                klog_print(" frame_ss=");
                klog_hex64(next_proc ? task_get_ss(&next_proc->task) : 0);
                klog_print(" frame_rflags=");
                klog_hex64(frame ? frame->rflags : 0);
                klog_print("\n");

                klog_print("CTX RESUME_FRAME rbx=");
                klog_hex64(frame ? frame->rbx : 0);
                klog_print(" rbp=");
                klog_hex64(frame ? frame->rbp : 0);
                klog_print(" r12=");
                klog_hex64(frame ? frame->r12 : 0);
                klog_print(" r13=");
                klog_hex64(frame ? frame->r13 : 0);
                klog_print(" r14=");
                klog_hex64(frame ? frame->r14 : 0);
                klog_print(" r15=");
                klog_hex64(frame ? frame->r15 : 0);
                klog_print(" rdi=");
                klog_hex64(frame ? frame->rdi : 0);
                klog_print(" rsi=");
                klog_hex64(frame ? frame->rsi : 0);
                klog_print(" rdx=");
                klog_hex64(frame ? frame->rdx : 0);
                klog_print(" r10=");
                klog_hex64(frame ? frame->r10 : 0);
                klog_print(" r8=");
                klog_hex64(frame ? frame->r8 : 0);
                klog_print(" r9=");
                klog_hex64(frame ? frame->r9 : 0);
                klog_print("\n");
            }
#endif
            switch_to_user_task(next);
#if IR0_DEBUG_WAIT
            klog_print("CTX RESUME unexpected_return active_cr3_after=");
            klog_hex64(get_current_page_directory());
            klog_print("\n");
#endif
        }
        return;
        }
    }

    /*
     * wait4 blocked in process_wait: re-assert ring-0 before switch_context
     * only when rip is already in-kernel (post-save). User RIP + kernel CS is
     * the desk-session #UD class; leave user CS so iretq is used instead.
     */
    if (next_proc && next_proc->wait_target_pid != 0 &&
        next_proc->wait_resume_child_pid <= 0 && !next_proc->coop_resched_resume)
    {
        uint64_t nrip = task_get_ip(&next_proc->task);

        if (nrip < 0x00400000ULL || nrip > 0x00007FFFFFFFFFFFULL)
            process_arm_kernel_syscall_sleep(next_proc);
    }

    arch_fixup_user_task_for_iretq(next_proc);

    /*
     * Finish deferred arm: save already put kernel RIP on this task, or we are
     * switching to a waiter whose rip is kernel .text — apply KERNEL CS now.
     */
    if (next && next_proc && next_proc->want_kernel_ret &&
        !process_rip_in_user_range(task_get_ip(next)))
    {
        process_arm_kernel_syscall_sleep(next_proc);
    }

    /*
     * KTM: force Class B on *next* (KERNEL CS + user RIP) before sanitize.
     * Seed syscall_frame so REPAIR can apply a coherent user iretq frame.
     * With IR0_CLASS_B_REPAIR=0 → KERNEL_RET_BAD_RIP.
     */
    if (next && next_proc && next_proc->mode == USER_MODE &&
        KTM_FAULT_HIT("sched.class_b_arm_window"))
    {
		arch_task_set_kernel_segments(next);
		task_set_ip(next, IR0_USER_RIP_LO + 0x1000ULL);
        if (!process_rip_in_user_range(task_get_sp(next)))
            task_set_sp(next, 0x00007FFFFFF0ULL);
        process_syscall_set_ip(next_proc, task_get_ip(next));
        process_syscall_set_sp(next_proc, task_get_sp(next));
        process_syscall_set_flags(next_proc, task_get_flags(next) | 2ULL);
        klog_info("CTX", "CLASSIFY CLASS_B_FAULT_INJECT");
    }

    /*
     * Linux-like Class B safety net (IR0_CLASS_B_REPAIR): KERNEL_CS + user RIP
     * must not reach kernel_ret. Natural paths should not create this after
     * pt_regs-only capture + want_kernel_ret; KTM inject still exercises it.
     * Repair only when syscall_frame has usable user RIP/RSP.
     */
#if IR0_CLASS_B_REPAIR
    /*
     * Do not skip waiters: ash wait4 + blocking child (hexdump/stdin) used to
     * hit Class B while wait_blocked=1, and the old exclusion let it panic.
     */
    if (next && next_proc && next_proc->mode == USER_MODE &&
        process_task_kernel_ret_rip_bad(next) &&
        !next_proc->coop_resched_resume)
    {
        const syscall_user_frame_t *sf = &next_proc->syscall_frame;
        const int wait_no_child =
            (next_proc->wait_blocked || next_proc->wait_target_pid != 0) &&
            next_proc->wait_resume_child_pid <= 0;

        if (wait_no_child)
        {
            /*
             * Cannot safely apply syscall_frame (placeholder rax=0). Demote
             * to USER CS so kernel_ret does not jmp to a user VA. Prefer
             * surviving over panic; formation is fixed in process_wait arm.
             */
            klog_info("CTX", "CLASSIFY KERNEL_CS_USER_RIP_WAIT_DEMOTE");
            next_proc->irq_frame_saved = 0;
            process_restore_user_task_segments(next_proc);
        }
        else if (process_rip_in_user_range(process_syscall_ip(next_proc)) &&
                 process_rip_in_user_range(process_syscall_sp(next_proc)) &&
                 !process_rip_in_user_stack(process_syscall_ip(next_proc)))
        {
            uint64_t rax = next_proc->syscall_resume_rax;

            if (rax == 0)
                rax = task_get_retval(&next_proc->task);
            klog_info("CTX", "CLASSIFY KERNEL_CS_USER_RIP_REPAIR");
			process_apply_syscall_frame_to_task(&next_proc->task, sf, rax);
            if (next_proc->signal_enter_pending && next_proc->saved_context)
                next_proc->signal_enter_pending = 0;
        }
        else
            klog_info("CTX", "CLASSIFY KERNEL_CS_USER_RIP_UNREPAIRED");
    }
#endif

    switch_context_x64(prev, next);
}
