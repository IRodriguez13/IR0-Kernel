/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: allocator.h
 * Description: IR0 kernel source/header file
 */

/* Simple unified memory allocator */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/arch_mm.h>

/*
 * Memory layout (from boot: 0-32MB mapped)
 * 0x000000 - 0x100000 : Reserved (BIOS, boot)
 * 0x100000 - 0x800000 : Kernel code/data (1MB-8MB)
 * 0x800000 - 0x2000000: Heap (8MB-32MB = 24MB heap)
 */

#define SIMPLE_HEAP_START (mm_kernel_heap_start())
#define SIMPLE_HEAP_SIZE (mm_kernel_heap_size())
#define SIMPLE_HEAP_END (SIMPLE_HEAP_START + SIMPLE_HEAP_SIZE)

/* Initialize the allocator */
void alloc_init(void);

void *alloc(size_t size);

void alloc_free(void *ptr);

void alloc_stats(size_t *total, size_t *used, size_t *allocs);

void *all_realloc(void *ptr, size_t new_size);
