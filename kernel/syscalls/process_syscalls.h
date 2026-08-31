/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_syscalls.h
 * Description: process/credential/signal syscall helpers (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/time.h>
#include <ir0/types.h>
#include <ir0/signals.h>
#include <ir0/process.h>
#include <stddef.h>
#include <stdint.h>

struct sigaction;
struct robust_list_head;

int64_t sys_rt_sigaction(int signum, const struct sigaction *act,
			 struct sigaction *oldact, size_t sigsetsize);
int64_t sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset,
			   size_t sigsetsize);
int64_t sys_rt_sigsuspend(const sigset_t *mask, size_t sigsetsize);
int64_t sys_getgroups(int size, gid_t *list);
int64_t sys_setgroups(size_t size, const gid_t *list);
int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
int64_t sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int64_t sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
int64_t sys_tgkill(pid_t tgid, pid_t tid, int sig);
int64_t sys_set_robust_list(struct robust_list_head *head, size_t len);
int64_t sys_get_robust_list(int pid, struct robust_list_head **head_ptr,
			    size_t *len_ptr);
void process_exit_robust_list(process_t *p);
int64_t sys_setsid(void);
int64_t sys_getsid(pid_t pid);
int64_t sys_getpgid(pid_t pid);
int64_t sys_vfork(void);
int64_t sys_personality(unsigned long persona);
int64_t sys_setpriority(int which, int who, int prio);
int64_t sys_getpriority(int which, int who);
int64_t sys_setpgid(pid_t pid, pid_t pgid);
void process_cred_init_groups(process_t *p);
int process_cred_in_group(const process_t *p, gid_t gid);
void process_reset_signals_on_exec(process_t *p);

int64_t sys_exit(int exit_code);
int64_t sys_exit_group(int exit_code);
int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg);
int64_t sys_getpid(void);
int64_t sys_gettid(void);
int64_t sys_getppid(void);
int64_t sys_getuid(void);
int64_t sys_geteuid(void);
int64_t sys_getgid(void);
int64_t sys_getegid(void);
int64_t sys_setuid(uid_t uid);
int64_t sys_setgid(gid_t gid);
int64_t sys_umask(mode_t mask);
int64_t sys_exec(const char *pathname, char *const argv[], char *const envp[]);
int64_t sys_fork(void);
int64_t sys_clone(unsigned long flags, void *stack, int *parent_tid,
		  int *child_tid, unsigned long tls);
int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage);
int64_t sys_waitpid(pid_t pid, int *status, int options);
int64_t sys_kill(pid_t pid, int signal);
int64_t sys_sigaction(int signum, const struct sigaction *act,
		      struct sigaction *oldact);
int64_t sys_prctl(int option, unsigned long arg2, unsigned long arg3,
		  unsigned long arg4, unsigned long arg5);
int64_t sys_arch_prctl(int code, unsigned long addr);
int64_t sys_set_tid_address(int *tidptr);
int64_t sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
		  int *uaddr2, int val3);
int64_t sys_getrandom(void *buf, size_t buflen, unsigned int flags);
int64_t sys_prlimit64(pid_t pid, unsigned int resource, const void *new_limit,
		      void *old_limit);
int64_t sys_getrlimit(unsigned int resource, void *rlim);
int64_t sys_sigreturn(struct sigcontext *ctx);
