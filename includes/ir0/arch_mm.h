/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <ir0/mm.h>

/* Kernel heap region supplied by the active architecture memory layout. */
uintptr_t mm_kernel_heap_start(void);
size_t mm_kernel_heap_size(void);
