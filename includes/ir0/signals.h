/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signals.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only


#ifndef _IR0_SIGNALS_H
#define _IR0_SIGNALS_H

#include <stdint.h>
#include <stddef.h>
#include <ir0/arch_config.h>

typedef struct process process_t;


/* Standard Unix signals - essential set for error handling */
/* Hardware/CPU exceptions */
#define SIGSEGV  11   /* Segmentation violation (invalid memory access) */
#define SIGFPE    8   /* Floating point exception (divide by zero, overflow) */
#define SIGILL    4   /* Illegal instruction */
#define SIGBUS    7   /* Bus error (misaligned memory access) */
#define SIGTRAP   5   /* Trace/breakpoint trap */

/* Termination signals */
#define SIGKILL   9   /* Kill process (cannot be caught or ignored) */
#define SIGTERM  15   /* Termination signal (can be caught) */
#define SIGINT    2   /* Interrupt from keyboard (Ctrl+C) */
#define SIGHUP    1   /* Hangup on controlling terminal */
#define SIGQUIT   3   /* Quit from keyboard (Ctrl+\\) */

/* Process control */
#define SIGCHLD  17   /* Child process terminated or stopped */
#define SIGSTOP  19   /* Stop process (cannot be caught) */
#define SIGCONT  18   /* Continue if stopped */

/* Other */
#define SIGABRT   6   /* Abort signal (from abort()) */
#define SIGALRM  14   /* Timer signal (from alarm()) */
#define SIGUSR1  10   /* User-defined signal 1 */
#define SIGUSR2  12   /* User-defined signal 2 */
#define SIGWINCH 28   /* Window size change (TTY) */

/* Signal bitmask helpers */
#define SIGNAL_MASK(sig) (1U << (sig))

/* Maximum signal number */
#define _NSIG 32

/* sigset_t — Linux uapi layout (128 bytes on x86-64). */
#define _IR0_SIGSET_WORDS 16

typedef struct
{
	unsigned long __val[_IR0_SIGSET_WORDS];
} sigset_t;

/* rt_sigprocmask how argument */
#define SIG_BLOCK     0  /* Add signals to mask */
#define SIG_UNBLOCK   1  /* Remove signals from mask */
#define SIG_SETMASK   2  /* Replace mask */

/* Special signal handler values */
#define SIG_DFL ((void (*)(int))0)  /* Default handler */
#define SIG_IGN ((void (*)(int))1)  /* Ignore signal */
#define SIG_ERR ((void (*)(int))-1) /* Error return */

#define SA_SIGINFO    4
#define SA_RESTORER   0x04000000U
#define SA_RESTART    0x10000000U
#define SA_RESETHAND  0x80000000U

#ifndef SIGNAL_DELIVER_LOG
#define SIGNAL_DELIVER_LOG 0
#endif

/* Linux uapi si_code for SIGSEGV (subset). */
#define SEGV_MAPERR   1

/*
 * siginfo_t — Linux/musl layout (128 bytes on x86-64).
 * Only fault fields used by D1.2 SIGSEGV delivery.
 */
typedef struct
{
	int si_signo;
	int si_errno;
	int si_code;
	union
	{
		char _pad[128 - 3 * (int)sizeof(int)];
		struct
		{
			void *si_addr;
		} _sigfault;
	} _sifields;
} siginfo_t;

/**
 * struct sigaction — Linux rt_sigaction layout (musl-compatible size).
 */
struct sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};

/*
 * ISA-specific sigcontext (Linux uapi: x86-64 and aarch64).
 * Portable code: arch_task_load/store_sigcontext / arch_signal_* only.
 */
#if defined(ARCH_ARM64) || defined(__aarch64__)
#include <ir0/sigcontext_arm64.h>
#else
#include <ir0/sigcontext_x86_64.h>
#endif

/**
 * struct sigframe - Signal frame on userspace stack
 * Complete context saved when signal handler is invoked.
 *
 * Layout on stack (low→high): [restorer][sigframe…]. Handler entry RSP must
 * be 8 mod 16 (SysV). sizeof must be 0 mod 16 so that after aligning RSP,
 * subtracting this frame and the 8-byte restorer leaves RSP ≡ 8; otherwise
 * musl/glibc movaps in rt_sigaction/setitimer #GP (BusyBox ping SIGALRM).
 */
struct sigframe {
    void (*handler)(int);     /* Signal handler function */
    int signum;               /* Signal number */
    int __pad0;
    struct sigcontext ctx;    /* Saved CPU context */
    uint64_t __align16;       /* pad to multiple of 16 */
};

_Static_assert((sizeof(struct sigframe) % 16) == 0,
	       "sigframe size must be 16-byte multiple for handler RSP ABI");

/**
 * send_signal - Send a signal to a process
 * @pid: Target process ID
 * @signal: Signal number to send
 *
 * Returns: 0 on success, -1 on error
 */
int send_signal(int pid, int signal);

/**
 * send_signal_pgrp - Deliver @signal to every live process with pgid == @pgid.
 * Returns count of processes signaled (≥0).
 */
int send_signal_pgrp(int32_t pgid, int signal);

/**
 * handle_signals - Check and handle pending signals for current process
 *
 * Called by scheduler before context switch
 */
void handle_signals(void);

/*
 * True when @p has a pending signal that should interrupt pause(2) or run
 * handle_signals() even if the signal is blocked in signal_mask (default
 * termination for SIGKILL / SIGTERM).
 */
int signals_pause_should_interrupt(process_t *p);

/*
 * True when handle_signals() should run on context switch to @p (includes
 * default SIGTERM/SIGKILL even if blocked in signal_mask).
 */
int signals_should_handle_on_run(process_t *p);

/**
 * register_signal_handler - Register a signal handler for current process
 * @signal: Signal number
 * @handler: Handler function pointer (userspace address)
 *
 * Returns: 0 on success, -1 on error
 */
int register_signal_handler(int signal, void (*handler)(int));

/**
 * signal_ignore - Ignore a signal for current process
 * @signal: Signal number to ignore
 *
 * Returns: 0 on success, -1 on error
 */
int signal_ignore(int signal);

/*
 * Returns 1 if @sig has a deliverable userspace handler (not DFL/IGN/blocked).
 */
int signals_has_user_handler(process_t *p, int sig);

/** POSIX exec: reset handlers, mask, pending; drop saved sigreturn context. */
void signals_reset_on_exec(process_t *p);

/*
 * Deliver @sig synchronously from a user #PF IRQ @frame (isr_common_stub layout).
 * @fault_addr is CR2 (stored in siginfo for SA_SIGINFO).
 * Returns 1 if redirected to handler; 0 if caller should terminate/default.
 */
int signals_deliver_from_irq_frame(process_t *p, int sig, uint64_t *frame,
				   uint64_t fault_addr);

static inline uint32_t ir0_sigset_low32(const sigset_t *set)
{
	if (!set)
		return 0;
	return (uint32_t)(set->__val[0] & 0xFFFFFFFFUL);
}

static inline void ir0_sigset_set_low32(sigset_t *set, uint32_t mask)
{
	size_t i;

	if (!set)
		return;

	for (i = 0; i < _IR0_SIGSET_WORDS; i++)
		set->__val[i] = 0;
	set->__val[0] = (unsigned long)mask;
}

#endif /* _IR0_SIGNALS_H */
