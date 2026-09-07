/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console / TTY facade — keyboard input, text output, minimal termios.
 *
 * Syscall/devfs copy user buffers; TTY operates on kernel buffers only.
 * devfs_console_ioctl copies struct ir0_termios once, then calls
 * tty_ioctl_termios_kernel() with a kernel struct.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_CONSOLE_TCGETS  0x5401u
#define IR0_CONSOLE_TCSETS  0x5402u
#define IR0_CONSOLE_TCSETSW 0x5403u
#define IR0_CONSOLE_TCSETSF 0x5404u
#define IR0_CONSOLE_TCFLSH  0x540Bu
#define IR0_CONSOLE_TIOCGWINSZ 0x5413u
#define IR0_CONSOLE_TIOCSWINSZ 0x5414u
/* Linux FIONREAD / TIOCINQ — bytes available to read */
#define IR0_CONSOLE_FIONREAD 0x541Bu
/* Linux TIOCGPTN — get pty number (_IOR('T', 0x30, unsigned int)) */
#define IR0_TIOCGPTN 0x80045430u
/* Linux TIOCSPTLCK — lock/unlock pty (_IOW('T', 0x31, int)); no-op OK */
#define IR0_TIOCSPTLCK 0x40045431u
/* Linux TIOCSCTTY — make this the controlling terminal */
#define IR0_TIOCSCTTY 0x540Eu
/* Linux TIOCGPGRP / TIOCSPGRP — foreground process group */
#define IR0_TIOCGPGRP 0x540Fu
#define IR0_TIOCSPGRP 0x5410u

typedef unsigned int ir0_tcflag_t;
typedef unsigned char ir0_cc_t;

/*
 * Linux kernel uapi termios for TCGETS/TCSETS (asm-generic/termbits.h).
 * NCCS=19 → sizeof 36. glibc userspace termios is larger (NCCS=32); libc
 * converts. Copying the glibc size into the kernel TCGETS buffer smashes
 * TinyX/glibc stack canaries in tcgetattr().
 */
#define IR0_NCCS 19

struct ir0_winsize
{
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
};

struct ir0_termios
{
	ir0_tcflag_t c_iflag;
	ir0_tcflag_t c_oflag;
	ir0_tcflag_t c_cflag;
	ir0_tcflag_t c_lflag;
	ir0_cc_t c_line;
	ir0_cc_t c_cc[IR0_NCCS];
};

/* c_iflag bits: Linux asm-generic/termbits.h (octal → hex). */
#define IR0_IFLAG_INLCR           (0x00000040u) /* 0000100 */
#define IR0_IFLAG_IGNCR           (0x00000080u) /* 0000200 */
#define IR0_IFLAG_ICRNL           (0x00000100u) /* 0000400 — was wrongly 0x400 (IXON) */
#define IR0_IFLAG_IXON            (0x00000400u) /* 0002000 */
#define IR0_CONSOLE_IFLAG_DEFAULT (IR0_IFLAG_ICRNL)
#define IR0_CONSOLE_OFLAG_DEFAULT (0x00000005u) /* ONLCR|OPOST */
#define IR0_CONSOLE_CFLAG_DEFAULT (0x00004B00u) /* CS8|CREAD|HUPCL */
/* ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHONL — ECHONL keeps Enter visible with ECHO off. */
#define IR0_CONSOLE_LFLAG_DEFAULT (0x0000007Bu)
#define IR0_LFLAG_ISIG             (0x00000001u)
#define IR0_LFLAG_ICANON           (0x00000002u)
#define IR0_LFLAG_ECHO             (0x00000008u)
#define IR0_LFLAG_ECHOE            (0x00000010u)
#define IR0_LFLAG_ECHOK            (0x00000020u)
#define IR0_LFLAG_ECHONL           (0x00000040u)
#define IR0_OFLAG_ONLCR           (0x00000004u)
#define IR0_OFLAG_OPOST           (0x00000001u)
/* Linux asm-generic/termbits.h c_cc indices */
#define IR0_CC_VINTR              0
#define IR0_CC_VQUIT              1
#define IR0_CC_VERASE             2
#define IR0_CC_VEOF               4
#define IR0_CC_VTIME              5
#define IR0_CC_VMIN               6

/* TTY line discipline (kernel buffers only) */
void tty_input_char(char c);
int64_t tty_read_kernel(char *kbuf, size_t count, int nonblock);
int64_t tty_write_kernel(const char *kbuf, size_t count, uint8_t color);
int tty_ioctl_termios_kernel(uint64_t request, struct ir0_termios *ktermios);
void tty_flush_input(void);
int tty_input_bytes_available(void);

int ir0_console_wake_readers(void);
int ir0_console_take_resched(void);
int ir0_console_resched_pending(void);
int ir0_console_in_tty_sleep(void);
int ir0_console_timer_resched_pending(void);
int ir0_console_poll(void);
int ir0_console_has_blocked_reader(void);
struct process;
void ir0_console_purge_waiters_for_process(struct process *p);

void ir0_console_input_enqueue(char c);
void ir0_console_keypress(char c);
int ir0_console_input_ready(void);
int ir0_console_store_key_in_ring(void);
void ir0_console_drain_echo(void);
void ir0_console_on_userspace_attach(void);
int ir0_console_in_userspace(void);

int64_t ir0_console_read(void *kbuf, size_t count, int nonblock);
int64_t ir0_console_write(const void *kbuf, size_t count, uint8_t color);
int ir0_console_isatty(void);
int ir0_console_term_width(void);
int ir0_console_term_height(void);
int ir0_console_ioctl_winsize(void *user_arg);
int ir0_console_ioctl_winsize_set(void *user_arg);
int ir0_console_fill_termios(struct ir0_termios *out);
int ir0_console_set_termios(const struct ir0_termios *in);
void ir0_console_reset_cooked_echo(void);
void ir0_console_flush_input(void);
/* Userspace TCFLSH: drop pending LD input and resync the PS/2 decoder so a
 * session ended mid-modifier (SEGV/logout) does not leak Shift/Ctrl/E0. */
void ir0_console_flush_input_session(void);
/*
 * Linux-like console hygiene after a signal hits stdin read(2).
 * IR0 adapts to BusyBox/GNU userspace — not the other way around.
 *  SIGINT/SIGQUIT: flush raw/canonical partial + resync PS/2 mods.
 *  SIGCHLD/other: resync mods only (EINTR, no spurious EOF).
 */
void ir0_console_after_tty_read_signal(int signo);
/* Ash longjmp without rt_sigreturn (SIGFRAME_ABANDON). */
void ir0_console_after_signal_abandon(void);
int ir0_console_set_fg_pgid(int32_t pgid);
void ir0_console_clear_fg_pgid(int32_t pgid, int32_t exiting_pid);
/* TIOCSCTTY on the console: session-leader check + foreground pgrp bind. */
int ir0_console_ioctl_set_ctty(void);
int32_t ir0_console_get_fg_pgid(void);
