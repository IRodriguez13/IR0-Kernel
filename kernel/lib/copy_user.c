// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025–2026  Iván Rodriguez
 *
 * File: copy_user.c
 * Description: Portable copy_to/from_user with mode-aware validation.
 *              USER_MODE walks process page tables (COW-safe). KERNEL_MODE
 *              bypass is dbgshell/ktest only — never use for production
 *              userspace pointers.
 */

#include <ir0/copy_user.h>
#include <ir0/mm.h>
#include "process.h"
#include <mm/paging.h>
#include <string.h>
#include <config.h>
#include <ir0/ktm/klog.h>

int is_user_address_checked(const void *addr, size_t size, int check_mapped)
{
	uintptr_t start = (uintptr_t)addr;
	uintptr_t end = start + size;

	if (addr == NULL)
		return 0;

	if (end < start)
		return 0;

	if (!mm_user_va_ok(start, size))
		return 0;

	if (check_mapped)
	{
		process_t *current = process_get_current();

		if (!current || !process_pgd(current))
			return 0;

		for (uintptr_t addr_check = start & ~0xFFFUL; addr_check < end;
		     addr_check += 0x1000UL)
		{
			int mapped = is_page_mapped_in_directory(
				process_pgd(current), addr_check, NULL);

			if (mapped != 1)
			{
#if DEBUG_SYSCALLS
				klog_info("COPY_USER",
					  "Address range contains unmapped pages");
#endif
				return 0;
			}
		}
	}

	return 1;
}

int is_user_address(const void *addr, size_t size)
{
	/*
	 * Fast range check only; unmapped pages fault on copy. Callers that must
	 * reject unmapped user pointers before touching page tables (e.g. exec
	 * argv/envp walks) use is_user_address_checked(addr, size, 1).
	 */
	return is_user_address_checked(addr, size, 0);
}

int copy_to_user(void *dst, const void *src, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	/* KERNEL_MODE bypass (dbgshell, embedded init) */
	if (current && current->mode == KERNEL_MODE)
	{
		memcpy(dst, src, n);
		return 0;
	}

	if (!is_user_address(dst, n))
		return -EFAULT;

	/*
	 * Walk target process page tables via MM facade — safe under any active
	 * CR3 and fails cleanly on guard/unmapped pages (no kernel #PF panic).
	 */
	if (current && process_pgd(current))
	{
		if (copy_to_user_region_in_directory(process_pgd(current),
						     (uintptr_t)dst, src, n) != 0)
			return -EFAULT;
		return 0;
	}

	return -EFAULT;
}

int copy_from_user(void *dst, const void *src, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (current && current->mode == KERNEL_MODE)
	{
		memcpy(dst, src, n);
		return 0;
	}

	if (!is_user_address(src, n))
		return -EFAULT;

	if (current && process_pgd(current))
	{
		if (copy_from_user_region_in_directory(process_pgd(current),
						       (uintptr_t)src, dst,
						       n) != 0)
			return -EFAULT;
		return 0;
	}

	return -EFAULT;
}

int clear_user(void *dst, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (current && current->mode == KERNEL_MODE)
	{
		memset(dst, 0, n);
		return 0;
	}

	if (!is_user_address(dst, n))
		return -EFAULT;

	if (!current || !process_pgd(current))
		return -EFAULT;

	if (zero_user_region_in_directory(process_pgd(current), (uintptr_t)dst,
					  n) != 0)
		return -EFAULT;
	return 0;
}

int copy_from_user_cstring(char *dst, size_t dst_sz, const char *src)
{
	process_t *current = process_get_current();
	size_t i;

	if (!dst || dst_sz == 0 || !src)
		return -EFAULT;

	if (current && current->mode == KERNEL_MODE)
	{
		size_t n = 0;

		while (n + 1 < dst_sz && ((const char *)src)[n])
		{
			dst[n] = ((const char *)src)[n];
			n++;
		}
		dst[n] = '\0';
		return 0;
	}

	if (!is_user_address(src, 1))
		return -EFAULT;

	if (!current || !process_pgd(current))
		return -EFAULT;

	for (i = 0; i + 1 < dst_sz; i++)
	{
		char c;

		if (copy_from_user_region_in_directory(process_pgd(current),
						       (uintptr_t)src + i, &c,
						       1) != 0)
			return -EFAULT;
		dst[i] = c;
		if (c == '\0')
			return 0;
	}

	dst[dst_sz - 1] = '\0';
	return 0;
}
