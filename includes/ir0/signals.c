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
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/ktm/klog.h>
#include <ir0/debug_runtime.h>
#include <ir0/clock.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/kmem.h>
#include <ir0/arch_task_ops.h>
#include <ir0/arch_signal.h>
#include <config.h>
#include <string.h>
#include <ktm.h>

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

	handler = p->signal_handlers[sig];
	sa_flags = p->signal_sa_flags[sig];

	if (p->saved_context)
	{
		kfree(p->saved_context);
		p->saved_context = NULL;
	}

	ctx = kmalloc(sizeof(*ctx));
	if (!ctx)
		return 0;

	arch_signal_fill_sigcontext_from_irq_frame(ctx, frame);
	p->saved_context = ctx;

	new_rsp = arch_irq_frame_sp(frame);
	if (sa_flags & SA_SIGINFO)
		new_rsp -= 256;
	else
		new_rsp -= 128;
	new_rsp &= ~0xFULL;

	if (new_rsp < 0x400000UL || new_rsp > 0x7FFFFFFFFFFFUL)
	{
		kfree(ctx);
		p->saved_context = NULL;
		return 0;
	}

	info_addr = 0;
	uctx_addr = 0;

	if (sa_flags & SA_SIGINFO)
	{
		uint64_t old_cr3;
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

		if (uctx_addr < 0x400000UL)
		{
			kfree(ctx);
			p->saved_context = NULL;
			return 0;
		}

		memset(uctx_zero, 0, sizeof(uctx_zero));
		old_cr3 = get_current_page_directory();
		load_page_directory((uint64_t)process_pgd(p));
		memcpy((void *)info_addr, &info, sizeof(info));
		memcpy((void *)uctx_addr, uctx_zero, sizeof(uctx_zero));
		load_page_directory(old_cr3);
	}

	arch_signal_redirect_irq_frame(frame, (void *)handler, sig, new_rsp,
				       info_addr, uctx_addr,
				       (sa_flags & SA_SIGINFO) ? 1 : 0);
	irq_save_user_frame(frame);

	if (sa_flags & SA_RESETHAND)
		p->signal_handlers[sig] = SIG_DFL;

	p->signal_pending &= ~SIGNAL_MASK(sig);

#if SIGNAL_DELIVER_LOG
	klog_info_fmt("SIGNAL",
		      "[SIGNAL][DELIVER] pid=0x%x sig=0x%x cr2=0x%llx rip=0x%llx handler=0x%llx sa_siginfo=0x%x rsp=0x%llx",
		      (unsigned)p->task.pid, (unsigned)sig,
		      (unsigned long long)fault_addr,
		      (unsigned long long)arch_sigcontext_ip(ctx),
		      (unsigned long long)(uintptr_t)handler,
		      (unsigned)((sa_flags & SA_SIGINFO) ? 1U : 0U),
		      (unsigned long long)new_rsp);
#endif

#if defined(CONFIG_KTM_FLIGHT) && CONFIG_KTM_FLIGHT
	{
		uint32_t pid = (uint32_t)p->task.pid;

		KTM_FLIGHT(KTM_FL_PF_USER, pid, (uint32_t)fault_addr,
			   (uint32_t)(fault_addr >> 32),
			   (uint32_t)arch_sigcontext_ip(ctx));
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
	if (p->saved_context)
	{
		kfree(p->saved_context);
		p->saved_context = NULL;
	}
	p->signal_pending = 0;
	p->signal_mask = 0;
	p->signal_ignored = 0;
	p->it_real_expire_ms = 0;
	p->it_real_interval_ms = 0;
	for (i = 0; i < _NSIG; i++)
	{
		p->signal_handlers[i] = SIG_DFL;
		p->signal_sa_flags[i] = 0;
		p->signal_sa_mask[i] = 0;
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
#if DEBUG_PROCESS
            klog_info("SIGNAL", "SIGSEGV received (segmentation fault), terminating process");
#endif
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

    /* SIGCHLD - child process terminated (handled by parent, clear here) */
    if (current->signal_pending & SIGNAL_MASK(SIGCHLD))
    {
#if DEBUG_PROCESS
        klog_info("SIGNAL", "SIGCHLD received (child terminated)");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGCHLD);
        if (current->state == PROCESS_BLOCKED)
            process_set_sched_state(current, PROCESS_READY);
    }

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
                        /* Save current context */
                        struct sigcontext *ctx = kmalloc(sizeof(struct sigcontext));
                        if (!ctx)
                        {
                            /* Out of memory - fall back to default handler */
                            current->signal_handlers[sig] = SIG_DFL;
                            current->signal_pending &= ~SIGNAL_MASK(sig);
                            continue;
                        }
                        
                        /* Save CPU state from task structure */
                        arch_task_store_sigcontext(ctx, &current->task);
                        
                        /* Store saved context in process */
                        current->saved_context = ctx;
                        
                        /* Allocate signal frame on userspace stack */
                        uint64_t frame_addr = task_get_sp(&current->task) -
                                              sizeof(struct sigframe);
                        frame_addr &= ~0xF;  /* Align to 16 bytes (ABI requirement) */
                        
                        /* Ensure frame is in userspace */
                        if (frame_addr < 0x400000UL || frame_addr > 0x7FFFFFFFFFFFUL)
                        {
                            /* Invalid stack - fall back to default handler */
                            kfree(ctx);
                            current->saved_context = NULL;
                            current->signal_handlers[sig] = SIG_DFL;
                            current->signal_pending &= ~SIGNAL_MASK(sig);
                            continue;
                        }
                        
                        /* Setup signal frame */
                        struct sigframe frame;
                        frame.handler = handler;
                        frame.signum = sig;
                        frame.ctx = *ctx;
                        
                        /* Copy frame to userspace stack */
                        uint64_t old_cr3 = get_current_page_directory();
                        load_page_directory((uint64_t)process_pgd(current));
                        memcpy((void *)frame_addr, &frame, sizeof(struct sigframe));
                        load_page_directory(old_cr3);
                        
                        arch_signal_prepare_task_handler(&current->task,
                                                         (void *)handler, sig,
                                                         frame_addr);

                        /* Clear signal before calling handler */
                        current->signal_pending &= ~SIGNAL_MASK(sig);
                        
#if DEBUG_PROCESS
                        klog_info("SIGNAL", "Signal frame set up, handler will be called");
#endif
                        /* Handler will be invoked on next context switch */
                        /* When handler returns, it will call sigreturn() syscall */
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
