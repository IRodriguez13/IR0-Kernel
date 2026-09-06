/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vdso.c
 * Description: Map built-in vDSO (AT_SYSINFO_EHDR) for musl clock_gettime fast path.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/vdso.h>
#include <ir0/elf64_layout.h>
#include <kernel/process.h>
#include <mm/paging.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>

int vdso_map(struct process *proc)
{
	const uint8_t *blob;
	size_t blob_size;
	uintptr_t base;
	uintptr_t base_aligned;
	size_t map_size;
	uint64_t *pml4;
	uint64_t flags;

	if (!vdso_have())
		return -ENOSYS;

	if (!proc || proc->mode != USER_MODE)
		return -EINVAL;

	pml4 = process_pgd(proc);
	if (!pml4)
		return -EINVAL;

	blob = vdso_blob_start();
	blob_size = vdso_blob_size();
	if (!blob || blob_size == 0)
		return -ENOEXEC;

	if (!elf64_image_valid(blob, blob_size))
	{
		klog_info_fmt("VDSO", "vDSO blob invalid (size=%u magic=%02x%02x%02x%02x)",
			(unsigned)blob_size,
			blob_size > 0 ? blob[0] : 0,
			blob_size > 1 ? blob[1] : 0,
			blob_size > 2 ? blob[2] : 0,
			blob_size > 3 ? blob[3] : 0);
		return -ENOEXEC;
	}

	/*
	 * Flat map the whole ELF at VDSO_USER_BASE so AT_SYSINFO_EHDR points at a
	 * valid ehdr and phdr vaddrs stay mapped (see arch vdso linker script).
	 */
	base = (uintptr_t)vdso_user_base();
	base_aligned = base & ~(uintptr_t)0xFFF;
	map_size = (size_t)(((base + blob_size + 0xFFF) & ~0xFFFULL) - base_aligned);
	flags = PAGE_USER | PAGE_EXEC;

	if (map_user_region_in_directory(pml4, base_aligned, map_size, flags) != 0)
		return -ENOMEM;

	if (copy_to_user_region_in_directory(pml4, base, blob, blob_size) != 0)
		return -EFAULT;

	return 0;
}
