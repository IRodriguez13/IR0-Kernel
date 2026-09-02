/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: io_syscalls.h
 * Description: IR0 kernel header — io syscalls
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <ir0/poll.h>
#include <ir0/select.h>
#include <ir0/time.h>
#include <ir0/process.h>
#include <ir0/pipe.h>
#include <stddef.h>

int64_t sys_poll(struct pollfd *user_fds, unsigned int nfds, int timeout_ms);
int64_t sys_select(int nfds, fd_set *user_r, fd_set *user_w, fd_set *user_e,
		   struct timeval *user_tv);
int64_t io_select_timeout_ms(int nfds, fd_set *user_r, fd_set *user_w,
			     fd_set *user_e, int timeout_ms, int has_timeout);
int64_t syscall_sleep_ms_locked(uint64_t ms);
int fd_can_read_for(process_t *proc, int fd);
int fd_can_write_for(process_t *proc, int fd);
int64_t sys_pause(void);
int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem);
void poll_wake_check(void);
int poll_wake_check_nosched(void);
void syscall_wake_blocked_on_child(process_t *parent);
void syscall_poll_finish_blocked_resume(process_t *proc);

int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_dup(int oldfd);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_ioctl(int fd, uint64_t request, void *arg);
int64_t sys_fcntl(int fd, int cmd, unsigned long arg);
int64_t sys_pipe(int pipefd[2]);
int64_t sys_pipe2(int pipefd[2], int flags);
/* Linux syslog(2) / klogctl — BusyBox dmesg. */
int64_t sys_syslog(int type, char *bufp, int len);
fd_entry_t *get_process_fd_table(void);
void ensure_devfs_init(void);
int stdio_is_redirected(fd_entry_t *fd_table, int fd);
int pipe_wait(process_t *proc, pipe_t *pipe, int waiting_read, size_t write_need);
void pipe_wake_check(void);
void pipe_wake_all(pipe_t *pipe);
void pipe_purge_waiters_for_process(process_t *proc);
void fd_slot_stats_get(uint64_t *created, uint64_t *destroyed,
		       uint64_t *blocked_readers, uint64_t *blocked_writers);
void fd_slot_note_created(void);
void fd_slot_note_destroyed(void);
