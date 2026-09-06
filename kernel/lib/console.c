/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console.c
 * Description: IR0 kernel source — console
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/klog.h>
#include <ir0/copy_user.h>
#include <ir0/console.h>
#include <ir0/paging.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/console_backend.h>
#include <ir0/ash_smoke.h>
#include <d1_12_read_diag.h>
#include <d1_16_tty_read_diag.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <ir0/video_console.h>
#include <ir0/kernel.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/signals.h>
#include <ir0/clock.h>
#include <ir0/clock_wait.h>
#include <string.h>

/* TCGETS/TCSETS ABI must stay Linux kernel termios (36 bytes, NCCS=19). */
_Static_assert(sizeof(struct ir0_termios) == 36,
	       "ir0_termios must match Linux uapi termios for TCGETS");

extern void kernel_idle_poll(void);

#define IR0_TTY_MAX_READ_WAITERS 8
#define IR0_TTY_ECHO_COLOR       0x07u
#define IR0_TTY_CANON_MAX        256

static process_t *tty_read_waiters[IR0_TTY_MAX_READ_WAITERS];
static struct ir0_termios tty_termios;
static int tty_termios_ready;
static int tty_userspace_attached;
static int tty_need_resched;
static int tty_sleep_depth;
/* Foreground pgrp for /dev/console (TIOCSPGRP); 0 → signal TTY waiters only. */
static int32_t console_fg_pgid;
/* Soft winsize from TIOCSWINSZ (0 → report renderer geometry). */
static uint16_t soft_ws_row;
static uint16_t soft_ws_col;

static char canon_line[IR0_TTY_CANON_MAX];
static size_t canon_line_len;

static char canon_readq[IR0_TTY_CANON_MAX + 1];
static size_t canon_readq_len;
static size_t canon_readq_pos;
/* Set when ICANON VEOF arrives on an empty line — next read returns 0. */
static int canon_eof_pending;

static void tty_termios_ensure(void)
{
	if (!tty_termios_ready)
	{
		memset(&tty_termios, 0, sizeof(tty_termios));
		tty_termios.c_iflag = IR0_CONSOLE_IFLAG_DEFAULT;
		tty_termios.c_oflag = IR0_CONSOLE_OFLAG_DEFAULT;
		tty_termios.c_cflag = IR0_CONSOLE_CFLAG_DEFAULT;
		tty_termios.c_lflag = IR0_CONSOLE_LFLAG_DEFAULT;
		tty_termios.c_line = 0;
		tty_termios.c_cc[IR0_CC_VINTR] = 3;   /* Ctrl+C */
		tty_termios.c_cc[IR0_CC_VQUIT] = 28;  /* Ctrl+\ */
		tty_termios.c_cc[IR0_CC_VERASE] = 127;
		tty_termios.c_cc[IR0_CC_VEOF] = 4;    /* Ctrl+D */
		tty_termios.c_cc[IR0_CC_VTIME] = 0;
		tty_termios.c_cc[IR0_CC_VMIN] = 1;
		tty_termios_ready = 1;
	}
}

static int tty_isig_on(void)
{
	tty_termios_ensure();
	return (tty_termios.c_lflag & IR0_LFLAG_ISIG) ? 1 : 0;
}

/*
 * Deliver INTR/QUIT to the console foreground group, or to blocked readers
 * when job-control has not installed a fg pgid yet (ash without tcsetpgrp).
 */
static void tty_deliver_sig(int sig)
{
	int i;
	int n = 0;

	if (console_fg_pgid > 1)
		n = send_signal_pgrp(console_fg_pgid, sig);

	/*
	 * Blocked stdin readers (hexdump, cat, ash lineedit) must see VINTR even
	 * when console_fg_pgid lags tcsetpgrp after SIGCHLD or a short-lived fg
	 * job — Linux job control can leave the tty waiter outside a stale pgid.
	 */
	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		process_t *w = tty_read_waiters[i];

		if (!w || w->task.pid <= 1)
			continue;
		if (send_signal((int)w->task.pid, sig) == 0)
			n++;
	}

	if (n > 0)
		return;

	/*
	 * No fg pgrp and no blocked tty readers: deliver to the running task
	 * (e.g. cat busy in write, not read).
	 */
	if (current_process && current_process->task.pid > 1)
		(void)send_signal((int)current_process->task.pid, sig);
}

int ir0_console_set_fg_pgid(int32_t pgid)
{
	if (pgid <= 1)
		return -EINVAL;
	console_fg_pgid = pgid;
	return 0;
}

void ir0_console_clear_fg_pgid(int32_t pgid)
{
	if (pgid > 1 && console_fg_pgid == pgid)
		console_fg_pgid = 0;
}

int ir0_console_ioctl_set_ctty(void)
{
	int32_t pgid;

	if (!current_process)
		return -ESRCH;

	/*
	 * Linux TIOCSCTTY requires the caller to be a session leader; the
	 * console is the only tty of the boot session, so binding it means
	 * adopting the caller's process group as foreground.
	 *
	 * ARCH_DEBT: the console keeps no per-session ctty state, so the
	 * "already controlling terminal of another session" case (-EPERM
	 * without the force argument) cannot be detected yet.
	 */
	if (current_process->sid != (pid_t)current_process->task.pid)
		return -EPERM;

	pgid = current_process->pgid > 0 ? (int32_t)current_process->pgid
					: (int32_t)current_process->task.pid;
	(void)ir0_console_set_fg_pgid(pgid);
	return 0;
}

int32_t ir0_console_get_fg_pgid(void)
{
	if (console_fg_pgid > 0)
		return console_fg_pgid;
	if (current_process)
	{
		if (current_process->pgid > 0)
			return (int32_t)current_process->pgid;
		return (int32_t)current_process->task.pid;
	}
	return 1;
}

static int tty_echo_on(void)
{
	tty_termios_ensure();
	return (tty_termios.c_lflag & IR0_LFLAG_ECHO) ? 1 : 0;
}

static int tty_icanon_on(void)
{
	tty_termios_ensure();
	return (tty_termios.c_lflag & IR0_LFLAG_ICANON) ? 1 : 0;
}

static char tty_normalize_input(char c)
{
	tty_termios_ensure();
	/*
	 * ICRNL maps CR→NL for cooked lines. In non-canonical/raw mode
	 * (nano/ncurses), deliver CR as 0x0d even if ICRNL is stale — apps
	 * expect Enter = '\r' after cfmakeraw().
	 */
	if (c == '\r' && (tty_termios.c_iflag & IR0_IFLAG_ICRNL) &&
	    tty_icanon_on())
		return '\n';
	return c;
}

static void tty_echo_char(char c)
{
	char echo_buf[4];
	size_t echo_len = 0;

	if (!tty_echo_on())
		return;

	if (c == '\b' || c == 127)
	{
		echo_buf[echo_len++] = '\b';
		echo_buf[echo_len++] = ' ';
		echo_buf[echo_len++] = '\b';
	}
	else if (c == '\r')
	{
		if (tty_termios.c_iflag & IR0_IFLAG_ICRNL)
			echo_buf[echo_len++] = '\n';
		else
			echo_buf[echo_len++] = '\r';
	}
	else if (c == '\n' || c == '\t' || (unsigned char)c >= ' ')
	{
		echo_buf[echo_len++] = c;
	}

	if (echo_len > 0)
		console_backend_write(echo_buf, echo_len, IR0_TTY_ECHO_COLOR);
}

static void tty_canon_erase(void)
{
	if (!tty_echo_on())
		return;
	if (!(tty_termios.c_lflag & IR0_LFLAG_ECHOE))
		return;
	tty_echo_char('\b');
}

static void tty_canon_drain(char *kbuf, size_t count, size_t *out_len)
{
	size_t avail;
	size_t n;

	if (canon_readq_pos >= canon_readq_len)
	{
		canon_readq_len = 0;
		canon_readq_pos = 0;
		return;
	}

	avail = canon_readq_len - canon_readq_pos;
	n = avail;
	if (n > count - *out_len)
		n = count - *out_len;
	if (n == 0)
		return;

	memcpy(kbuf + *out_len, canon_readq + canon_readq_pos, n);
	canon_readq_pos += n;
	*out_len += n;

	if (canon_readq_pos >= canon_readq_len)
	{
		canon_readq_len = 0;
		canon_readq_pos = 0;
	}
}

/*
 * Returns 1 when a completed canonical line is queued in canon_readq.
 */
static int tty_canon_feed(char c)
{
	size_t i;
	unsigned char erase;

	tty_termios_ensure();
	erase = tty_termios.c_cc[IR0_CC_VERASE];
	if (erase == 0)
		erase = 127;

	c = tty_normalize_input(c);
	/* Defensive: treat bare CR as EOL under ICANON (Enter from PS/2). */
	if (c == '\r')
		c = '\n';

	if (c == '\b' || c == 127 || (unsigned char)c == erase)
	{
		if (canon_line_len > 0)
		{
			canon_line_len--;
			tty_canon_erase();
		}
		return 0;
	}

	if (c == '\n')
	{
		/*
		 * Linux ECHONL: echo NL even when ECHO is clear (password prompts).
		 * Without this, "Password:" leaves the cursor on the same line.
		 */
		if (tty_echo_on() || (tty_termios.c_lflag & IR0_LFLAG_ECHONL))
		{
			char nl = '\n';

			console_backend_write(&nl, 1, IR0_TTY_ECHO_COLOR);
		}
		canon_readq_len = 0;
		canon_readq_pos = 0;
		for (i = 0; i < canon_line_len; i++)
			canon_readq[canon_readq_len++] = canon_line[i];
		canon_readq[canon_readq_len++] = '\n';
		canon_line_len = 0;
		d1_12_read_diag_tty_line(canon_readq_len - 1, canon_readq,
					 canon_readq_len);
		d1_16_tty_line_ready((uintptr_t)(void *)&canon_readq,
				     canon_readq_len);
		return 1;
	}

	/* VEOF (Ctrl+D): empty line → EOF; else push partial line without NL. */
	if ((unsigned char)c == tty_termios.c_cc[IR0_CC_VEOF] ||
	    (unsigned char)c == 4)
	{
		canon_readq_len = 0;
		canon_readq_pos = 0;
		if (canon_line_len == 0)
		{
			canon_eof_pending = 1;
			return 1;
		}
		for (i = 0; i < canon_line_len; i++)
			canon_readq[canon_readq_len++] = canon_line[i];
		canon_line_len = 0;
		return 1;
	}

	if (canon_line_len + 1 >= IR0_TTY_CANON_MAX)
		return 0;

	canon_line[canon_line_len++] = c;
	if (tty_echo_on())
		tty_echo_char(c);
	return 0;
}

static inline uint64_t tty_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void tty_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

static int tty_waiter_count(void)
{
	int i;
	int n = 0;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i])
			n++;
	}
	return n;
}

static void tty_waiter_remove(process_t *p)
{
	int i;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == p)
			tty_read_waiters[i] = NULL;
	}
}

static int tty_waiter_register(process_t *p)
{
	int i;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == p)
			return 1;
	}

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == NULL)
		{
			tty_read_waiters[i] = p;
			return 1;
		}
	}

	return 0;
}

/*
 * Linux-like prepare_to_wait: register on the TTY wait queue, re-check the
 * canonical line under IRQ mask, then block.  Without the re-check, a keyboard
 * IRQ can complete the line and wake the reader between registration and
 * PROCESS_BLOCKED, leaving the task blocked with no waiter and data ready.
 */
static int tty_sleep_for_input(void)
{
	process_t *proc = current_process;
	uint64_t flags;
	int blocked_once = 0;
	int prev_state;

	if (!proc)
		return 0;

	if (!tty_waiter_register(proc))
	{
		/*
		 * Table full. Returning here without draining left
		 * tty_read_kernel spinning: it never polled the keyboard and
		 * never yielded, so getty printed its prompt and then ignored
		 * every keystroke. Make progress instead of busy-looping.
		 */
		static unsigned int full_hits;

		if (++full_hits <= 8)
			klog_notice_fmt("TTY",
					"[TTY] read-waiter table full (n=%u pid=%u)\n",
					full_hits, (unsigned int)proc->task.pid);
		enable_interrupts();
		kernel_idle_poll();
		sched_schedule_next();
		return 0;
	}

	d1_16_tty_read_block(proc, tty_waiter_count(), "tty_input");
	tty_sleep_depth++;

	for (;;)
	{
		if (ir0_console_input_ready())
		{
			tty_waiter_remove(proc);
			process_clear_in_thread_syscall_block(proc);
			d1_16_tty_read_resume(proc, "input_ready");
			tty_sleep_depth--;
			return 1;
		}

		flags = tty_irq_save();
		if (ir0_console_input_ready())
		{
			tty_irq_restore(flags);
			tty_waiter_remove(proc);
			process_clear_in_thread_syscall_block(proc);
			d1_16_tty_read_resume(proc, "input_ready_irq");
			tty_sleep_depth--;
			return 1;
		}

		prev_state = proc->state;
		/*
		 * Stay in the syscall (kernel_ret), do not arm user-iretq with
		 * rax=0. Staging a user frame here caused login to hang after
		 * Enter: echo ran, but read() never returned → no Password:.
		 */
		if (proc->mode == USER_MODE)
			process_arm_kernel_syscall_sleep(proc);
		if (proc->state != PROCESS_READY)
		{
			process_set_sched_state(proc, PROCESS_BLOCKED);
			blocked_once = 1;
			d1_16_tty_state_transition(proc, prev_state,
						   PROCESS_BLOCKED);
		}
		tty_irq_restore(flags);

		/*
		 * Depth must be 0 while switched out. A global depth left >0
		 * after schedule made IRQ1 think it was nested in tty_sleep and
		 * skip scheduling the woken ash reader (dead shell keyboard).
		 */
		tty_sleep_depth--;
		enable_interrupts();
		sched_schedule_next();
		tty_sleep_depth++;

		if (proc->state != PROCESS_BLOCKED)
		{
			if (blocked_once)
				d1_16_tty_state_transition(proc, PROCESS_BLOCKED,
							   proc->state);
			tty_waiter_remove(proc);
			process_clear_in_thread_syscall_block(proc);
			d1_16_tty_read_resume(proc, "woke");
			tty_sleep_depth--;
			return 1;
		}

		/*
		 * All tasks blocked: RR has no idle thread yet, so poll PS/2 and
		 * wake TTY waiters from syscall context (QEMU GTK often skips IRQ1).
		 */
		enable_interrupts();
		kernel_idle_poll();

		if (ir0_console_input_ready())
		{
			prev_state = proc->state;
			process_set_sched_state(proc, PROCESS_READY);
			d1_16_tty_state_transition(proc, prev_state,
						   PROCESS_READY);
			tty_waiter_remove(proc);
			process_clear_in_thread_syscall_block(proc);
			d1_16_tty_read_resume(proc, "poll_ready");
			tty_sleep_depth--;
			return 1;
		}
	}
}

void tty_input_char(char c)
{
	ir0_console_keypress(c);
}

void ir0_console_keypress(char c)
{
	char nc;
	int line_done;
	unsigned char vintr;
	unsigned char vquit;

	tty_termios_ensure();
	nc = tty_normalize_input(c);
	if (nc == 0)
		return;

	/*
	 * ISIG: Ctrl+C / Ctrl+\ generate signals and are not queued as input.
	 * Without this, Ctrl+C inserted literal 'c' into blocked readers (wc).
	 */
	if (tty_isig_on())
	{
		vintr = tty_termios.c_cc[IR0_CC_VINTR];
		vquit = tty_termios.c_cc[IR0_CC_VQUIT];
		if (vintr == 0)
			vintr = 3;
		if (vquit == 0)
			vquit = 28;
		if ((unsigned char)nc == vintr)
		{
			canon_line_len = 0;
			input_kbd_resync_modifiers();
			tty_deliver_sig(SIGINT);
			/*
			 * Never sched_schedule_next() here: keypress runs under
			 * IRQ1. Wake + tty_need_resched; ISR / idle_poll switch.
			 */
			(void)ir0_console_wake_readers();
			return;
		}
		if ((unsigned char)nc == vquit)
		{
			canon_line_len = 0;
			input_kbd_resync_modifiers();
			tty_deliver_sig(SIGQUIT);
			(void)ir0_console_wake_readers();
			return;
		}
	}

	if (tty_icanon_on())
	{
		line_done = tty_canon_feed(nc);
		if (line_done)
		{
			ir0_ash_smoke_tty_line_ready();
			/*
			 * Wake waiters only. Scheduling from IRQ1 (keyboard →
			 * keypress) corrupted prev RIP → #UD into .bss /
			 * process_t during firstboot password Confirm.
			 * sched_irq_preempt_from_frame / idle_poll pick READY.
			 */
			(void)ir0_console_wake_readers();
		}
		return;
	}

	/* Raw (!ICANON): optional echo; byte lands in the kbd ring via store. */
	if (tty_echo_on())
		tty_echo_char(nc);
}

int ir0_console_input_ready(void)
{
	if (canon_readq_pos < canon_readq_len)
		return 1;
	if (!tty_icanon_on() && input_kbd_has_data())
		return 1;
	return 0;
}

int ir0_console_store_key_in_ring(void)
{
	tty_termios_ensure();
	return tty_icanon_on() ? 0 : 1;
}

int64_t tty_read_kernel(char *kbuf, size_t count, int nonblock)
{
	size_t bytes_read = 0;

	if (!kbuf)
		return -EFAULT;
	if (count == 0)
		return 0;

	tty_termios_ensure();

	for (;;)
	{
		/*
		 * Drain ready input before honouring a pending signal. Otherwise a
		 * sticky SIGCHLD (runsv/logger) turns every completed canon line into
		 * EINTR and getty never leaves the username read — no Password:
		 * after firstboot.
		 */
		if (canon_eof_pending && canon_readq_pos >= canon_readq_len)
		{
			canon_eof_pending = 0;
			klog_info_fmt("TTY",
				      "TTY_EOF pid=%x (VEOF empty line)",
				      current_process
					  ? (unsigned)((uint32_t)current_process->task.pid)
					  : 0u);
			return 0;
		}

		while (bytes_read < count && canon_readq_pos < canon_readq_len)
			tty_canon_drain(kbuf, count, &bytes_read);

		if (bytes_read > 0)
			return (int64_t)bytes_read;

		if (!tty_icanon_on())
		{
			while (bytes_read < count && input_kbd_has_data())
			{
				char c = input_kbd_get();

				c = tty_normalize_input(c);
				if (c == 0)
					continue;

				kbuf[bytes_read++] = c;
				if (tty_termios.c_cc[IR0_CC_VMIN] == 0)
					return (int64_t)bytes_read;
				if (bytes_read >= (size_t)tty_termios.c_cc[IR0_CC_VMIN])
					return (int64_t)bytes_read;
			}

			if (bytes_read > 0)
				return (int64_t)bytes_read;
		}

		if (current_process && signals_should_handle_on_run(current_process))
			return -EINTR;

		if (tty_icanon_on())
		{
			if (nonblock)
				return -EAGAIN;
			(void)tty_sleep_for_input();
			continue;
		}

		if (nonblock)
			return -EAGAIN;

		{
			unsigned vmin = tty_termios.c_cc[IR0_CC_VMIN];
			unsigned vtime = tty_termios.c_cc[IR0_CC_VTIME];

			/*
			 * Linux non-canonical: VMIN=0,VTIME=0 → return 0 now
			 * (ncurses poll). VMIN=0,VTIME>0 → wait up to VTIME
			 * tenths of a second then return 0.
			 */
			if (vmin == 0 && vtime == 0)
			{
				if (current_process &&
				    signals_should_handle_on_run(current_process))
					return -EINTR;
				return 0;
			}
			if (vmin == 0 && vtime > 0)
			{
				uint64_t now = clock_get_uptime_milliseconds();
				uint64_t deadline = now + (uint64_t)vtime * 100u;

				while (clock_get_uptime_milliseconds() < deadline)
				{
					if (input_kbd_has_data())
						break;
					if (current_process &&
					    signals_should_handle_on_run(current_process))
						return -EINTR;
					(void)ir0_clock_wait_block_until(deadline);
				}
				continue;
			}
		}

		(void)tty_sleep_for_input();
	}
}

int64_t tty_write_kernel(const char *kbuf, size_t count, uint8_t color)
{
	size_t i;

	if (!kbuf)
		return -EFAULT;

	tty_termios_ensure();

	for (i = 0; i < count; i++)
	{
		char c = kbuf[i];

		if (c == '\n' &&
		    (tty_termios.c_oflag & (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR)) ==
		    (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR))
		{
			char cr = '\r';

			console_backend_write(&cr, 1, color);
		}
		console_backend_write(&c, 1, color);
	}

	return (int64_t)count;
}

int tty_ioctl_termios_kernel(uint64_t request, struct ir0_termios *ktermios)
{
	if (!ktermios)
		return -EINVAL;

	if (request == IR0_CONSOLE_TCGETS)
	{
		tty_termios_ensure();
		*ktermios = tty_termios;
		return 0;
	}

	if (request == IR0_CONSOLE_TCSETS ||
	    request == IR0_CONSOLE_TCSETSW ||
	    request == IR0_CONSOLE_TCSETSF)
	{
		int was_icanon = tty_icanon_on();
		int now_icanon;

		/*
		 * Leaving raw: the kbd ring is the raw LD readq. Drain it before
		 * committing ICANON so tty_flush_input() (which only clears the
		 * ring while !ICANON) does not leave stale ESC/NL for the next
		 * raw session. Linux flushes the LD buffer, not i8042, on the
		 * equivalent termios path.
		 */
		now_icanon = (ktermios->c_lflag & IR0_LFLAG_ICANON) ? 1 : 0;
		if (!was_icanon && now_icanon)
			input_kbd_clear();

		tty_termios = *ktermios;
		/*
		 * Do NOT force ICANON when VMIN==0: nano and other editors use
		 * non-canonical timed reads (VMIN=0). Password prompts
		 * (bb_ask_noecho) keep ICANON and only clear ECHO. Cooked+echo
		 * restore after exec is ir0_console_reset_cooked_echo().
		 */
		now_icanon = (tty_termios.c_lflag & IR0_LFLAG_ICANON) ? 1 : 0;
		if (now_icanon)
			tty_termios.c_iflag |= IR0_IFLAG_ICRNL;
		tty_termios_ready = 1;
		/*
		 * Mode flip or TCSETSF: drop pending LD input so raw↔cooked does
		 * not replay stale NL/ESC bytes (empty-prompt storms).
		 */
		if (request == IR0_CONSOLE_TCSETSF || was_icanon != now_icanon)
		{
			tty_flush_input();
			/*
			 * Password/no-echo prompts can end with modifiers held; TCSETSF
			 * must resync like TCFLSH or the first shell keystrokes show
			 * garbage (ê-prefix) after interactive firstboot login.
			 */
			input_kbd_resync_modifiers();
		}
		return 0;
	}

	return -ENOTTY;
}

int tty_input_bytes_available(void)
{
	tty_termios_ensure();
	if (canon_readq_pos < canon_readq_len)
		return (int)(canon_readq_len - canon_readq_pos);
	if (!tty_icanon_on() && input_kbd_has_data())
		return 1;
	return 0;
}

void tty_flush_input(void)
{
	/*
	 * Line discipline only. The keyboard ring is the raw (!ICANON) readq;
	 * in cooked mode it is not filled and must not be reset from termios
	 * (TCFLSH/TCSETSF) — that races the IRQ1 producer (login garbage).
	 * Linux n_tty_flush_buffer does not empty i8042.
	 */
	if (!tty_icanon_on())
		input_kbd_clear();
	canon_line_len = 0;
	canon_readq_len = 0;
	canon_readq_pos = 0;
	canon_eof_pending = 0;
}

int ir0_console_wake_readers(void)
{
	int i;
	int woke = 0;
	int waiters_before = tty_waiter_count();
	uint32_t last_pid = 0;

	if (!ir0_console_input_ready())
		return 0;

	/*
	 * Wake one waiter back into tty_read_kernel (in-syscall drain).
	 * Do not tty_wake_stage_user_read here — that path fought
	 * process_arm_blocked_syscall_resume and hung getty after Enter.
	 */
	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		process_t *reader;
		int prev_state;

		if (!tty_read_waiters[i])
			continue;

		reader = tty_read_waiters[i];
		if (reader->state == PROCESS_ZOMBIE)
		{
			tty_read_waiters[i] = NULL;
			continue;
		}
		/*
		 * Async wake: mark READY only. Do not clear want_kernel_ret
		 * (process_after_task_save still needs it). Do not
		 * process_clear_in_thread_syscall_block here.
		 */
		if (reader->mode == USER_MODE)
			reader->irq_frame_saved = 0;
		prev_state = reader->state;
		process_set_sched_state(reader, PROCESS_READY);
		d1_16_tty_state_transition(reader, prev_state, PROCESS_READY);
		/* Prefer woken TTY reader on the next schedule. */
		sched_promote_process(reader);
		last_pid = (uint32_t)reader->task.pid;
		tty_read_waiters[i] = NULL;
		woke = 1;
		break;
	}

	if (woke)
	{
		tty_need_resched = 1;
		d1_16_tty_wake(waiters_before, woke, last_pid);
	}

	return woke;
}

int ir0_console_take_resched(void)
{
	int v = tty_need_resched;

	tty_need_resched = 0;
	return v;
}

int ir0_console_resched_pending(void)
{
	return tty_need_resched;
}

int ir0_console_in_tty_sleep(void)
{
	return tty_sleep_depth > 0;
}

int ir0_console_timer_resched_pending(void)
{
	return 0;
}

int ir0_console_poll(void)
{
	return ir0_console_input_ready();
}

int ir0_console_has_blocked_reader(void)
{
	int i;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i])
			return 1;
	}
	return 0;
}

void ir0_console_purge_waiters_for_process(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == p)
			tty_read_waiters[i] = NULL;
	}
}

void ir0_console_input_enqueue(char c)
{
	tty_input_char(c);
}

void ir0_console_drain_echo(void)
{
	(void)0;
}

void ir0_console_on_userspace_attach(void)
{
	if (tty_userspace_attached)
		return;
	tty_userspace_attached = 1;
	console_backend_userspace_handoff();
}

int ir0_console_in_userspace(void)
{
	return tty_userspace_attached;
}

int64_t ir0_console_read(void *kbuf, size_t count, int nonblock)
{
	d1_16_tty_read_pre(current_process, 0, nonblock,
			   (uintptr_t)(void *)&canon_readq);
	return tty_read_kernel((char *)kbuf, count, nonblock);
}

int64_t ir0_console_write(const void *kbuf, size_t count, uint8_t color)
{
	return tty_write_kernel((const char *)kbuf, count, color);
}

int ir0_console_isatty(void)
{
	return 1;
}

int ir0_console_term_width(void)
{
	int w;

	if (soft_ws_col > 0)
		return (int)soft_ws_col;
	w = console_get_width();
	return w > 0 ? w : 80;
}

int ir0_console_term_height(void)
{
	int h;

	if (soft_ws_row > 0)
		return (int)soft_ws_row;
	h = console_get_height();
	return h > 0 ? h : 25;
}

int ir0_console_ioctl_winsize(void *user_arg)
{
	struct ir0_winsize win;
	struct console_geometry geo;

	if (!user_arg)
		return -EINVAL;
	console_get_geometry(&geo);
	win.ws_row = (uint16_t)ir0_console_term_height();
	win.ws_col = (uint16_t)ir0_console_term_width();
	{
		unsigned scale = geo.scale ? geo.scale : 1u;
		unsigned cw = geo.cell_width ? geo.cell_width : 8u * scale;
		unsigned ch = geo.cell_height ? geo.cell_height : 16u * scale;

		win.ws_xpixel = (uint16_t)(win.ws_col * cw);
		win.ws_ypixel = (uint16_t)(win.ws_row * ch);
	}
	if (copy_to_user(user_arg, &win, sizeof(win)) != 0)
		return -EFAULT;
	return 0;
}

int ir0_console_ioctl_winsize_set(void *user_arg)
{
	struct ir0_winsize win;
	int32_t pgid;

	if (!user_arg)
		return -EINVAL;
	if (copy_from_user(&win, user_arg, sizeof(win)) != 0)
		return -EFAULT;
	if (win.ws_row == 0 || win.ws_col == 0)
		return -EINVAL;
	soft_ws_row = win.ws_row;
	soft_ws_col = win.ws_col;
	pgid = ir0_console_get_fg_pgid();
	if (pgid > 1)
		(void)send_signal_pgrp(pgid, SIGWINCH);
	return 0;
}

int ir0_console_fill_termios(struct ir0_termios *out)
{
	return tty_ioctl_termios_kernel(IR0_CONSOLE_TCGETS, out);
}

/*
 * Restore cooked+echo after password entry / exec. Dropped ECHO or ICANON
 * leaves ash looking dead (no echo, lines never finish).
 */
void ir0_console_reset_cooked_echo(void)
{
	tty_termios_ensure();
	tty_termios.c_iflag |= IR0_IFLAG_ICRNL;
	tty_termios.c_lflag |= (IR0_LFLAG_ICANON | IR0_LFLAG_ECHO |
				IR0_LFLAG_ECHOE | IR0_LFLAG_ECHOK |
				IR0_LFLAG_ECHONL);
	tty_termios_ready = 1;
	input_kbd_resync_modifiers();
}

int ir0_console_set_termios(const struct ir0_termios *in)
{
	struct ir0_termios tmp;

	if (!in)
		return -EINVAL;
	tmp = *in;
	return tty_ioctl_termios_kernel(IR0_CONSOLE_TCSETSW, &tmp);
}

void ir0_console_flush_input(void)
{
	tty_flush_input();
}

void ir0_console_flush_input_session(void)
{
	static int resync_tag;

	tty_flush_input();
	/*
	 * Only the momentary decoder state (mods + E0/E1), never the ring:
	 * clearing bytes here races IRQ1 (login garbage), but a session that
	 * ended with a modifier held would otherwise stick into the next
	 * getty (wrong case / stray CSI on the first keystrokes).
	 */
	input_kbd_resync_modifiers();
	if (!resync_tag)
	{
		resync_tag = 1;
		klog_notice_fmt("TTY", "KBD_STATE_RESYNC (session flush)");
	}
}

void ir0_console_after_tty_read_signal(int signo)
{
	(void)signo;
	/*
	 * PS/2 modifier state must not leak across any signal that interrupted
	 * a console read (SIGCHLD after ./a.out, SIGINT, SIGQUIT). Without this,
	 * VINTR stops working until session restart (hexdump | grep, ^C dead).
	 */
	input_kbd_resync_modifiers();
	if (signo == SIGINT || signo == SIGQUIT)
		tty_flush_input();
}

void ir0_console_after_signal_abandon(void)
{
	/*
	 * BusyBox ash: raise_interrupt() longjmps without rt_sigreturn
	 * (SIGFRAME_ABANDON). Same hygiene as VINTR on a cooked tty.
	 */
	ir0_console_after_tty_read_signal(SIGINT);
}
