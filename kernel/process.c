/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.c
 * Description: Legacy path stub — lifecycle lives in kernel/process/*.c
 */

/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * Process ownership split (ARCH process monolith):
 *   kernel/process/core.c      — list, PID, syscall frame
 *   kernel/process/create.c    — spawn
 *   kernel/process/domains.c   — mm/files attach
 *   kernel/process/mm_struct.c — address-space domain
 *   kernel/process/files_struct.c — fd-table domain
 *   kernel/process/fork.c      — fork + rollback
 *   kernel/process/exec.c      — CLOEXEC on exec
 *   kernel/process/exit.c      — exit → zombie, destroy on reap
 *   kernel/process/wait.c      — wait/reap/reparent
 *   kernel/process/fdtable.c   — fd table lifecycle
 *   kernel/process/mm.c        — address space helpers
 *   kernel/process/signals.c   — default-fatal signals
 *
 * Teardown policy (exit vs destroy): see kernel/process/exit.c and
 * Documentation/mandocs/en/process.md.
 */
