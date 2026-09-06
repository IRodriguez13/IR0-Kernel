/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signals.c
 * Description: Basic signal implementation
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/signals.h>
#include <ir0/console.h>
#include <ir0/abi/signal_contract.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/ktm/klog.h>
#include <ir0/debug_runtime.h>
#include <ir0/clock.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/kmem.h>
#include <ir0/task_ops.h>
#include <ir0/signal_irq.h>
#include <ir0/paging.h>
#include <ir0/tls.h>
#include <ir0/arch_task.h>
#include <ir0/errno.h>
#include <config.h>
#include <kernel/process.h>
#include <string.h>
#include <ktm.h>

static int signal_sp_in_stack(process_t *p, uint64_t sp);
static uint64_t signal_pick_handler_sp(process_t *p, uint64_t saved_sp,
				       uint64_t *frame_addr_out);

static int signal_rip_is_registered_handler(process_t *p, uint64_t rip)
{
	int i;

	if (!p || rip < 0x1000ul)
		return 0;
	for (i = 1; i < _NSIG; i++)
	{
		void (*h)(int) = p->signal_handlers[i];

		if (!h || h == SIG_DFL || h == SIG_IGN)
			continue;
		if ((uint64_t)(uintptr_t)h == rip)
			return 1;
	}
	return 0;
}

static int signal_delivery_blocked_nested(process_t *p)
{
	return process_saved_context_present(p) ||
	       process_signal_enter_pending(p);
}

static void signals_apply_handler_mask(process_t *p, int sig, uint32_t sa_flags)
{
	if (!p || sig < 1 || sig >= _NSIG)
		return;
	if (!p->signal_mask_saved_valid)
	{
		p->signal_mask_saved = p->signal_mask;
		p->signal_mask_saved_valid = 1;
	}
	p->signal_mask |= p->signal_sa_mask[sig];
	if (!(sa_flags & SA_NODEFER))
		p->signal_mask |= SIGNAL_MASK(sig);
}

static void signals_restore_handler_mask(process_t *p)
{
	if (!p || !p->signal_mask_saved_valid)
		return;
	p->signal_mask = p->signal_mask_saved;
	p->signal_mask_saved_valid = 0;
}

void signals_on_sigreturn(process_t *p)
{
	signals_restore_handler_mask(p);
	if (p)
		p->signal_frame_sp = 0;
}

void signals_try_abandon_sigframe(process_t *p)
{
	uint64_t usp;
	uint64_t frame_sp;

	if (!p)
		return;
	frame_sp = p->signal_frame_sp;
	if (frame_sp == 0)
		return;

	/*
	 * Still on the handler stack if SP is at or below the top of the
	 * sigframe (stack grows down). longjmp back to the interrupted site
	 * restores a higher SP — that is the Linux "abandoned trampoline"
	 * case where rt_sigreturn never runs.
	 */
	usp = process_syscall_sp(p);
	if (usp != 0 && usp <= frame_sp + sizeof(struct sigframe) + 128UL)
		return;

	klog_info_fmt("SIGNAL",
		      "SIGFRAME_ABANDON pid=%x usp=%llx frame_sp=%llx "
		      "mask_saved=%x",
		      (unsigned)((uint32_t)p->task.pid),
		      (unsigned long long)usp,
		      (unsigned long long)frame_sp,
		      (unsigned)(p->signal_mask_saved_valid ? 1U : 0U));

	process_saved_context_clear(p);
	process_signal_enter_pending_clear(p);
	/*
	 * Restore pre-delivery mask. Strict Linux leaves the handler mask
	 * after longjmp(); with a full sa_mask (ash sigfillset) that would
	 * mute the process. Restoring matches siglongjmp(... savesigs) and
	 * keeps the process signable.
	 *
	 * Userspace-first: stock BusyBox ash longjmps without rt_sigreturn;
	 * resync console input so GNU/BusyBox need no kernel-specific patches.
	 */
	signals_on_sigreturn(p);
	ir0_console_after_signal_abandon();
}

int signals_has_user_handler(process_t *p, int sig)
{
	void (*handler)(int);

	if (!p || sig < 1 || sig >= _NSIG)
		return 0;
	if (p->signal_ignored & SIGNAL_MASK(sig))
		return 0;
	if (p->signal_mask & SIGNAL_MASK(sig))
		return 0;
	handler = p->signal_handlers[sig];
	if (!handler || handler == SIG_DFL || handler == SIG_IGN)
		return 0;
	if (p->mode == USER_MODE &&
	    !is_user_address((void *)handler, sizeof(void *)))
		return 0;
	return 1;
}

int signals_deliver_from_irq_frame(process_t *p, int sig, uint64_t *frame,
				   uint64_t fault_addr)
{
	void (*handler)(int);
	struct sigcontext *ctx;
	uint64_t new_rsp;
	uint64_t info_addr;
	uint64_t uctx_addr;
	uint32_t sa_flags;
	siginfo_t info;

	if (!signals_has_user_handler(p, sig))
		return 0;
	if (!frame || p->mode != USER_MODE)
		return 0;

	/*
	 * Already in a userspace handler: a nested SEGV must not replace the
	 * outer saved context (that dropped the interrupted setjmp site and
	 * left rdi=signum on resume). Treat nested fault as fatal.
	 */
	if (signal_delivery_blocked_nested(p))
	{
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
		    sig == SIGFPE)
		{
			p->signal_pending &= ~SIGNAL_MASK(sig);
			p->exit_signal = sig;
			process_exit(0);
		}
		return 0;
	}

	handler = p->signal_handlers[sig];
	sa_flags = p->signal_sa_flags[sig];

	ctx = kmalloc(sizeof(*ctx));
	if (!ctx)
		return 0;

	signal_fill_sigcontext_from_irq_frame(ctx, frame);
	if (signal_rip_is_registered_handler(p, ctx->rip))
	{
		kfree(ctx);
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
		    sig == SIGFPE)
		{
			p->signal_pending &= ~SIGNAL_MASK(sig);
			p->exit_signal = sig;
			process_exit(0);
		}
		return 0;
	}
	if (process_saved_context_attach(p, ctx) != 0)
	{
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
		    sig == SIGFPE)
		{
			p->signal_pending &= ~SIGNAL_MASK(sig);
			p->exit_signal = sig;
			process_exit(0);
		}
		return 0;
	}

	/*
	 * Same stack-band policy as handle_signals(): never build the
	 * handler frame in the canary / near USER_STACK_TOP (STACK_TOP_OVERRUN).
	 */
	{
		uint64_t frame_addr;

		new_rsp = signal_pick_handler_sp(p, irq_frame_sp(frame),
						 &frame_addr);
		if (new_rsp == 0)
		{
			process_saved_context_clear(p);
			return 0;
		}
		(void)frame_addr;
	}

	info_addr = 0;
	uctx_addr = 0;

	if (sa_flags & SA_SIGINFO)
	{
		char uctx_zero[128];

		memset(&info, 0, sizeof(info));
		info.si_signo = sig;
		info.si_errno = 0;
		info.si_code = SEGV_MAPERR;
		info._sifields._sigfault.si_addr = (void *)(uintptr_t)fault_addr;

		info_addr = new_rsp - 128;
		uctx_addr = info_addr - 128;
		info_addr &= ~0xFULL;
		uctx_addr &= ~0xFULL;

		if (uctx_addr < 0x400000UL ||
		    !signal_sp_in_stack(p, uctx_addr))
		{
			process_saved_context_clear(p);
			return 0;
		}

		memset(uctx_zero, 0, sizeof(uctx_zero));
		if (copy_to_user_region_in_directory(process_pgd(p), info_addr,
						     &info, sizeof(info)) != 0 ||
		    copy_to_user_region_in_directory(process_pgd(p), uctx_addr,
						     uctx_zero,
						     sizeof(uctx_zero)) != 0)
		{
			process_saved_context_clear(p);
			return 0;
		}
	}

	signal_redirect_irq_frame(frame, (void *)handler, sig, new_rsp,
				       info_addr, uctx_addr,
				       (sa_flags & SA_SIGINFO) ? 1 : 0);
	irq_save_user_frame(frame);

	if (sa_flags & SA_RESETHAND)
		p->signal_handlers[sig] = SIG_DFL;

	p->signal_frame_sp = new_rsp;
	signals_apply_handler_mask(p, sig, sa_flags);
	p->signal_pending &= ~SIGNAL_MASK(sig);

#if SIGNAL_DELIVER_LOG
	klog_info_fmt("SIGNAL",
		      "[SIGNAL][DELIVER] pid=0x%x sig=0x%x cr2=0x%llx rip=0x%llx handler=0x%llx sa_siginfo=0x%x rsp=0x%llx",
		      (unsigned)p->task.pid, (unsigned)sig,
		      (unsigned long long)fault_addr,
		      (unsigned long long)sigcontext_ip(ctx),
		      (unsigned long long)(uintptr_t)handler,
		      (unsigned)((sa_flags & SA_SIGINFO) ? 1U : 0U),
		      (unsigned long long)new_rsp);
#endif

#if defined(CONFIG_KTM_FLIGHT) && CONFIG_KTM_FLIGHT
	{
		uint32_t pid = (uint32_t)p->task.pid;

		KTM_FLIGHT(KTM_FL_PF_USER, pid, (uint32_t)fault_addr,
			   (uint32_t)(fault_addr >> 32),
			   (uint32_t)sigcontext_ip(ctx));
		KTM_FLIGHT(KTM_FL_SIGNAL_DELIVER, (uint32_t)sig, pid,
			   (uint32_t)(uintptr_t)handler, 0);
	}
#endif

	return 1;
}

void signals_reset_on_exec(process_t *p)
{
	int i;

	if (!p)
		return;
	process_saved_context_clear(p);
	p->signal_pending = 0;
	p->signal_mask = 0;
	p->signal_ignored = 0;
	p->signal_mask_saved = 0;
	p->signal_mask_saved_valid = 0;
	p->signal_frame_sp = 0;
	p->it_real_expire_ms = 0;
	p->it_real_interval_ms = 0;
	for (i = 0; i < _NSIG; i++)
	{
		p->signal_handlers[i] = SIG_DFL;
		p->signal_sa_flags[i] = 0;
		p->signal_sa_mask[i] = 0;
		p->signal_restorer[i] = NULL;
	}
}

int signals_pause_should_interrupt(process_t *p)
{
	if (!p || p->signal_pending == 0)
		return 0;

	if (p->signal_pending & SIGNAL_MASK(SIGKILL))
		return 1;

	if ((p->signal_pending & SIGNAL_MASK(SIGTERM)) &&
	    !(p->signal_ignored & SIGNAL_MASK(SIGTERM)) &&
	    !signals_has_user_handler(p, SIGTERM))
		return 1;

	/* Default-terminate hangup must interrupt pause(2) like SIGTERM. */
	if ((p->signal_pending & SIGNAL_MASK(SIGHUP)) &&
	    !(p->signal_ignored & SIGNAL_MASK(SIGHUP)) &&
	    !signals_has_user_handler(p, SIGHUP))
		return 1;

	return (p->signal_pending & ~p->signal_mask) != 0;
}

int signals_should_handle_on_run(process_t *p)
{
	if (!p || p->signal_pending == 0)
		return 0;

	if (p->signal_pending & ~p->signal_mask)
		return 1;

	return signals_pause_should_interrupt(p);
}

/**
 * send_signal - Send signal to process
 */
int send_signal(int pid, int signal)
{
    process_t *proc;

    /* Validate signal number */
    if (signal < 0 || signal >= _NSIG)
    {
        return -1;
    }

    /* Find target process by PID */
    proc = process_find_by_pid(pid);
    if (!proc)
    {
        return -1; /* Process not found */
    }

    /*
     * kill(pid, 0): existence probe only (Linux). Must not set pending,
     * wake blocked tasks, or run default-fatal teardown.
     */
    if (signal == 0)
	return 0;

#if IR0_DEBUG_PROC
    klog_info_fmt("SIGNAL",
                  "[SIGTERM_AUDIT] send_signal pid=0x%x sig=0x%x pending=0x%x mask=0x%x state=0x%x",
                  (unsigned)pid, (unsigned)signal,
                  (unsigned)proc->signal_pending, (unsigned)proc->signal_mask,
                  (unsigned)proc->state);
#endif

    /*
     * Linux: signals to PID 1 are discarded unless init installed a handler
     * (man 2 kill). BusyBox poweroff/reboot without -f send SIGUSR2/SIGTERM
     * to PID 1; runit has no handler — leaving them pending destabilized
     * stage supervision and later showed up as kernel #UD.
     */
    if (proc->task.pid == 1)
    {
	void (*handler)(int) = proc->signal_handlers[signal];

	if (!handler || handler == SIG_DFL || handler == SIG_IGN)
		return 0;
    }

    /*
     * Default-fatal: zombieize immediately. Do not promote BLOCKED→READY
     * first (that raced schedule into a half-dead task → #UD on iret).
     */
    if (process_signal_is_default_fatal(proc, signal))
    {
	if (process_signal_default_kill(proc, signal))
	{
	    clock_request_sched_resched();
	    return 0;
	}
	/* Ignored / caught — fall through to pending delivery. */
    }

    proc->signal_pending |= SIGNAL_MASK(signal);

    /*
     * Wake blocked tasks so caught signals / pause(2) can run handlers.
     */
    if (proc->state == PROCESS_BLOCKED)
    {
	process_set_sched_state(proc, PROCESS_READY);
	sched_promote_process(proc);
	clock_request_sched_resched();
#if IR0_DEBUG_PROC
	klog_info_fmt("SIGNAL",
		      "[SIGTERM_AUDIT] wake pid=0x%x state=READY promote=1",
		      (unsigned)pid);
#endif
    }

#if DEBUG_PROCESS
    klog_info("SIGNAL", "Sent signal to process");
#endif

    return 0;
}

int send_signal_pgrp(int32_t pgid, int signal)
{
	process_t *p;
	int n = 0;

	if (pgid <= 0 || signal < 0 || signal >= _NSIG)
		return 0;

	for (p = process_list; p; p = p->next)
	{
		if ((int32_t)p->pgid != pgid)
			continue;
		if (send_signal((int)p->task.pid, signal) == 0)
			n++;
	}
	return n;
}

void signal_note_syscall_return(process_t *p, int64_t ret)
{
	if (!p || p->mode != USER_MODE)
		return;
	if (!process_saved_context_present(p))
		return;

	task_set_retval(&p->task, (uint64_t)ret);
}

/*
 * Pick a mapped user stack slot for [restorer][sigframe] + handler redzone.
 * When the interrupted SP is near the stack guard (deep call chain in
 * recvfrom → ping), placing the frame below saved SP lands in unmapped
 * pages and syscalls from the handler (clock_gettime in SIGALRM) fail with
 * -EFAULT.
 *
 * Stale syscall_frame SP (outside the process stack VMA) must not be used —
 * BusyBox ping after exec can interrupt with bogus sp≈0xffa… and unmapped
 * handler locals.
 */
static int signal_sp_in_stack(process_t *p, uint64_t sp)
{
	uint64_t lo;
	uint64_t hi;

	if (!p || sp == 0)
		return 0;

	lo = process_stack_start(p);
	hi = lo + process_stack_size(p);
	if (lo == 0 || hi <= lo)
	{
		lo = USER_STACK_BASE;
		hi = USER_STACK_TOP;
	}

	return sp >= lo + 16 && sp < hi;
}

static uint64_t signal_pick_handler_sp(process_t *p, uint64_t saved_sp,
				       uint64_t *frame_addr_out)
{
	uint64_t stack_lo;
	uint64_t stack_hi;
	uint64_t user_sp;
	uint64_t frame_addr;
	uint64_t min_sp;

	if (!p || !frame_addr_out)
		return 0;

	stack_lo = process_stack_start(p);
	stack_hi = stack_lo + process_stack_size(p);
	if (stack_lo == 0 || stack_hi <= stack_lo)
	{
		stack_lo = USER_STACK_BASE;
		stack_hi = USER_STACK_TOP;
	}

	saved_sp &= ~0xFUL;
	min_sp = stack_lo + 2048UL;

	if (!signal_sp_in_stack(p, saved_sp))
		saved_sp = 0;

	user_sp = saved_sp;
	if (user_sp < sizeof(struct sigframe) + 8)
		user_sp = 0;

	/*
	 * Deep call chain left SP near USER_STACK_TOP: building the frame below
	 * saved SP still runs the handler in the canary band (STACK_TOP_OVERRUN).
	 */
	if (user_sp != 0 && user_sp > stack_hi - SIGNAL_HANDLER_TOP_MARGIN)
		user_sp = 0;

	if (user_sp == 0 || user_sp < min_sp)
	{
		uint64_t margin = SIGNAL_HANDLER_TOP_MARGIN;

		if (margin + sizeof(struct sigframe) + 8 + 2048UL >
		    (stack_hi - stack_lo))
			margin = (stack_hi - stack_lo) / 4;

		user_sp = (stack_hi - margin) & ~0xFUL;
		if (user_sp < sizeof(struct sigframe) + 8 + min_sp)
			return 0;
	}
	else
	{
		if (user_sp < sizeof(struct sigframe) + 8)
			return 0;
	}

	user_sp -= sizeof(struct sigframe);
	frame_addr = user_sp;
	user_sp -= 8;

	if (user_sp < min_sp || frame_addr + sizeof(struct sigframe) > stack_hi)
		return 0;
	if (user_sp < 0x400000UL || user_sp > 0x7FFFFFFFFFFFUL)
		return 0;

	*frame_addr_out = frame_addr;
	return user_sp;
}

/**
 * handle_signals - Handle pending signals
 * Called by scheduler before switching to process
 * 
 * Signals are handled in priority order:
 * 1. Unstoppable signals (SIGKILL, SIGSTOP)
 * 2. Error signals (SIGSEGV, SIGFPE, SIGILL, SIGBUS) - terminate process
 * 3. Termination signals (SIGTERM, SIGINT, SIGQUIT, SIGABRT)
 * 4. Other signals
 */
void handle_signals(void)
{
    process_t *current = process_get_current();
    if (!current)
    {
        return;
    }

    /* Check for pending signals */
    if (current->signal_pending == 0)
    {
        return;
    }

    /* SIGKILL - immediate termination, cannot be caught or ignored */
    if (current->signal_pending & SIGNAL_MASK(SIGKILL))
    {
#if DEBUG_PROCESS
        klog_info("SIGNAL", "SIGKILL received, terminating process");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGKILL);
        process_exit(-1);
        return; /* Never returns */
    }

    /* SIGSTOP - stop process (cannot be caught) */
    if (current->signal_pending & SIGNAL_MASK(SIGSTOP))
    {
#if DEBUG_PROCESS
        klog_info("SIGNAL", "SIGSTOP received, stopping process");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGSTOP);
        process_set_sched_state(current, PROCESS_BLOCKED);
        return;
    }

    /* Error signals — terminate unless a userspace handler is registered */
    if (current->signal_pending & SIGNAL_MASK(SIGSEGV))
    {
        if (!signals_has_user_handler(current, SIGSEGV))
        {
	    /*
	     * Pending-bit kill without a live #PF frame (e.g. send_signal from
	     * non-PF path). Still emit a greppable tag so session smokes are
	     * not limited to CONSOLE_SESSION_SEGV.
	     */
	    klog_info_fmt("FAULT",
			  "USER_FAULT_FRAME CLASSIFY SIGNAL_KILL_SIGSEGV "
			  "pid=%x comm=%s",
			  (unsigned)((uint32_t)current->task.pid),
			  current->comm[0] ? current->comm : "(none)");
	    klog_print("USER_FAULT_FRAME\n");
            if (!process_signal_default_kill(current, SIGSEGV))
            {
                current->signal_pending &= ~SIGNAL_MASK(SIGSEGV);
                current->exit_signal = SIGSEGV;
                process_exit(0);
            }
            return;
        }
    }

    if (current->signal_pending & SIGNAL_MASK(SIGFPE))
    {
        if (!signals_has_user_handler(current, SIGFPE))
        {
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGFPE received (arithmetic error), terminating process");
#endif
            if (!process_signal_default_kill(current, SIGFPE))
            {
                current->signal_pending &= ~SIGNAL_MASK(SIGFPE);
                current->exit_signal = SIGFPE;
                process_exit(0);
            }
            return;
        }
    }

    if (current->signal_pending & SIGNAL_MASK(SIGILL))
    {
        if (!signals_has_user_handler(current, SIGILL))
        {
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGILL received (illegal instruction), terminating process");
#endif
            if (!process_signal_default_kill(current, SIGILL))
            {
                current->signal_pending &= ~SIGNAL_MASK(SIGILL);
                current->exit_signal = SIGILL;
                process_exit(0);
            }
            return;
        }
    }

    if (current->signal_pending & SIGNAL_MASK(SIGBUS))
    {
        if (!signals_has_user_handler(current, SIGBUS))
        {
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGBUS received (bus error), terminating process");
#endif
            if (!process_signal_default_kill(current, SIGBUS))
            {
                current->signal_pending &= ~SIGNAL_MASK(SIGBUS);
                current->exit_signal = SIGBUS;
                process_exit(0);
            }
            return;
        }
    }

    /* Termination signals */
    if (current->signal_pending & SIGNAL_MASK(SIGTERM))
    {
#if IR0_DEBUG_PROC
        klog_info_fmt("SIGNAL",
                      "[SIGTERM_AUDIT] handle_signals SIGTERM pending pid=0x%x ignored=0x%x mask=0x%x",
                      (unsigned)current->task.pid,
                      (unsigned)current->signal_ignored,
                      (unsigned)current->signal_mask);
#endif
        if (current->signal_ignored & SIGNAL_MASK(SIGTERM))
        {
            current->signal_pending &= ~SIGNAL_MASK(SIGTERM);
            return;
        }
        if (!signals_has_user_handler(current, SIGTERM))
        {
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGTERM received, terminating process");
#endif
#if IR0_DEBUG_PROC
            klog_info_fmt("SIGNAL",
                          "[SIGTERM_AUDIT] default terminate pid=0x%x exit_signal=15",
                          (unsigned)current->task.pid);
#endif
            current->signal_pending &= ~SIGNAL_MASK(SIGTERM);
            current->exit_signal = SIGTERM;
            process_exit(0);
            return;
        }
    }

    /*
     * SIGINT/SIGQUIT: same contract as SIGTERM. BusyBox ash installs a
     * handler and may raise(SIGINT) after a fg job (^C in nano). Always
     * process_exit(130) killed the login shell → runsv restart loop.
     */
    if (current->signal_pending & SIGNAL_MASK(SIGINT))
    {
        if (current->signal_ignored & SIGNAL_MASK(SIGINT))
        {
            current->signal_pending &= ~SIGNAL_MASK(SIGINT);
        }
        else if (!signals_has_user_handler(current, SIGINT))
        {
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGINT received, terminating process");
#endif
            current->signal_pending &= ~SIGNAL_MASK(SIGINT);
            current->exit_signal = SIGINT;
            process_exit(0);
            return;
        }
        /* else: fall through to userspace handler delivery below */
    }

    if (current->signal_pending & SIGNAL_MASK(SIGHUP))
    {
        if (current->signal_ignored & SIGNAL_MASK(SIGHUP))
        {
            current->signal_pending &= ~SIGNAL_MASK(SIGHUP);
        }
        else if (!signals_has_user_handler(current, SIGHUP))
        {
            klog_info("SIGNAL", "SIGHUP received, terminating process");
            current->signal_pending &= ~SIGNAL_MASK(SIGHUP);
            current->exit_signal = SIGHUP;
            process_exit(0);
            return;
        }
    }

    if (current->signal_pending & SIGNAL_MASK(SIGQUIT))
    {
        if (current->signal_ignored & SIGNAL_MASK(SIGQUIT))
        {
            current->signal_pending &= ~SIGNAL_MASK(SIGQUIT);
        }
        else if (!signals_has_user_handler(current, SIGQUIT))
        {
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGQUIT received, terminating process");
#endif
            current->signal_pending &= ~SIGNAL_MASK(SIGQUIT);
            current->exit_signal = SIGQUIT;
            process_exit(0);
            return;
        }
    }

    if (current->signal_pending & SIGNAL_MASK(SIGABRT))
    {
#if DEBUG_PROCESS
        klog_info("SIGNAL", "SIGABRT received, terminating process");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGABRT);
        current->exit_signal = SIGABRT;
        process_exit(0);
        return;
    }

    /*
     * SIGPIPE: write(2) to a pipe/FIFO with no readers (Linux signal(7)).
     * Queued from sys_write; delivered here before returning to ring 3.
     * Masked: leave pending (no terminate). Ignored: drop. Default: exit.
     * User handler: fall through to delivery below.
     */
    if (current->signal_pending & SIGNAL_MASK(SIGPIPE))
    {
        if (current->signal_ignored & SIGNAL_MASK(SIGPIPE))
        {
            current->signal_pending &= ~SIGNAL_MASK(SIGPIPE);
        }
        else if (current->signal_mask & SIGNAL_MASK(SIGPIPE))
        {
            /* blocked — keep pending */
        }
        else if (!signals_has_user_handler(current, SIGPIPE))
        {
            current->signal_pending &= ~SIGNAL_MASK(SIGPIPE);
            current->exit_signal = SIGPIPE;
            process_exit(0);
            return;
        }
        /* else: fall through to userspace handler delivery below */
    }

    /* SIGCONT - continue if stopped */
    if (current->signal_pending & SIGNAL_MASK(SIGCONT))
    {
#if DEBUG_PROCESS
        klog_info("SIGNAL", "SIGCONT received, resuming process");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGCONT);
        if (current->state == PROCESS_BLOCKED)
        {
            process_set_sched_state(current, PROCESS_READY);
        }
    }

    /*
     * SIGCHLD: do not clear pending here. Ash waits via rt_sigsuspend with a
     * temporary mask that unblocks SIGCHLD; clearing in handle_signals() on
     * schedule-in dropped the notification and left pipeline subshells as
     * zombies while the shell slept forever (P1 wait4/sigsuspend hang).
     * send_signal() already sets pending and wakes BLOCKED parents.
     */

    /* Check for signals with userspace handlers */
    for (int sig = 1; sig < _NSIG; sig++)
    {
        if (current->signal_pending & SIGNAL_MASK(sig))
        {
            /* Check if signal is ignored */
            if (current->signal_ignored & SIGNAL_MASK(sig))
            {
                current->signal_pending &= ~SIGNAL_MASK(sig);
                continue;
            }
            
            /* Check if signal is blocked */
            if (current->signal_mask & SIGNAL_MASK(sig))
            {
                continue; /* Don't deliver blocked signals */
            }
            
            /* Check if there's a userspace handler */
            if (current->signal_handlers[sig] && 
                current->signal_handlers[sig] != SIG_DFL &&
                current->signal_handlers[sig] != SIG_IGN)
            {
                /* Call userspace handler */
                void (*handler)(int) = current->signal_handlers[sig];

		/*
		 * Defer catchable delivery: leave pending for the in-syscall
		 * wait to return -EINTR/-ETIMEDOUT (see signal_defer_catchable).
		 */
		if (process_signal_defer_catchable(current))
			continue;
                
                /* Validate handler is in userspace */
                if (is_user_address((void *)handler, sizeof(void *)))
                {
#if DEBUG_PROCESS
                    klog_info_fmt("SIGNAL",
                                  "Setting up signal frame for signal 0x%x",
                                  (unsigned)sig);
#endif
                    /* Only setup signal frame for USER_MODE processes */
                    if (current->mode == USER_MODE)
                    {
                        struct sigcontext *ctx;
                        struct sigframe frame;
                        uint64_t user_sp;
                        uint64_t frame_addr;
                        uint64_t restorer;
                        void (*restorer_fn)(void);

                        /*
                         * Nested catchable delivery while a sigframe is already
                         * armed corrupts resume (SIGINT handler + nested SEGV
                         * saved rdi=signum, then setjmp #PF at addr 2). Linux
                         * keeps the signal blocked in sa_mask; until we honor
                         * that fully, refuse nested user-handler delivery.
                         */
                        if (signal_delivery_blocked_nested(current))
                        {
                            klog_info_fmt("SIGNAL",
                                          "NESTED_BLOCKED sig=%x outer_rip=%llx",
                                          (unsigned)sig,
                                          (unsigned long long)
                                          (process_saved_context_present(current)
                                           ? sigcontext_ip(
                                               process_saved_context_peek(
                                                 current))
                                           : 0ULL));
                            if (sig == SIGSEGV || sig == SIGBUS ||
                                sig == SIGILL || sig == SIGFPE)
                            {
                                current->signal_pending &= ~SIGNAL_MASK(sig);
                                current->exit_signal = sig;
                                process_exit(0);
                                return;
                            }
                            continue;
                        }

                        ctx = kmalloc(sizeof(struct sigcontext));
                        if (!ctx)
                        {
                            current->signal_handlers[sig] = SIG_DFL;
                            current->signal_pending &= ~SIGNAL_MASK(sig);
                            continue;
                        }

                        /*
                         * Interrupted context: prefer syscall_frame (musl
                         * syscall insn) — task.RSP/RIP are often kernel/stale
                         * while blocked in recvfrom/nanosleep/TTY read.
                         *
                         * When kernel_syscall_sleep is set the task.arch GPRs
                         * are kernel-stack residue; never copy them into ctx.
                         */
                        if (current->kernel_syscall_sleep &&
                            current->syscall_frame_fresh)
                        {
                            signal_fill_sigcontext_from_syscall_frame(
                                ctx, &current->kernel_sleep_syscall_frame,
                                (uint64_t)(int64_t)(-EINTR));
                        }
                        else if (current->syscall_frame_fresh)
                        {
                            signal_fill_sigcontext_from_syscall_frame(
                                ctx, &current->syscall_frame,
                                (uint64_t)(int64_t)(-EINTR));
                            if (ctx->rdi > 0 && ctx->rdi < 64)
                                task_store_sigcontext(ctx, &current->task);
                        }
                        else
                            task_store_sigcontext(ctx, &current->task);

                        if (ctx->rdi < 0x1000ul)
                        {
                            uint64_t snap_rdi = 0;

                            if (current->kernel_syscall_sleep)
                                snap_rdi = syscall_frame_arg(
                                    &current->kernel_sleep_syscall_frame, 0);
                            else if (current->syscall_frame_fresh)
                                snap_rdi = syscall_frame_arg(
                                    &current->syscall_frame, 0);

                            if (snap_rdi >= 0x1000ul)
                                ctx->rdi = snap_rdi;
                            else
                            {
                                uint64_t trdi = task_get_rdi(&current->task);

                                if (trdi >= 0x1000ul)
                                    ctx->rdi = trdi;
                            }
                        }

                        if (current->syscall_entry_nr == 0u &&
                            ctx->rsi < 0x1000ul)
                        {
                            uint64_t snap_rsi = 0;

                            if (current->kernel_syscall_sleep)
                                snap_rsi = syscall_frame_arg(
                                    &current->kernel_sleep_syscall_frame, 1);
                            else if (current->syscall_frame_fresh)
                                snap_rsi = syscall_frame_arg(
                                    &current->syscall_frame, 1);

                            if (snap_rsi >= 0x1000ul)
                                ctx->rsi = snap_rsi;
                        }

                        /*
                         * The saved sigcontext must describe a *user* return
                         * site. A task blocked mid-syscall keeps a kernel
                         * continuation in task.arch (rip in kernel .text, rsp
                         * on the kernel stack, rdi=process_t*). Saving that as
                         * the interrupted context makes rt_sigreturn iretq to
                         * ring3 with a kernel RIP — panic "invalid RIP for
                         * ring3 iretq" (SIGCHLD to a shell blocked in wait4,
                         * delivered on schedule-in before wait4 unwinds).
                         * Defer: leave the signal pending; the block resumes
                         * its kernel continuation, returns to a real user
                         * frame, and delivery retries there.
                         */
                        if (ctx->rip < 0x00400000ULL ||
                            ctx->rip > 0x00007FFFFFFFFFFFULL)
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_DEFER sig=%x "
                                          "reason=nonuser_site rip=%llx",
                                          (unsigned)sig,
                                          (unsigned long long)ctx->rip);
                            kfree(ctx);
                            continue;
                        }

                        if (ctx->rsp < 0x00400000ULL ||
                            ctx->rsp > 0x00007FFFFFFFFFFFULL)
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_DEFER sig=%x "
                                          "reason=nonuser_rsp rsp=%llx",
                                          (unsigned)sig,
                                          (unsigned long long)ctx->rsp);
                            kfree(ctx);
                            continue;
                        }

                        if (signal_rip_is_registered_handler(current,
                                                             ctx->rip))
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_ABORT sig=%x "
                                          "reason=handler_as_site rip=%llx",
                                          (unsigned)sig,
                                          (unsigned long long)ctx->rip);
                            kfree(ctx);
                            if (sig == SIGSEGV || sig == SIGBUS ||
                                sig == SIGILL || sig == SIGFPE)
                            {
                                current->signal_pending &= ~SIGNAL_MASK(sig);
                                current->exit_signal = sig;
                                process_exit(0);
                                return;
                            }
                            continue;
                        }

                        if (process_saved_context_attach(current, ctx) != 0)
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_ABORT sig=%x "
                                          "reason=attach_busy",
                                          (unsigned)sig);
                            if (sig == SIGSEGV || sig == SIGBUS ||
                                sig == SIGILL || sig == SIGFPE)
                            {
                                current->signal_pending &= ~SIGNAL_MASK(sig);
                                current->exit_signal = sig;
                                process_exit(0);
                                return;
                            }
                            continue;
                        }

                        user_sp = signal_pick_handler_sp(current,
                            sigcontext_sp(ctx), &frame_addr);
                        if (user_sp == 0)
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_ABORT sig=%x reason=bad_sp",
                                          (unsigned)sig);
                            process_saved_context_clear(current);
                            current->signal_handlers[sig] = SIG_DFL;
                            current->signal_pending &= ~SIGNAL_MASK(sig);
                            continue;
                        }

                        restorer_fn = current->signal_restorer[sig];
                        if (!restorer_fn ||
                            !is_user_address((void *)restorer_fn,
                                             sizeof(void *)))
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_ABORT sig=%x reason=no_restorer",
                                          (unsigned)sig);
                            process_saved_context_clear(current);
                            break;
                        }
                        restorer = (uint64_t)(uintptr_t)restorer_fn;

                        frame.handler = handler;
                        frame.signum = sig;
                        frame.ctx = *ctx;
                        frame.oldmask = (uint64_t)current->signal_mask;

                        if (copy_to_user_region_in_directory(
				    process_pgd(current), frame_addr, &frame,
				    sizeof(struct sigframe)) != 0 ||
			    copy_to_user_region_in_directory(
				    process_pgd(current), user_sp, &restorer,
				    sizeof(restorer)) != 0)
                        {
                            klog_info_fmt("SIGNAL",
                                          "DELIVER_ABORT sig=%d reason=copy",
                                          sig);
                            process_saved_context_clear(current);
                            break;
                        }

                        current->signal_frame_sp = user_sp;

                        signal_prepare_task_handler(&current->task,
                                                         (void *)handler, sig,
                                                         user_sp);

                        klog_info_fmt("SIGNAL",
                                      "DELIVER_CTX sig=%d saved_rip=%llx "
                                      "saved_rdi=%llx saved_rsp=%llx "
                                      "handler=%llx",
                                      sig,
                                      (unsigned long long)ctx->rip,
                                      (unsigned long long)ctx->rdi,
                                      (unsigned long long)ctx->rsp,
                                      (unsigned long long)(uintptr_t)handler);

                        /*
                         * Backup the real syscall entry frame before redirecting
                         * to the handler. rt_sigreturn uses it when the signal
                         * interrupted kernel_syscall_sleep (TTY/pipe block).
                         */
                        process_kernel_sleep_interrupted_backup_frame(current);

                        /*
                         * Always redirect syscall_frame to the handler so
                         * Class B repair / irq_frame resume cannot iretq to a
                         * stale recvfrom RIP. Then arm coop user-iret (not
                         * kernel_ret): REPAIR alone has raced with FS=0 and
                         * SEGV at low TLS offsets (BusyBox ping SIGALRM).
                         */
                        process_syscall_set_ip(
                            current, (uint64_t)(uintptr_t)handler);
                        process_syscall_set_sp(current, user_sp);
                        process_syscall_set_arg(
                            current, 0, (uint64_t)(uint32_t)sig);
                        process_syscall_set_arg(current, 1, 0);
                        process_syscall_set_arg(current, 2, 0);
                        if (!process_syscall_flags(current))
                            process_syscall_set_flags(current,
                                                      (uint64_t)RFLAGS_IF);

                        current->want_kernel_ret = 0;
                        process_apply_syscall_frame_to_task(
                            &current->task, &current->syscall_frame,
                            0);
                        /*
                         * syscall_frame now mirrors the handler (Class B /
                         * leak-repair safety). It must NOT stay "fresh" as a
                         * syscall interrupt site: a later handle_signals()
                         * fill would save rdi=signum + rip=handler as the
                         * interrupted context.
                         */
                        current->syscall_frame_fresh = 0;
                        restore_user_fs_base();
                        signals_apply_handler_mask(
                            current, sig, current->signal_sa_flags[sig]);
                        process_signal_enter_pending_set(current);
                        process_signal_last_delivered_set(current, sig);

                        current->signal_pending &= ~SIGNAL_MASK(sig);

#if DEBUG_PROCESS
                        klog_info("SIGNAL",
                                  "Signal frame set up (restorer+syscall)");
#endif
                    }
                    else
                    {
                        /* KERNEL_MODE: call directly (for dbgshell) */
                        current->signal_pending &= ~SIGNAL_MASK(sig);
                        handler(sig);
                    }
                }
                else
                {
                    /* Invalid handler address - use default */
                    current->signal_handlers[sig] = SIG_DFL;
                    current->signal_pending &= ~SIGNAL_MASK(sig);
                }
            }
        }
    }
    
    /* SIGALRM, SIGUSR1, SIGUSR2, SIGTRAP - handled above or default */
}

/**
 * register_signal_handler - Register a signal handler for current process
 */
int register_signal_handler(int signal, void (*handler)(int))
{
    if (signal < 1 || signal >= _NSIG)
        return -1;
    
    /* Signals that cannot be caught */
    if (signal == SIGKILL || signal == SIGSTOP)
        return -1;
    
    process_t *current = process_get_current();
    if (!current)
        return -1;
    
    /* Validate handler is in userspace (for USER_MODE processes) */
    if (current->mode == USER_MODE && handler != SIG_DFL && handler != SIG_IGN)
    {
        if (!is_user_address((void *)handler, sizeof(void *)))
            return -1; /* Invalid handler address */  
    }
    
    current->signal_handlers[signal] = handler;
    
    /* If handler is SIG_IGN, add to ignored mask */
    if (handler == SIG_IGN)
    {
        current->signal_ignored |= SIGNAL_MASK(signal);
    }
    else
    {
        current->signal_ignored &= ~SIGNAL_MASK(signal);
    }
    
    return 0;
}

/**
 * signal_ignore - Ignore a signal for current process
 */
int signal_ignore(int signal)
{
    if (signal < 1 || signal >= _NSIG)
        return -1;
    
    /* Signals that cannot be ignored */
    if (signal == SIGKILL || signal == SIGSTOP)
        return -1;
    
    return register_signal_handler(signal, SIG_IGN);
}
