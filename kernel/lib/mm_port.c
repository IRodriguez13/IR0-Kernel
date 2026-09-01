/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_port.c
 * Description: MM stats facade implementation (PMM + heap allocator).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/mm_port.h>

#include <mm/allocator.h>
#include <mm/pmm.h>

void ir0_mm_pmm_stats(size_t *total_frames, size_t *used_frames,
		      size_t *free_frames)
{
	pmm_stats(total_frames, used_frames, free_frames);
}

uintptr_t ir0_mm_pmm_start(void)
{
	return pmm_get_start();
}

uintptr_t ir0_mm_pmm_end(void)
{
	return pmm_get_end();
}

void ir0_mm_alloc_stats(size_t *total, size_t *used, size_t *allocs)
{
	alloc_stats(total, used, allocs);
}
