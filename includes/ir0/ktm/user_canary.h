/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: user_canary.h
 * Description: Canary over the initial user stack image (argv/envp/auxv).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * The 16 bytes between the end of the initial stack image and USER_STACK_TOP
 * are slack that no correct program touches. Stamping them turns a silent
 * overwrite into a named event at a known point in time, which is what a
 * crash further downstream cannot tell us: a memcpy faulting one page past
 * the stack top only proves the length was already wrong.
 */
void ktm_user_canary_install(uint64_t *pml4, uint64_t stack_top, uint32_t pid);

/*
 * Returns 0 when intact or unreadable, -1 when the pattern was overwritten.
 * Emits a KTM event and dumps the ring on the first breakage seen per task.
 */
int ktm_user_canary_check(uint64_t *pml4, uint64_t stack_top, uint32_t pid,
			  const char *where);

/* Rate-limited watchdog for the syscall boundary; cheap to call often. */
void ktm_user_canary_poll(uint64_t *pml4, uint64_t stack_top, uint32_t pid);
