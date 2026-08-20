/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signals.c
 * Description: Default-fatal signal delivery into process exit/zombie path.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

int process_signal_is_default_fatal(process_t *p, int sig)
{
	void (*handler)(int);

	if (!p)
		return 0;
	if (sig == SIGKILL)
		return 1;
	/*
	 * Default action Terminate (Linux signal(7)). Include:
	 * - USR1/USR2 — BusyBox halt/poweroff when not using -f
	 * - SEGV/FPE/ILL/BUS/ABRT — so wait status is WIFSIGNALED (ash
	 *   prints "Segmentation fault", etc.)
	 */
	if (sig != SIGTERM && sig != SIGHUP && sig != SIGINT && sig != SIGQUIT &&
	    sig != SIGUSR1 && sig != SIGUSR2 && sig != SIGSEGV && sig != SIGFPE &&
	    sig != SIGILL && sig != SIGBUS && sig != SIGABRT && sig != SIGALRM)
		return 0;
	if (p->signal_ignored & SIGNAL_MASK(sig))
		return 0;
	handler = p->signal_handlers[sig];
	if (handler && handler != SIG_DFL && handler != SIG_IGN)
		return 0;
	return 1;
}

int process_signal_default_kill(process_t *dying, int sig)
{
	process_t *parent;
	int parent_state_before = -1;

	if (!dying || dying->state == PROCESS_ZOMBIE)
		return 0;
	if (!process_signal_is_default_fatal(dying, sig))
		return 0;

	/*
	 * Self-terminate must use process_exit(): zombieize + schedule away.
	 * Returning to ring 3 after marking current PROCESS_ZOMBIE yields #UD.
	 */
	if (dying == current_process)
	{
		dying->signal_pending &= ~SIGNAL_MASK(sig);
		dying->exit_signal = sig;
		process_exit(0);
	}

	dying->irq_frame_saved = 0;
	process_reap_zombies(dying);
	process_reparent_children(dying);
	process_release_fds(dying, "EXIT_CLOSE");

	dying->signal_pending &= ~SIGNAL_MASK(sig);
	dying->exit_signal = sig;
	dying->exit_code = 0;
	process_mark_zombie(dying);
	sched_remove_process(dying);

#if IR0_DEBUG_PROC
	klog_debug_fmt("SIGNAL", "[SIGTERM_AUDIT] process_signal_default_kill pid=%x sig=%x wait_status=%x", (unsigned)((uint32_t)dying->task.pid), (unsigned)((uint32_t)sig), (unsigned)((uint32_t)process_child_wait_status_word(dying)));
#endif

	if (dying->ppid > 0)
	{
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
		wait_exit_audit_process_exit(dying, parent, parent_state_before);
	}
	else
	{
		wait_exit_audit_process_exit(dying, NULL, -1);
	}

	return 1;
}

