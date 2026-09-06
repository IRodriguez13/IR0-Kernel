/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmap_contract.h
 * Description: musl/Linux mmap userspace ABI helpers (host + kernel)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_MAP_PRIVATE    0x02
#define IR0_MAP_ANONYMOUS  0x20
#define IR0_MAP_FIXED      0x10

#define IR0_MMAP_FAILED       ((uintptr_t)-1)
#define IR0_USER_MMAP_START   0x20000000UL
#define IR0_USER_STACK_TOP    0x7FFFF000UL
#define IR0_USER_STACK_SIZE   0x100000UL
#define IR0_USER_STACK_BASE   (IR0_USER_STACK_TOP - IR0_USER_STACK_SIZE)
#define IR0_USER_STACK_GUARD  (IR0_USER_STACK_BASE - IR0_PAGE_SIZE)
/*
 * Linux keeps anon mmap arenas far below the stack (large unmapped gap).
 * IR0 top-down mmap(NULL) must not place mappings adjacent to the stack guard.
 */
#define IR0_USER_STACK_MMAP_GAP  (1UL << 20)  /* 1 MiB */
#define IR0_USER_MMAP_END     (IR0_USER_STACK_GUARD - IR0_USER_STACK_MMAP_GAP)
#define IR0_PAGE_SIZE         4096UL
#define IR0_PAGE_MASK         (IR0_PAGE_SIZE - 1UL)
/*
 * mmap(NULL) on Linux (ASLR off) lands well above the legacy musl fallback
 * arena; host/ktest use this floor to reject low fixed placements.
 */
#define IR0_MMAP_NULL_MIN_VA  0x60000000UL

/*
 * Kernel internal mmap errors use negative errno magnitudes cast to void*
 * (SYSCALL_PTR_ERR). musl treats (unsigned long)ret >= -4096UL as error.
 */
static inline int ir0_mmap_is_kernel_error(uintptr_t ret)
{
	intptr_t ri = (intptr_t)ret;

	return ri >= -4095 && ri < 0;
}

static inline int ir0_mmap_kernel_errno(uintptr_t ret)
{
	if (!ir0_mmap_is_kernel_error(ret))
		return 0;
	return (int)-(intptr_t)ret;
}

/*
 * Encode kernel mmap return for x86-64 syscall rax (musl __syscall path).
 * Success: user VA. Failure: negative errno (-ENOMEM, -EINVAL, …).
 */
static inline int64_t ir0_mmap_syscall_ret(uintptr_t kernel_ret)
{
	if (ir0_mmap_is_kernel_error(kernel_ret))
		return (int64_t)(intptr_t)kernel_ret;
	return (int64_t)kernel_ret;
}

/*
 * libc mmap(2) wrapper view: MAP_FAILED + errno. Host tests validate mapping.
 */
static inline uintptr_t ir0_mmap_libc_ret(uintptr_t kernel_ret, int *errno_out)
{
	if (ir0_mmap_is_kernel_error(kernel_ret))
	{
		if (errno_out)
			*errno_out = ir0_mmap_kernel_errno(kernel_ret);
		return IR0_MMAP_FAILED;
	}
	if (errno_out)
		*errno_out = 0;
	return kernel_ret;
}

static inline int ir0_mmap_fixed_requires_exact_va(int flags)
{
	return (flags & IR0_MAP_FIXED) != 0;
}

static inline int ir0_mmap_in_musl_arena(uintptr_t va, size_t len,
					 uintptr_t arena_start,
					 uintptr_t arena_end)
{
	if (len == 0)
		return 0;
	return va >= arena_start && (va + len) <= arena_end;
}

/*
 * musl mallocng linear slot after last 4 KiB map ends at slot_end.
 * Next slot starts at slot_end (page aligned).
 */
static inline uintptr_t ir0_mmap_musl_next_slot(uintptr_t slot_start, size_t slot_len)
{
	return slot_start + slot_len;
}

static inline int ir0_mmap_page_aligned(uintptr_t va)
{
	return (va & IR0_PAGE_MASK) == 0;
}

static inline uintptr_t ir0_mmap_align_down(uintptr_t va, uintptr_t align)
{
	if (align == 0)
		return va;
	return va & ~(align - 1UL);
}

static inline int ir0_mmap_ranges_overlap(uintptr_t a, size_t alen,
					  uintptr_t b, size_t blen)
{
	uintptr_t a_end;
	uintptr_t b_end;

	if (alen == 0 || blen == 0)
		return 0;
	a_end = a + alen;
	b_end = b + blen;
	return a < b_end && b < a_end;
}

typedef struct ir0_mmap_occ
{
	uintptr_t start;
	size_t len;
} ir0_mmap_occ_t;

static inline int ir0_mmap_occupy(const ir0_mmap_occ_t *occ, size_t n,
				  uintptr_t va, size_t len)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (ir0_mmap_ranges_overlap(va, len, occ[i].start, occ[i].len))
			return 1;
	}
	return 0;
}

static inline uintptr_t ir0_mmap_heap_low_limit(uintptr_t heap_end,
						uintptr_t search_low)
{
	uintptr_t lo = search_low;

	if (heap_end > lo)
	{
		lo = ir0_mmap_align_down(heap_end + IR0_PAGE_SIZE - 1UL,
					 IR0_PAGE_SIZE);
	}
	return lo;
}

static inline uintptr_t ir0_mmap_stack_search_end(uintptr_t stack_start)
{
	uintptr_t guard;
	uintptr_t end;

	if (stack_start == 0)
		return IR0_USER_MMAP_END;

	guard = stack_start - IR0_PAGE_SIZE;
	if (guard < IR0_USER_STACK_MMAP_GAP)
		return IR0_USER_MMAP_END;

	end = guard - IR0_USER_STACK_MMAP_GAP;
	return end;
}

static inline int ir0_mmap_respects_stack_gap(uintptr_t map_start, size_t len,
					      uintptr_t stack_start)
{
	uintptr_t guard;
	uintptr_t map_end;

	if (len == 0)
		return 1;

	guard = (stack_start ? stack_start : IR0_USER_STACK_BASE) - IR0_PAGE_SIZE;
	map_end = map_start + len;
	if (map_end > guard)
		return 0;
	return (guard - map_end) >= IR0_USER_STACK_MMAP_GAP;
}

/*
 * Linux-like top-down placement for mmap(NULL). On success returns the
 * chosen start and updates *mmap_base_out for the next allocation.
 */
static inline uintptr_t ir0_mmap_pick_topdown(uintptr_t *mmap_base_out,
					      uintptr_t search_end,
					      uintptr_t search_low,
					      size_t length,
					      const ir0_mmap_occ_t *occ,
					      size_t nocc)
{
	uintptr_t top;
	uintptr_t start;

	if (!mmap_base_out || length == 0 || search_end <= search_low)
		return 0;
	if (length > (size_t)(search_end - search_low))
		return 0;

	top = *mmap_base_out;
	if (top == 0 || top > search_end)
		top = search_end;

	for (start = top - length; start >= search_low; start -= IR0_PAGE_SIZE)
	{
		start = ir0_mmap_align_down(start, IR0_PAGE_SIZE);
		if (start < search_low)
			break;
		if (ir0_mmap_occupy(occ, nocc, start, length))
			continue;
		*mmap_base_out = start;
		return start;
	}
	return 0;
}
