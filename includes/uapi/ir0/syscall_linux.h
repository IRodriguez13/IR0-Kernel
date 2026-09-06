/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_linux.h
 * Description: Public UAPI — Linux x86-64 syscall numbers implemented by IR0
 * plus the IR0-specific range. Exported by `make headers_install`.
 *
 * Numbers from Linux arch/x86/entry/syscalls/syscall_64.tbl, so musl-linked
 * userspace binaries run unmodified.
 */

/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_UAPI_SYSCALL_LINUX_H
#define _IR0_UAPI_SYSCALL_LINUX_H

/* Linux x86-64 syscall numbers (subset for musl/Doom) */
#define __NR_read           0
#define __NR_write          1
#define __NR_open           2
#define __NR_close          3
#define __NR_stat           4
#define __NR_fstat          5
#define __NR_lstat          6
#define __NR_poll           7
#define __NR_lseek          8
#define __NR_mmap           9
#define __NR_mprotect      10
#define __NR_munmap        11
#define __NR_brk           12
#define __NR_rt_sigaction  13
#define __NR_rt_sigprocmask 14
#define __NR_rt_sigsuspend 130
#define __NR_rt_sigreturn  15
#define __NR_ioctl         16
#define __NR_pread64        17
#define __NR_pwrite64      18
#define __NR_readv         19
#define __NR_writev        20
#define __NR_access        21
#define __NR_pipe          22
#define __NR_pipe2        293
#define __NR_select        23
#define __NR_sched_yield   24
#define __NR_mremap        25
#define __NR_msync         26
#define __NR_mincore       27
#define __NR_madvise       28
#define __NR_shmget        29
#define __NR_shmat         30
#define __NR_shmctl        31
#define __NR_dup           32
#define __NR_dup2          33
#define __NR_pause         34
#define __NR_nanosleep     35
#define __NR_getitimer     36
#define __NR_alarm         37
#define __NR_setitimer     38
#define __NR_getpid        39
#define __NR_socket        41
#define __NR_connect       42
#define __NR_accept        43
#define __NR_sendto        44
#define __NR_recvfrom      45
#define __NR_sendmsg       46
#define __NR_recvmsg       47
#define __NR_shutdown      48
#define __NR_bind          49
#define __NR_listen        50
#define __NR_getsockname   51
#define __NR_getpeername   52
#define __NR_socketpair    53
#define __NR_setsockopt    54
#define __NR_getsockopt    55
#define __NR_clone         56
#define __NR_fork          57
#define __NR_vfork         58
#define __NR_vfork         58
#define __NR_execve        59
#define __NR_exit          60
#define __NR_wait4         61
#define __NR_kill          62
#define __NR_tkill         200
#define __NR_tgkill        234
#define __NR_uname         63
#define __NR_shmdt         67
#define __NR_mknod         133
#define __NR_fcntl         72
#define __NR_flock         73
#define __NR_fsync         74
#define __NR_fdatasync     75
#define __NR_truncate      76
#define __NR_ftruncate     77
#define __NR_getdents      78
#define __NR_getdents64   217
#define __NR_getcwd        79
#define __NR_chdir         80
#define __NR_fchdir        81
#define __NR_chroot       161
#define __NR_rename        82
#define __NR_mkdir         83
#define __NR_rmdir         84
#define __NR_link          86
#define __NR_unlink        87
#define __NR_symlink       88
#define __NR_readlink      89
#define __NR_chmod         90
#define __NR_fchmod        91
#define __NR_chown         92
#define __NR_fchown        93
#define __NR_lchown        94
#define __NR_umask         95
#define __NR_gettimeofday  96
#define __NR_personality   135
#define __NR_getpriority   140
#define __NR_setpriority   141
#define __NR_getrlimit     97
#define __NR_getrusage     98
#define __NR_sysinfo       99
#define __NR_ptrace       101
#define __NR_getuid       102
#define __NR_syslog       103
#define __NR_getgid       104
#define __NR_getgroups    115
#define __NR_setuid       105
#define __NR_setgid       106
#define __NR_geteuid      107
#define __NR_getegid      108
#define __NR_setpgid      109
#define __NR_getppid      110
#define __NR_setsid       112
#define __NR_getpgid      121
#define __NR_getsid       124
#define __NR_setgroups    116
#define __NR_setresuid    117
#define __NR_getresuid    118
#define __NR_setresgid    119
#define __NR_getresgid    120
#define __NR_epoll_create  213
#define __NR_epoll_ctl     233
#define __NR_epoll_wait    232
#define __NR_epoll_pwait   281
#define __NR_epoll_create1 291
#define __NR_pselect6      270
#define __NR_statfs       137
#define __NR_fstatfs      138
#define __NR_mount        165
#define __NR_umount2      166
#define __NR_sync         162
#define __NR_reboot       169
#define __NR_kexec_load   246
#define __NR_prctl        157
#define __NR_arch_prctl   158
#define __NR_gettid       186
#define __NR_set_tid_address 218
#define __NR_futex          202
#define __NR_clock_gettime   228
#define __NR_set_robust_list 273
#define __NR_get_robust_list 274
#define __NR_getrandom       318
#define __NR_memfd_create    319
#define __NR_timerfd_create  283
#define __NR_timerfd_settime 286
#define __NR_timerfd_gettime 287
#define __NR_accept4         288
#define __NR_eventfd2        290
#define __NR_prlimit64       302
#define __NR_openat          257
#define __NR_mknodat         259
#define __NR_fchmodat        268
#define __NR_faccessat       269
#define __NR_symlinkat       266
#define __NR_readlinkat      267
#define __NR_fchownat        260
#define __NR_newfstatat      262
#define __NR_unlinkat        263
#define __NR_renameat        264
#define __NR_utimensat       280
#define __NR_exit_group   231

/*
 * Linux time64 syscalls (musl / BusyBox may call these even on x86_64 when built
 * against recent kernel headers). Must not collide with IR0 custom numbers.
 */
#define __NR_clock_gettime64 403
#define __NR_clock_settime64 404
#define __NR_clock_adjtime64 405
#define __NR_clock_getres_time64 406
#define __NR_clock_nanosleep_time64 407

/* IR0 custom syscalls (440+ — outside Linux x86-64 assigned range) */
#define __NR_console_scroll   440
#define __NR_console_clear    441
#define __NR_keymap_set       442
#define __NR_keymap_get       443
/* 444 was sudo_auth: privilege elevation is userspace policy (doas/sudo). */

/* Max syscall number we handle (for table size) */
#define __NR_syscall_max   450

#endif /* _IR0_UAPI_SYSCALL_LINUX_H */
