/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_dispatch.c
 * Description: syscall table + dispatcher (ARCH-1 split)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "syscall_dispatch.h"
#include "process.h"
#include <kernel/syscalls.h>
#include "fs_syscalls.h"
#include "fs_path_syscalls.h"
#include "mm_syscalls.h"
#include "socket_syscalls.h"
#include "process_syscalls.h"
#include "io_syscalls.h"
#include "time_syscalls.h"
#include "epoll_syscalls.h"
#include <ir0/syscall_linux.h>
#include <ir0/kexec.h>
#include <ir0/signals.h>
#include <ir0/futex.h>
#include <ir0/sock_udp.h>
#include <ir0/clone.h>
#include <ir0/utsname.h>
#include <ir0/console_backend.h>
#include <ir0/input_backend.h>
#include <ir0/serial_io.h>
#include <ir0/errno.h>
#include <ir0/process.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/sched.h>
#include <ir0/sysv_shm.h>
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <ir0/time.h>
#include <ktm.h>
#include <ktm_probe_diag.h>
#include <d1_12_read_diag.h>
#include <config.h>
#include <stddef.h>
#include <stdint.h>

/* Stub for unimplemented syscalls (musl ABI compatibility) */
static int64_t sys_nosys(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6)
{
  (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
  return -ENOSYS;
}

/* Syscall handler type: 6 args for Linux ABI (arg6 for mmap, etc.) */
typedef int64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t);

/* Wrappers to adapt IR0 handlers to uniform 6-arg signature */
#define WRAP0(h) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return h(); }
#define WRAP1(h, cast1) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return h((cast1)a1); }
#define WRAP2(h, c1, c2) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a3;(void)a4;(void)a5;(void)a6; return h((c1)a1, (c2)a2); }
#define WRAP3(h, c1, c2, c3) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a4;(void)a5;(void)a6; return h((c1)a1, (c2)a2, (c3)a3); }
#define WRAP4(h, c1, c2, c3, c4) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a5;(void)a6; return h((c1)a1, (c2)a2, (c3)a3, (c4)a4); }
#define WRAP5(h, c1, c2, c3, c4, c5) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a6; return h((c1)a1, (c2)a2, (c3)a3, (c4)a4, (c5)a5); }
#define WRAP6(h, c1, c2, c3, c4, c5, c6) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    return (int64_t)h((c1)a1, (c2)a2, (c3)a3, (c4)a4, (c5)a5, (c6)a6); }

static int64_t wrap_sys_mmap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
			     uint64_t a5, uint64_t a6)
{
	uintptr_t ret;

	ret = (uintptr_t)sys_mmap((void *)a1, (size_t)a2, (int)a3, (int)a4,
				  (int)a5, (off_t)a6);
	return ir0_mmap_syscall_ret(ret);
}

WRAP3(sys_socket, int, int, int)
WRAP3(sys_bind, int, const struct sockaddr *, socklen_t)
WRAP3(sys_connect, int, const struct sockaddr *, socklen_t)
WRAP2(sys_listen, int, int)
WRAP3(sys_accept, int, struct sockaddr *, socklen_t *)
WRAP4(sys_accept4, int, struct sockaddr *, socklen_t *, int)
WRAP4(sys_socketpair, int, int, int, int *)
WRAP3(sys_sendmsg, int, const struct msghdr *, int)
WRAP3(sys_recvmsg, int, struct msghdr *, int)
WRAP2(sys_shutdown, int, int)
WRAP3(sys_getsockname, int, struct sockaddr *, socklen_t *)
WRAP3(sys_getpeername, int, struct sockaddr *, socklen_t *)
WRAP5(sys_setsockopt, int, int, int, const void *, socklen_t)
WRAP5(sys_getsockopt, int, int, int, void *, socklen_t *)
WRAP3(sys_shmget, int, size_t, int)
WRAP3(sys_shmat, int, const void *, int)
WRAP1(sys_shmdt, const void *)
WRAP3(sys_shmctl, int, int, void *)
WRAP2(sys_memfd_create, const char *, unsigned int)
WRAP2(sys_eventfd2, unsigned int, int)
WRAP2(sys_timerfd_create, int, int)
WRAP4(sys_timerfd_settime, int, int, const struct itimerspec *, struct itimerspec *)
WRAP2(sys_timerfd_gettime, int, struct itimerspec *)
WRAP6(sys_sendto, int, const void *, size_t, int, const struct sockaddr *, socklen_t)
WRAP6(sys_recvfrom, int, void *, size_t, int, struct sockaddr *, socklen_t *)

WRAP1(sys_exit, int)
WRAP1(sys_exit_group, int)
WRAP3(sys_read, int, void *, size_t)
WRAP3(sys_write, int, const void *, size_t)
WRAP3(sys_readv, int, const struct iovec *, int)
WRAP3(sys_writev, int, const struct iovec *, int)
WRAP3(sys_open, const char *, int, mode_t)
WRAP1(sys_close, int)
WRAP3(sys_waitpid, pid_t, int *, int)
WRAP2(sys_link, const char *, const char *)
WRAP3(sys_readlink, const char *, char *, size_t)
WRAP4(sys_readlinkat, int, const char *, char *, size_t)
WRAP2(sys_symlink, const char *, const char *)
WRAP3(sys_symlinkat, const char *, int, const char *)
WRAP2(sys_rename, const char *, const char *)
WRAP2(sys_truncate, const char *, off_t)
WRAP2(sys_ftruncate, int, off_t)
WRAP1(sys_unlink, const char *)
WRAP3(sys_unlinkat, int, const char *, int)
WRAP4(sys_renameat, int, const char *, int, const char *)
WRAP1(sys_uname, struct utsname *)
WRAP2(sys_access, const char *, int)
WRAP4(sys_faccessat, int, const char *, int, int)
WRAP1(sys_dup, int)
WRAP3(sys_exec, const char *, char *const *, char *const *)
WRAP1(sys_chdir, const char *)
WRAP1(sys_chroot, const char *)
WRAP1(sys_fchdir, int)
WRAP5(sys_mount, const char *, const char *, const char *, unsigned long,
      const void *)
WRAP2(sys_umount, const char *, int)
WRAP2(sys_mkdir, const char *, mode_t)
WRAP1(sys_rmdir, const char *)
WRAP2(sys_chmod, const char *, mode_t)
WRAP3(sys_chown, const char *, uid_t, gid_t)
WRAP3(sys_lseek, int, off_t, int)
WRAP2(sys_getcwd, char *, size_t)
WRAP4(sys_utimensat, int, const char *, const struct timespec *, int)
WRAP2(sys_stat, const char *, stat_t *)
WRAP2(sys_fstat, int, stat_t *)
WRAP2(sys_statfs, const char *, void *)
WRAP2(sys_fstatfs, int, void *)
WRAP2(sys_dup2, int, int)
WRAP2(sys_flock, int, int)
WRAP2(sys_fchmod, int, mode_t)
WRAP3(sys_fchown, int, uid_t, gid_t)
WRAP4(sys_fchmodat, int, const char *, mode_t, int)
WRAP5(sys_fchownat, int, const char *, uid_t, gid_t, int)
WRAP4(sys_mknodat, int, const char *, unsigned int, unsigned int)
WRAP3(sys_mknod, const char *, unsigned int, unsigned int)
WRAP1(sys_brk, void *)
WRAP2(sys_munmap, void *, size_t)
WRAP3(sys_mprotect, void *, size_t, int)
WRAP2(sys_kill, pid_t, int)
WRAP4(sys_rt_sigaction, int, const struct sigaction *, struct sigaction *, size_t)
WRAP4(sys_rt_sigprocmask, int, const sigset_t *, sigset_t *, size_t)
WRAP2(sys_rt_sigsuspend, const sigset_t *, size_t)
WRAP2(sys_getgroups, int, gid_t *)
WRAP2(sys_setgroups, size_t, const gid_t *)
WRAP3(sys_setresuid, uid_t, uid_t, uid_t)
WRAP3(sys_getresuid, uid_t *, uid_t *, uid_t *)
WRAP3(sys_setresgid, gid_t, gid_t, gid_t)
WRAP3(sys_getresgid, gid_t *, gid_t *, gid_t *)
WRAP3(sys_tgkill, pid_t, pid_t, int)
WRAP2(sys_arch_prctl, int, unsigned long)
WRAP1(sys_set_tid_address, int *)
WRAP3(sys_fcntl, int, int, unsigned long)
WRAP4(sys_openat, int, const char *, int, mode_t)
WRAP4(sys_newfstatat, int, const char *, stat_t *, int)
WRAP2(sys_clock_gettime, int, struct timespec *)
WRAP6(sys_futex, int *, int, int, const struct timespec *, int *, int)
WRAP3(sys_getrandom, void *, size_t, unsigned int)
WRAP2(sys_set_robust_list, struct robust_list_head *, size_t)
WRAP3(sys_get_robust_list, int, struct robust_list_head **, size_t *)
WRAP4(sys_prlimit64, pid_t, unsigned int, const void *, void *)
WRAP2(sys_getrlimit, unsigned int, void *)
WRAP4(sys_reboot, int, int, unsigned int, void *)
WRAP4(sys_kexec_load, unsigned long, unsigned long, struct kexec_segment *, unsigned long)
WRAP2(sys_pipe2, int *, int)
WRAP1(sys_pipe, int *)
WRAP1(sys_sigreturn, struct sigcontext *)
WRAP3(sys_ioctl, int, uint64_t, void *)
WRAP3(sys_syslog, int, char *, int)
WRAP3(sys_getdents, int, void *, size_t)
WRAP3(sys_getdents64, int, void *, size_t)
WRAP3(sys_poll, struct pollfd *, unsigned int, int)
WRAP5(sys_select, int, fd_set *, fd_set *, fd_set *, struct timeval *)
WRAP1(sys_epoll_create1, int)
WRAP4(sys_epoll_ctl, int, int, int, struct epoll_event *)
WRAP4(sys_epoll_wait, int, struct epoll_event *, int, int)
WRAP6(sys_epoll_pwait, int, struct epoll_event *, int, int, const void *, size_t)
WRAP6(sys_pselect6, int, fd_set *, fd_set *, fd_set *, const struct timespec *, const void *)
WRAP2(sys_nanosleep, const struct timespec *, struct timespec *)
WRAP0(sys_pause)
WRAP0(sys_sync)
WRAP2(sys_gettimeofday, struct timeval *, void *)
WRAP2(sys_getitimer, int, struct itimerval *)
WRAP3(sys_setitimer, int, const struct itimerval *, struct itimerval *)
WRAP1(sys_alarm, unsigned int)
WRAP5(sys_prctl, int, unsigned long, unsigned long, unsigned long, unsigned long)
WRAP1(sys_setuid, uid_t)
WRAP1(sys_setgid, gid_t)
WRAP1(sys_umask, mode_t)
WRAP5(sys_clone, unsigned long, void *, int *, int *, unsigned long)

#undef WRAP1
#undef WRAP2
#undef WRAP3
#undef WRAP4
#undef WRAP5
#undef WRAP6

/* WRAP0 for no-arg handlers */
static int64_t wrap_sys_fork(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_fork(); }
static int64_t wrap_sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getpid(); }
static int64_t wrap_sys_gettid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_gettid(); }
static int64_t wrap_sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getppid(); }
static int64_t wrap_sys_setsid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_setsid(); }
static int64_t wrap_sys_setpgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a3;(void)a4;(void)a5;(void)a6; return sys_setpgid((pid_t)a1, (pid_t)a2); }
static int64_t wrap_sys_getsid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getsid((pid_t)a1); }
static int64_t wrap_sys_getpgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getpgid((pid_t)a1); }
static int64_t wrap_sys_getuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getuid(); }
static int64_t wrap_sys_geteuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_geteuid(); }
static int64_t wrap_sys_getgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getgid(); }
static int64_t wrap_sys_getegid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getegid(); }

/* Console scroll: IR0 custom syscall */
static int64_t wrap_console_scroll(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  console_backend_scroll((int)a1);
  return 0;
}

/* Console clear: IR0 custom syscall */
static int64_t wrap_console_clear(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  console_backend_clear((uint8_t)a1);
  return 0;
}

/* Keyboard layout set/get: IR0 custom syscalls */
static int64_t wrap_keymap_set(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  return input_kbd_set_layout((int)a1);
}

static int64_t wrap_keymap_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  return input_kbd_get_layout();
}

/* Syscall table: Linux x86-64 numbers -> handlers */
static syscall_handler_t syscall_table_rw[__NR_syscall_max];

void syscall_table_init(void)
{
  for (size_t i = 0; i < __NR_syscall_max; i++)
    syscall_table_rw[i] = sys_nosys;

  /* Implemented syscalls - Linux numbers */
  syscall_table_rw[__NR_read]           = wrap_sys_read;
  syscall_table_rw[__NR_readv]          = wrap_sys_readv;
  syscall_table_rw[__NR_write]          = wrap_sys_write;
  syscall_table_rw[__NR_writev]         = wrap_sys_writev;
  syscall_table_rw[__NR_open]           = wrap_sys_open;
  syscall_table_rw[__NR_close]          = wrap_sys_close;
  syscall_table_rw[__NR_stat]           = wrap_sys_stat;
  syscall_table_rw[__NR_lstat]          = wrap_sys_stat;
  syscall_table_rw[__NR_fstat]          = wrap_sys_fstat;
  syscall_table_rw[__NR_statfs]         = wrap_sys_statfs;
  syscall_table_rw[__NR_fstatfs]        = wrap_sys_fstatfs;
  syscall_table_rw[__NR_poll]           = wrap_sys_poll;
  syscall_table_rw[__NR_select]         = wrap_sys_select;
  syscall_table_rw[__NR_lseek]          = wrap_sys_lseek;
  syscall_table_rw[__NR_mmap]           = wrap_sys_mmap;
  syscall_table_rw[__NR_mprotect]       = wrap_sys_mprotect;
  syscall_table_rw[__NR_munmap]         = wrap_sys_munmap;
  syscall_table_rw[__NR_brk]            = wrap_sys_brk;
  syscall_table_rw[__NR_rt_sigaction]   = wrap_sys_rt_sigaction;
  syscall_table_rw[__NR_rt_sigprocmask] = wrap_sys_rt_sigprocmask;
  syscall_table_rw[__NR_rt_sigsuspend]   = wrap_sys_rt_sigsuspend;
  syscall_table_rw[__NR_rt_sigreturn]   = wrap_sys_sigreturn;
  syscall_table_rw[__NR_fcntl]            = wrap_sys_fcntl;
  syscall_table_rw[__NR_flock]            = wrap_sys_flock;
  syscall_table_rw[__NR_mknodat]          = wrap_sys_mknodat;
  syscall_table_rw[__NR_mknod]            = wrap_sys_mknod;
  syscall_table_rw[__NR_ioctl]          = wrap_sys_ioctl;
  syscall_table_rw[__NR_pipe]           = wrap_sys_pipe;
  syscall_table_rw[__NR_pipe2]          = wrap_sys_pipe2;
  syscall_table_rw[__NR_dup2]           = wrap_sys_dup2;
  syscall_table_rw[__NR_nanosleep]      = wrap_sys_nanosleep;
  syscall_table_rw[__NR_getitimer]      = wrap_sys_getitimer;
  syscall_table_rw[__NR_alarm]          = wrap_sys_alarm;
  syscall_table_rw[__NR_setitimer]      = wrap_sys_setitimer;
  syscall_table_rw[__NR_pause]          = wrap_sys_pause;
  syscall_table_rw[__NR_getpid]         = wrap_sys_getpid;
  syscall_table_rw[__NR_gettid]         = wrap_sys_gettid;
  syscall_table_rw[__NR_getuid]         = wrap_sys_getuid;
  syscall_table_rw[__NR_syslog]         = wrap_sys_syslog;
  syscall_table_rw[__NR_geteuid]        = wrap_sys_geteuid;
  syscall_table_rw[__NR_getgid]         = wrap_sys_getgid;
  syscall_table_rw[__NR_getegid]        = wrap_sys_getegid;
  syscall_table_rw[__NR_setuid]         = wrap_sys_setuid;
  syscall_table_rw[__NR_setgid]         = wrap_sys_setgid;
  syscall_table_rw[__NR_getgroups]      = wrap_sys_getgroups;
  syscall_table_rw[__NR_setgroups]      = wrap_sys_setgroups;
  syscall_table_rw[__NR_setresuid]      = wrap_sys_setresuid;
  syscall_table_rw[__NR_getresuid]      = wrap_sys_getresuid;
  syscall_table_rw[__NR_setresgid]      = wrap_sys_setresgid;
  syscall_table_rw[__NR_getresgid]      = wrap_sys_getresgid;
  syscall_table_rw[__NR_umask]          = wrap_sys_umask;
  syscall_table_rw[__NR_prctl]          = wrap_sys_prctl;
  syscall_table_rw[__NR_clone]           = wrap_sys_clone;
  syscall_table_rw[__NR_fork]          = wrap_sys_fork;
  syscall_table_rw[__NR_execve]        = wrap_sys_exec;
  syscall_table_rw[__NR_exit]           = wrap_sys_exit;
  syscall_table_rw[__NR_wait4]          = wrap_sys_waitpid;
  syscall_table_rw[__NR_kill]           = wrap_sys_kill;
  syscall_table_rw[__NR_reboot]         = wrap_sys_reboot;
  syscall_table_rw[__NR_kexec_load]     = wrap_sys_kexec_load;
  syscall_table_rw[__NR_tgkill]         = wrap_sys_tgkill;
  syscall_table_rw[__NR_getdents]       = wrap_sys_getdents;
  syscall_table_rw[__NR_getcwd]         = wrap_sys_getcwd;
  syscall_table_rw[__NR_utimensat]      = wrap_sys_utimensat;
  syscall_table_rw[__NR_chdir]          = wrap_sys_chdir;
  syscall_table_rw[__NR_chroot]         = wrap_sys_chroot;
  syscall_table_rw[__NR_fchdir]         = wrap_sys_fchdir;
  syscall_table_rw[__NR_mkdir]          = wrap_sys_mkdir;
  syscall_table_rw[__NR_rmdir]          = wrap_sys_rmdir;
  syscall_table_rw[__NR_link]           = wrap_sys_link;
  syscall_table_rw[__NR_readlink]       = wrap_sys_readlink;
  syscall_table_rw[__NR_symlink]        = wrap_sys_symlink;
  syscall_table_rw[__NR_symlinkat]      = wrap_sys_symlinkat;
  syscall_table_rw[__NR_readlinkat]     = wrap_sys_readlinkat;
  syscall_table_rw[__NR_rename]         = wrap_sys_rename;
  syscall_table_rw[__NR_unlink]         = wrap_sys_unlink;
  syscall_table_rw[__NR_truncate]       = wrap_sys_truncate;
  syscall_table_rw[__NR_ftruncate]      = wrap_sys_ftruncate;
  syscall_table_rw[__NR_unlinkat]       = wrap_sys_unlinkat;
  syscall_table_rw[__NR_renameat]       = wrap_sys_renameat;
  syscall_table_rw[__NR_uname]          = wrap_sys_uname;
  syscall_table_rw[__NR_access]         = wrap_sys_access;
  syscall_table_rw[__NR_faccessat]      = wrap_sys_faccessat;
  syscall_table_rw[__NR_dup]            = wrap_sys_dup;
  syscall_table_rw[__NR_chmod]         = wrap_sys_chmod;
  syscall_table_rw[__NR_fchmod]        = wrap_sys_fchmod;
  syscall_table_rw[__NR_chown]          = wrap_sys_chown;
  syscall_table_rw[__NR_fchown]        = wrap_sys_fchown;
  syscall_table_rw[__NR_fchmodat]       = wrap_sys_fchmodat;
  syscall_table_rw[__NR_fchownat]       = wrap_sys_fchownat;
  syscall_table_rw[__NR_gettimeofday]   = wrap_sys_gettimeofday;
  syscall_table_rw[__NR_getppid]        = wrap_sys_getppid;
  syscall_table_rw[__NR_setsid]         = wrap_sys_setsid;
  syscall_table_rw[__NR_getsid]         = wrap_sys_getsid;
  syscall_table_rw[__NR_getpgid]        = wrap_sys_getpgid;
  syscall_table_rw[__NR_setpgid]        = wrap_sys_setpgid;
  syscall_table_rw[__NR_arch_prctl]     = wrap_sys_arch_prctl;
  syscall_table_rw[__NR_set_tid_address] = wrap_sys_set_tid_address;
  syscall_table_rw[__NR_openat]         = wrap_sys_openat;
  syscall_table_rw[__NR_getdents64]     = wrap_sys_getdents64;
  syscall_table_rw[__NR_newfstatat]     = wrap_sys_newfstatat;
  syscall_table_rw[__NR_futex]          = wrap_sys_futex;
  syscall_table_rw[__NR_clock_gettime]  = wrap_sys_clock_gettime;
  syscall_table_rw[__NR_set_robust_list] = wrap_sys_set_robust_list;
  syscall_table_rw[__NR_get_robust_list] = wrap_sys_get_robust_list;
  syscall_table_rw[__NR_getrandom]      = wrap_sys_getrandom;
  syscall_table_rw[__NR_prlimit64]      = wrap_sys_prlimit64;
  syscall_table_rw[__NR_getrlimit]      = wrap_sys_getrlimit;
  syscall_table_rw[__NR_epoll_create1]  = wrap_sys_epoll_create1;
  syscall_table_rw[__NR_epoll_create]   = wrap_sys_epoll_create1;
  syscall_table_rw[__NR_epoll_ctl]      = wrap_sys_epoll_ctl;
  syscall_table_rw[__NR_epoll_wait]     = wrap_sys_epoll_wait;
  syscall_table_rw[__NR_epoll_pwait]    = wrap_sys_epoll_pwait;
  syscall_table_rw[__NR_pselect6]       = wrap_sys_pselect6;
  syscall_table_rw[__NR_mount]          = wrap_sys_mount;
  syscall_table_rw[__NR_umount2]        = wrap_sys_umount;
  syscall_table_rw[__NR_sync]           = wrap_sys_sync;
  syscall_table_rw[__NR_console_scroll]  = wrap_console_scroll;
  syscall_table_rw[__NR_console_clear]   = wrap_console_clear;
  syscall_table_rw[__NR_keymap_set]      = wrap_keymap_set;
  syscall_table_rw[__NR_keymap_get]      = wrap_keymap_get;

  /*
   * Socket API: AF_UNIX stream + TCP loopback + socketpair/msg/SCM_RIGHTS.
   */
#if CONFIG_ENABLE_NETWORKING
  syscall_table_rw[__NR_socket]      = wrap_sys_socket;
  syscall_table_rw[__NR_bind]        = wrap_sys_bind;
  syscall_table_rw[__NR_connect]     = wrap_sys_connect;
  syscall_table_rw[__NR_listen]      = wrap_sys_listen;
  syscall_table_rw[__NR_accept]      = wrap_sys_accept;
  syscall_table_rw[__NR_accept4]     = wrap_sys_accept4;
  syscall_table_rw[__NR_socketpair]  = wrap_sys_socketpair;
  syscall_table_rw[__NR_sendto]      = wrap_sys_sendto;
  syscall_table_rw[__NR_recvfrom]    = wrap_sys_recvfrom;
  syscall_table_rw[__NR_sendmsg]     = wrap_sys_sendmsg;
  syscall_table_rw[__NR_recvmsg]     = wrap_sys_recvmsg;
  syscall_table_rw[__NR_shutdown]    = wrap_sys_shutdown;
  syscall_table_rw[__NR_getsockname] = wrap_sys_getsockname;
  syscall_table_rw[__NR_getpeername] = wrap_sys_getpeername;
  syscall_table_rw[__NR_setsockopt]  = wrap_sys_setsockopt;
  syscall_table_rw[__NR_getsockopt]  = wrap_sys_getsockopt;
  syscall_table_rw[__NR_shmget]      = wrap_sys_shmget;
  syscall_table_rw[__NR_shmat]       = wrap_sys_shmat;
  syscall_table_rw[__NR_shmdt]       = wrap_sys_shmdt;
  syscall_table_rw[__NR_shmctl]      = wrap_sys_shmctl;
#else
  {
    static const unsigned socket_nosys_nrs[] = {
      __NR_socket, __NR_bind, __NR_connect, __NR_listen, __NR_accept,
      __NR_accept4, __NR_socketpair, __NR_sendto, __NR_recvfrom, __NR_sendmsg, __NR_recvmsg,
      __NR_shutdown, __NR_getsockname, __NR_getpeername, __NR_setsockopt,
      __NR_getsockopt,
    };
    size_t si;

    for (si = 0; si < sizeof(socket_nosys_nrs) / sizeof(socket_nosys_nrs[0]); si++)
      syscall_table_rw[socket_nosys_nrs[si]] = sys_nosys;
  }
#endif
  syscall_table_rw[__NR_memfd_create] = wrap_sys_memfd_create;
  syscall_table_rw[__NR_eventfd2] = wrap_sys_eventfd2;
  syscall_table_rw[__NR_timerfd_create] = wrap_sys_timerfd_create;
  syscall_table_rw[__NR_timerfd_settime] = wrap_sys_timerfd_settime;
  syscall_table_rw[__NR_timerfd_gettime] = wrap_sys_timerfd_gettime;
  syscall_table_rw[__NR_exit_group]     = wrap_sys_exit_group;
}

/**
 * syscall_dispatch - Dispatch system call via table (Linux/musl ABI)
 * @syscall_num: Linux x86-64 syscall number
 * @arg1-arg6: System call arguments (arg6 on stack per AMD64 SysV ABI)
 *
 * Returns: System call return value, or -ENOSYS for unknown/unimplemented
 */
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  int64_t r;
  static int fase10_count = 0;
  pid_t cur_pid = current_process ? current_process->task.pid : 0;
  int do_trace = (fase10_count < 5 && cur_pid >= 2);

  if (current_process)
    process_capture_syscall_frame(current_process);

  if (current_process && current_process->mode == USER_MODE)
  {
    fork_ret_first_syscall_entry(syscall_num,
                                 process_syscall_ip(current_process),
                                 process_syscall_sp(current_process));
  }

  if (do_trace) {
    extern uint64_t iretq_checkpoint_buf[40];
  }

  if (syscall_num >= __NR_syscall_max)
    return -ENOSYS;

  syscall_handler_t handler = syscall_table_rw[syscall_num];
  KTM_TRACE_SYSCALL_ENTER((uint32_t)syscall_num);
  if (current_process && current_process->mode == USER_MODE)
  {
    ktm_probe_diag_syscall_pre(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6,
			       process_syscall_ip(current_process));
    if (syscall_num == __NR_read)
    {
      d1_12_read_diag_syscall_pre(current_process, (int)arg1,
				  (uintptr_t)arg2, (size_t)arg3,
				  process_syscall_ip(current_process));
    }
  }
  r = handler(arg1, arg2, arg3, arg4, arg5, arg6);
  KTM_TRACE_SYSCALL_RET((uint32_t)syscall_num, (uint32_t)r);
  ktm_probe_diag_syscall_post(syscall_num, r);
  if (current_process && current_process->mode == USER_MODE &&
      syscall_num == __NR_read)
    d1_12_read_diag_syscall_post(current_process, r);

  if (do_trace) {
    fase10_count++;
  }

  if (syscall_num == __NR_fork || syscall_num == __NR_clone ||
      syscall_num == __NR_vfork)
  {
    /*
     * Mark ring-0 before waking the child so a timer-deferred schedule
     * resumes via kernel_ret (syscall stack), not user iretq with a stale rip.
     * IRQs off first: otherwise arm leaves KERNEL_CS+user RIP while the timer
     * can still preempt and schedule the parent as next (desk #UD class).
     */
    if (current_process && current_process->mode == USER_MODE)
    {
      disable_interrupts();
      process_arm_kernel_syscall_sleep(current_process);
    }

    /*
     * Linux-style fork exit: wake child after parent retval is in rax, then
     * keep IF=0 until sysret so the timer cannot run the child first (UP).
     */
    if (current_process && current_process->fork_pending_child)
    {
      /*
       * Disable IRQs before enqueueing the child: sched_add can otherwise
       * run the child on timer tick before parent sysret (UP race).
       */
      disable_interrupts();
      process_fork_wake_pending(current_process);
    }
  }
  else
  {
    /*
     * Signal handler armed during this syscall (e.g. SIGALRM in recvfrom):
     * sysret would ignore task.RIP and return to the insn after the syscall.
     * Iretq into the handler instead (restorer → rt_sigreturn later).
     */
    if (current_process && current_process->mode == USER_MODE &&
        current_process->saved_context &&
        current_process->signal_enter_pending)
    {
      current_process->signal_enter_pending = 0;
      process_restore_user_task_segments(current_process);
      current_process->irq_frame_saved = 0;
      current_process->coop_resched_resume = 0;
      current_process->want_kernel_ret = 0;
      arch_restore_user_fs_base();
      switch_to_user_task(&current_process->task);
    }

    if (current_process && current_process->mode == USER_MODE &&
        current_process->syscall_frame_fresh)
    {
      /*
       * Cooperative in-syscall reschedule for syscall-insn (musl) tasks.
       * Resume THIS task from its saved syscall_frame via a fresh iretq, never
       * via kernel_ret on the single shared global syscall stack / user_rsp_save
       * scratch: a peer task's syscall resets that stack to the top and would
       * clobber our saved pt_regs, corrupting user registers on resume.
       */
      if (sched_user_return_take_switch())
      {
        process_arm_coop_resched_resume(current_process, (uint64_t)r);
        sched_schedule_next();
        /*
         * Control only returns here when no switch actually occurred (a
         * coop-resumed task re-enters in user mode, not at this call site).
         * Disarm so a later real switch is not misrouted through the resume.
         */
        if (current_process)
        {
          current_process->irq_frame_saved = 0;
          current_process->coop_resched_resume = 0;
        }
      }
    }
    else
    {
      if (current_process && current_process->mode == USER_MODE)
        process_arm_kernel_syscall_sleep(current_process);
      sched_need_resched_user_return();
    }
  }

  /*
   * Restore user segments in task_t only after any in-syscall schedule
   * completes; asm sysret reloads DS/ES independently.
   */
  if (current_process && current_process->mode == USER_MODE)
    process_restore_user_task_segments(current_process);

  return r;
}
