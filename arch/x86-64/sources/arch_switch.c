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
#include <ir0/ktm/deferred.h>
#include <ir0/arch_cpu.h>
#include <ir0/klog.h>
#include <ir0/debug_runtime.h>
#include <ir0/ktm/klog.h>
#include <ir0/oops.h>
#include <ir0/ktm/fault.h>
#include <config.h>
#include <ir0/paging.h>
#include <pmm.h>
#include <mm/allocator.h>

extern int switch_context_x64(task_t *prev, task_t *next);
extern uintptr_t paging_current_address_space(void);
extern uint64_t kernel_syscall_stack_top;
extern uint64_t user_rsp_save;
extern void tss_set_rsp0(uint64_t rsp0);


static int arch_va_in_kstack_window(uint64_t v)
{
	return v >= IR0_KSTACK_VA_BASE &&
	       v < IR0_KSTACK_VA_BASE +
		   (uint64_t)IR0_KSTACK_MAX_SLOTS * IR0_KSTACK_SLOT_SIZE;
}

/*
 * Pointer-class kernel VA (not a small syscall retval). Mid-syscall saves leave
 * kmalloc identity (SIMPLE_HEAP) in callee-saved regs; the old check only
 * caught the high kstack window, so heap RAX/RBP survived into ring-3
 * (desk re-login: read() "returned" ~0x1a8120 → SEGV in ir0_read_line).
 */
static int arch_va_kernel_ptr_leak(uint64_t v)
{
	if (v == 0)
		return 0;
	if (arch_va_in_kstack_window(v))
		return 1;
	if (v >= (uint64_t)SIMPLE_HEAP_START && v < (uint64_t)SIMPLE_HEAP_END)
		return 1;
	if (v >= 0xffff800000000000ULL)
		return 1;
	return 0;
}

/*
 * Ring-3 resume invariant: no GPR handed to user may be a kernel pointer.
 * A task saved mid-syscall keeps kernel callee-saved values; any path that
 * flips CS back to user without reapplying the entry frame would leak them.
 */
static int arch_task_user_gprs_leak(const task_t *t)
{
	return arch_va_kernel_ptr_leak(t->arch.rbp) ||
	       arch_va_kernel_ptr_leak(t->arch.rbx) ||
	       arch_va_kernel_ptr_leak(t->arch.r12) ||
	       arch_va_kernel_ptr_leak(t->arch.r13) ||
	       arch_va_kernel_ptr_leak(t->arch.r14) ||
	       arch_va_kernel_ptr_leak(t->arch.r15) ||
	       arch_va_kernel_ptr_leak(t->arch.rax);
}

/*
 * Reapply syscall_frame before user iretq when callee-saved GPRs still carry
 * kernel-stack residue.  Never run on kernel_syscall_sleep / want_kernel_ret:
 * those tasks resume via kernel_ret inside the syscall handler, not iretq with
 * task.arch GPRs (TTY read + SIGCHLD was spamming USER_RESUME_KSTACK_GPR_LEAK
 * and occasionally pushing a user frame onto a kernel_ret waiter → login #PF).
 */
static int arch_will_resume_user_iretq(const process_t *proc, const task_t *task)
{
	if (!proc || !task || proc->mode != USER_MODE)
		return 0;
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
		return 0;
	if (proc->irq_frame_saved)
		return 0;
	if ((proc->wait_blocked || proc->wait_target_pid != 0) &&
	    proc->wait_resume_child_pid <= 0 && !proc->coop_resched_resume)
		return 0;
	if (!task_cs_is_user(task))
		return 0;
	if (!process_rip_in_user_range(task_get_ip(task)))
		return 0;
	return 1;
}

static void arch_repair_user_gprs_from_syscall_frame(process_t *proc,
						     task_t *task)
{
	uint64_t rax;

	if (!proc || !task || proc->mode != USER_MODE)
		return;
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
		return;
	if (!proc->syscall_frame_fresh || !task_cs_is_user(task))
		return;
	if (!arch_task_user_gprs_leak(task))
		return;

	rax = proc->syscall_resume_rax;
	if (rax == 0 && !arch_va_kernel_ptr_leak(task_get_retval(task)))
		rax = task_get_retval(task);
	klog_debug("CTX", "CLASSIFY USER_RESUME_KSTACK_GPR_LEAK");
	process_apply_syscall_frame_to_task(task, &proc->syscall_frame, rax);
}

void arch_prepare_task_user_iretq(process_t *proc)
{
	if (!proc || proc->mode != USER_MODE)
		return;
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
		return;
	arch_repair_user_gprs_from_syscall_frame(proc, &proc->task);
}

void set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	kernel_syscall_stack_top = proc->kstack_top;
	tss_set_rsp0(proc->kstack_top);
	user_rsp_save = proc->saved_user_rsp;
}

void switch_save_user_rsp(struct process *prev)
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
	 * Blocked syscalls (TTY read, pipe, poll) resume in-kernel via
	 * kernel_ret — do not rewrite task.arch to the syscall entry frame.
	 */
	if (proc->kernel_syscall_sleep || proc->want_kernel_ret)
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
	klog_hex64(paging_current_address_space());
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
    /*
     * High-regression area: wait4, irq_frame_saved, syscall_resume_rax,
     * and kernel_ret vs user-iret. Do not simplify these gates without
     * wait4 + blocked-syscall coverage.
     */
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
    set_current_kernel_stack(next_proc);

    /*
     * IA32_FS_BASE is per-CPU. Child execve / ARCH_SET_FS writes the MSR
     * while current==child; wait4/pipe kernel_ret and some user-iret
     * resumes never hit syscall sysret's restore_user_fs_base.
     * Parent ash then ran with FS=0 and the next TLS store #PF'd at
     * 0xffffffffffffffe2 (TP + negative TCB offset). Match ARM64:
     * always install next's saved base before the context switch.
     */
    if (next_proc)
        set_tls(process_tls_get(next_proc));

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
        else if (!next_proc->coop_resched_resume &&
                 (next_proc->kernel_syscall_sleep ||
                  next_proc->syscall_resume_rax == 0) &&
                 !(next_proc->wait_blocked &&
                   next_proc->wait_resume_child_pid > 0))
        {
            /*
             * Stale syscall-frame resume (wait4 placeholder rax=0). Continue
             * in kernel instead of iretq with rax=0.
             *
             * Pipe/TTY/poll must NOT arm blocked_resume(rax=0); they use
             * process_arm_kernel_syscall_sleep only (portable kernel_ret),
             * which is what kernel_syscall_sleep records. Keying solely on
             * syscall_resume_rax == 0 read leftover state from the previous
             * blocking syscall: a non-zero residue routed a woken pipe
             * reader through the user-iret branch below, so it left the read
             * without retrying and reported a short read with bytes still
             * buffered (KTM ring: PIPE_WRITE then no further PIPE_READ).
             *
             * Skip when coop_resched_resume is set: that path armed a real
             * syscall return (possibly after a TTY block that still had
             * sticky kernel_syscall_sleep until clear_in_thread). Disarming
             * it forced iretq with mid-syscall GPRs.
             */
            next_proc->irq_frame_saved = 0;
            next_proc->coop_resched_resume = 0;
        }
        else
        {
        syscall_user_frame_t *frame = &next_proc->syscall_frame;

        /*
         * Direct user transfer does not return through switch_context_x64, so
         * preserve a live cooperative caller first. On its later kernel
         * resume the helper returns non-zero and we unwind this old switch
         * invocation instead of transferring to @next a second time.
         */
        if (next_proc->coop_resched_resume && prev &&
            switch_context_x64(prev, NULL) != 0)
            return;

        /*
         * Why this task went back to ring 3 instead of continuing its
         * syscall in the kernel. Recorded, not emitted: an inline
         * ktm_event_emit4 here perturbs the switch badly enough to create
         * its own failures (see ir0/ktm/deferred.h).
         */
        ktm_deferred_record(KTM_DEFERRED_RESUME_GATE,
                            (uint32_t)next_proc->task.pid,
                            (uint64_t)next_proc->kernel_syscall_sleep,
                            (uint64_t)next_proc->wait_blocked |
                                ((uint64_t)(uint32_t)next_proc->wait_resume_child_pid << 8),
                            next_proc->syscall_resume_rax);

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
            paging_activate_address_space(task_mm_root(next));
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
        arch_repair_user_gprs_from_syscall_frame(next_proc, next);
        next_proc->wait_status_ptr = NULL;
        next_proc->wait_blocked = 0;
        next_proc->wait_target_pid = 0;
        next_proc->wait_options = 0;
        next_proc->wait_resume_child_pid = 0;
        next_proc->irq_frame_saved = 0;
        next_proc->coop_resched_resume = 0;
        next_proc->kernel_syscall_sleep = 0;
        process_kernel_sleep_interrupted_clear(next_proc);
        if (next)
        {
#if IR0_DEBUG_WAIT
            {
                uintptr_t active_cr3_before = paging_current_address_space();
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
                klog_hex64(paging_current_address_space());
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
            klog_hex64(paging_current_address_space());
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
		task_set_kernel_segments(next);
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
             * Still reapply entry GPRs when the frame is fresh — demote alone
             * left mid-syscall heap pointers in RAX/RBP.
             */
            klog_info("CTX", "CLASSIFY KERNEL_CS_USER_RIP_WAIT_DEMOTE");
            next_proc->irq_frame_saved = 0;
            process_restore_user_task_segments(next_proc);
            if (next_proc->syscall_frame_fresh &&
                process_rip_in_user_range(process_syscall_ip(next_proc)) &&
                process_rip_in_user_range(process_syscall_sp(next_proc)))
            {
                process_apply_syscall_frame_to_task(&next_proc->task, sf,
                                                    next_proc->syscall_resume_rax);
            }
        }
        else if (process_rip_in_user_range(process_syscall_ip(next_proc)) &&
                 process_rip_in_user_range(process_syscall_sp(next_proc)) &&
                 !process_rip_in_user_stack(process_syscall_ip(next_proc)))
        {
            uint64_t rax = next_proc->syscall_resume_rax;

            if (rax == 0 && !arch_va_kernel_ptr_leak(task_get_retval(&next_proc->task)))
                rax = task_get_retval(&next_proc->task);
            klog_info("CTX", "CLASSIFY KERNEL_CS_USER_RIP_REPAIR");
			process_apply_syscall_frame_to_task(&next_proc->task, sf, rax);
            if (process_signal_enter_pending(next_proc) &&
                process_saved_context_present(next_proc))
                process_signal_enter_pending_clear(next_proc);
        }
        else
        {
            /*
             * Cannot repair: falling through used to panic as
             * KERNEL_RET_BAD_RIP (ash banner → silent #DF in dump).
             * Demote to user segments so iretq path runs instead.
             */
            klog_info("CTX", "CLASSIFY KERNEL_CS_USER_RIP_UNREPAIRED_DEMOTE");
            next_proc->irq_frame_saved = 0;
            process_restore_user_task_segments(next_proc);
            if (next_proc->syscall_frame_fresh &&
                process_rip_in_user_range(process_syscall_ip(next_proc)) &&
                process_rip_in_user_range(process_syscall_sp(next_proc)))
            {
                process_apply_syscall_frame_to_task(&next_proc->task, sf,
                                                    next_proc->syscall_resume_rax);
            }
        }
    }
#endif

    /*
     * switch_context_x64 loads next CR3 while still on prev's RSP. Keep
     * shared kernel-half PDPT links (kstacks) fresh — asm bypasses
	 * paging_activate_address_space().
     */
    if (next)
    {
	uint64_t next_root = task_mm_root(next);

	if (next_root)
		paging_sync_kernel_mappings((address_space_root_t)next_root);
    }

    if (next_proc && next && arch_will_resume_user_iretq(next_proc, next))
        arch_repair_user_gprs_from_syscall_frame(next_proc, next);

    switch_context_x64(prev, next);
}
