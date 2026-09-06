/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tlb.h
 * Description: Address-space activate + local TLB invalidate (portable MM hooks).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void mm_activate(uintptr_t root);
uintptr_t mm_current_root(void);
void tlb_invalidate_page(uintptr_t va);
void tlb_invalidate_all(void);
