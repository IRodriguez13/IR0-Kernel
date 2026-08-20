/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_brk_post_exec.c
 * Description: ktest — Linux brk ABI after ELF-style initial break
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include <ir0/abi/brk_contract.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <stdint.h>

#define KTEST_BUSYBOX_INITIAL_BRK 0x453000UL
#define KTEST_BRK_GROW 0x2000UL
#define KTEST_BRK_ACROSS_6M 0x601000UL

static int ktest_pte_present(uintptr_t va)
{
	uint64_t flags = 0;

	if (!current_process || !process_pgd(current_process))
		return 0;
	return is_page_mapped_in_directory(process_pgd(current_process), va,
					   &flags) == 1;
}

static int ktest_pte_user(uintptr_t va)
{
	uint64_t flags = 0;

	if (!current_process || !process_pgd(current_process))
		return 0;
	if (is_page_mapped_in_directory(process_pgd(current_process), va,
					&flags) != 1)
		return 0;
	return (flags & PAGE_USER) != 0;
}

void ktest_brk_post_exec(void)
{
	uint64_t saved_start;
	uint64_t saved_end;
	int64_t cur;
	int64_t grown;

	KTEST_BEGIN("brk_post_exec");

	saved_start = process_heap_start(current_process);
	saved_end = process_heap_end(current_process);

	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);

	cur = sys_brk(NULL);
	KASSERT_EQ((uint64_t)cur, KTEST_BUSYBOX_INITIAL_BRK);

	grown = sys_brk((void *)(KTEST_BUSYBOX_INITIAL_BRK + KTEST_BRK_GROW));
	KASSERT_EQ((uint64_t)grown, KTEST_BUSYBOX_INITIAL_BRK + KTEST_BRK_GROW);
	KASSERT(ktest_pte_present(KTEST_BUSYBOX_INITIAL_BRK));
	KASSERT(ktest_pte_present(KTEST_BUSYBOX_INITIAL_BRK + 0x1000UL));

	/* Cross the 6 MiB supervisor-2MB identity seam (Linux split_huge_pmd). */
	grown = sys_brk((void *)KTEST_BRK_ACROSS_6M);
	KASSERT_EQ((uint64_t)grown, KTEST_BRK_ACROSS_6M);
	KASSERT(ktest_pte_present(0x600000UL));
	KASSERT(ktest_pte_user(0x600000UL));

	process_set_heap_start(current_process, saved_start);
	process_set_heap_end(current_process, saved_end);

	KTEST_END();
}
