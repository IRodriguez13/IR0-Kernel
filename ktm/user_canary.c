/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: user_canary.c
 * Description: Canary over the initial user stack image (argv/envp/auxv).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/user_canary.h>
#include <ir0/ktm/event.h>
#include <ir0/ktm/klog.h>
#include <mm/paging.h>

#define KTM_CANARY_BYTES 16
#define KTM_CANARY_MAGIC 0x4952304B414E3159ULL /* "IR0KANY1" */

/*
 * One in this many polls actually reads user memory. The point is to bound
 * the window between corruption and report without putting a user copy on
 * every syscall.
 */
#define KTM_CANARY_POLL_PERIOD 256

static uint32_t ktm_canary_poll_tick;
static uint32_t ktm_canary_reported_pid;

/*
 * Deliberately not mixed with the pid. A fork child inherits the parent's
 * stack image verbatim, so a pid-derived pattern makes every child look
 * corrupted: the first soak run reported pid 17 against a word stamped by
 * pid 10, with the complement word still intact.
 */
static uint64_t ktm_canary_word(void)
{
	return KTM_CANARY_MAGIC;
}

void ktm_user_canary_install(uint64_t *pml4, uint64_t stack_top, uint32_t pid)
{
	uint64_t words[2];

	if (!pml4 || stack_top < KTM_CANARY_BYTES)
		return;

	(void)pid;
	words[0] = ktm_canary_word();
	words[1] = ~words[0];

	(void)copy_to_user_region_in_directory(pml4,
					       (uintptr_t)(stack_top - KTM_CANARY_BYTES),
					       words, sizeof(words));
}

int ktm_user_canary_check(uint64_t *pml4, uint64_t stack_top, uint32_t pid,
			  const char *where)
{
	uint64_t words[2];
	uint64_t expect;

	if (!pml4 || stack_top < KTM_CANARY_BYTES)
		return 0;

	if (copy_from_user_region_in_directory(pml4,
					       (uintptr_t)(stack_top - KTM_CANARY_BYTES),
					       words, sizeof(words)) != 0)
		return 0; /* Not mapped: nothing proven either way. */

	expect = ktm_canary_word();
	if (words[0] == expect && words[1] == ~expect)
		return 0;

	ktm_event_emit4(KTM_EVENT_ERROR, KTM_SUBSYS_MM, (uint64_t)pid,
			stack_top - KTM_CANARY_BYTES, words[0], words[1]);

	/*
	 * Once per task: a broken canary usually stays broken, and repeating
	 * the dump would evict the history that explains the first break.
	 */
	if (ktm_canary_reported_pid != pid)
	{
		ktm_canary_reported_pid = pid;
		klog_notice_fmt("KTM",
				"KTM_USER_CANARY_BROKEN pid=%x at=%llx w0=%llx w1=%llx where=%s\n",
				(unsigned)pid,
				(unsigned long long)(stack_top - KTM_CANARY_BYTES),
				(unsigned long long)words[0],
				(unsigned long long)words[1],
				where ? where : "(none)");
		ktm_event_ring_dump(48, (1u << KTM_SUBSYS_MM) |
					(1u << KTM_SUBSYS_PROC) |
					(1u << KTM_SUBSYS_IPC));
	}

	return -1;
}

void ktm_user_canary_poll(uint64_t *pml4, uint64_t stack_top, uint32_t pid)
{
	if (++ktm_canary_poll_tick % KTM_CANARY_POLL_PERIOD)
		return;

	(void)ktm_user_canary_check(pml4, stack_top, pid, "syscall");
}
