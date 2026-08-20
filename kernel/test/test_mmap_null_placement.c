/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_mmap_null_placement.c
 * Description: ktest — Linux-like mmap(NULL) top-down placement
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include <config.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <stdint.h>

#define KTEST_MAP_PRIVATE   0x02
#define KTEST_MAP_ANONYMOUS 0x20
#define KTEST_MAP_FIXED     0x10
#define KTEST_PROT_RW       0x3

#define KTEST_BUSYBOX_INITIAL_BRK 0x453000UL

static int ktest_pte_present(uintptr_t va)
{
	uint64_t flags = 0;

	if (!current_process || !process_pgd(current_process))
		return 0;
	return is_page_mapped_in_directory(process_pgd(current_process), va,
					   &flags) == 1;
}

static void *ktest_mmap_anon(size_t len, int prot, int flags, void *addr)
{
	return sys_mmap(addr, len, prot, flags, -1, 0);
}

static void ktest_unmap_all(void **maps, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (maps[i] && maps[i] != (void *)(intptr_t)-1)
			sys_munmap(maps[i], 0x1000);
	}
}

void ktest_mmap_null_placement(void)
{
	void *slot[4];
	uintptr_t addrs[4];
	uint64_t saved_heap_start;
	uint64_t saved_heap_end;
	uint64_t saved_mmap_base;
	void *fixed;
	uintptr_t fixed_addr;
	size_t i;

	KTEST_BEGIN("mmap_null_placement");

	saved_heap_start = process_heap_start(current_process);
	saved_heap_end = process_heap_end(current_process);
	saved_mmap_base = process_mmap_base(current_process);

	process_set_heap_start(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_heap_end(current_process, KTEST_BUSYBOX_INITIAL_BRK);
	process_set_mmap_base(current_process, 0);

	for (i = 0; i < 4; i++)
	{
		slot[i] = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					  KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS,
					  NULL);
		KASSERT(slot[i] != (void *)(intptr_t)-1);
		addrs[i] = (uintptr_t)slot[i];
		KASSERT(addrs[i] >= IR0_MMAP_NULL_MIN_VA);
		KASSERT(addrs[i] != USER_MMAP_START);
		KASSERT(ir0_mmap_respects_stack_gap(addrs[i], 0x1000,
						    USER_STACK_BASE));
		KASSERT(ktest_pte_present(addrs[i]));
	}

	for (i = 0; i < 4; i++)
	{
		size_t j;

		for (j = i + 1; j < 4; j++)
			KASSERT(addrs[i] != addrs[j]);
	}

	for (i = 0; i < 4; i++)
	{
		size_t j;

		for (j = i + 1; j < 4; j++)
			KASSERT(addrs[i] + 0x1000 <= addrs[j] ||
				addrs[j] + 0x1000 <= addrs[i]);
	}

	fixed_addr = addrs[3] - 0x1000UL;
	fixed = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
				KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
				KTEST_MAP_FIXED,
				(void *)fixed_addr);
	KASSERT(fixed == (void *)fixed_addr);
	KASSERT(ktest_pte_present(fixed_addr));
	slot[3] = fixed;
	addrs[3] = fixed_addr;

	{
		uint64_t id_flags = 0;
		void *split;
		void *heap_fixed;
		void *anon;

		KASSERT(is_page_mapped_in_directory(process_pgd(current_process),
						    0x800000UL, &id_flags) == 1);
		KASSERT((id_flags & PAGE_USER) == 0);

		split = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
						KTEST_MAP_FIXED,
					(void *)0x600000UL);
		KASSERT(split == (void *)0x600000UL);
		KASSERT(ktest_pte_present(0x600000UL));
		KASSERT(sys_munmap(split, 0x1000) == 0);

		heap_fixed = ktest_mmap_anon(0x1000, KTEST_PROT_RW,
					     KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS |
						     KTEST_MAP_FIXED,
					     (void *)0x800000UL);
		KASSERT((intptr_t)heap_fixed < 0);

		anon = ktest_mmap_anon(0x4000, KTEST_PROT_RW,
				       KTEST_MAP_PRIVATE | KTEST_MAP_ANONYMOUS,
				       NULL);
		KASSERT(anon != (void *)(intptr_t)-1);
		KASSERT((uintptr_t)anon >= USER_MMAP_START);
		KASSERT((uintptr_t)anon >= IR0_MMAP_NULL_MIN_VA);
		((volatile char *)anon)[0] = 0x5A;
		((volatile char *)anon)[0x3FFF] = 0x5A;
		KASSERT(sys_munmap(anon, 0x4000) == 0);
	}

	ktest_unmap_all(slot, 4);

	process_set_heap_start(current_process, saved_heap_start);
	process_set_heap_end(current_process, saved_heap_end);
	process_set_mmap_base(current_process, saved_mmap_base);

	KTEST_END();
}
