/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fork.h
 * Description: ISA hooks for fork parent/child return (portable fork.c calls these).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>

struct process;

/*
 * Prepare parent/child CPU context so fork returns child_pid to parent and 0
 * to the child. May touch TLS (FS_BASE on x86). Returns 0 or -errno.
 * NULL-safe: no-op success if either process is NULL.
 *
 * May not sleep. IRQ state: caller-dependent (syscall context today).
 */
int fork_prepare_parent_return(struct process *parent, pid_t child_pid);
int fork_prepare_child_return(struct process *child, struct process *parent);

/*
 * Optional TLS after clone_thread SETTLS. Returns 0 or -errno.
 */
int process_set_tls(struct process *proc, uint64_t tls);
