/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_syscalls.h
 * Description: memory-management syscalls (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>

struct process;
typedef struct process process_t;

void *mm_mmap_file_private(process_t *proc, void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset);

int64_t sys_brk(void *addr);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t length);
int sys_mprotect(void *addr, size_t len, int prot);

/* sysinfo(2): memory, load averages, uptime and process count. */
int64_t sys_sysinfo(void *user_info);
