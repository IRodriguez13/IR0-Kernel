/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm.h
 * Description: ISA root-table layout helpers (user vs kernel half).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Number of root-table slots for the user half (x86-64: PML4[0..255]). */
unsigned mm_user_root_slots(void);

/* Total root-table slots (x86-64: 512). */
unsigned mm_root_slots(void);

/*
 * Copy present kernel-half entries from @src_root into @dst_root.
 * User half of @dst_root is left untouched by this helper.
 */
void mm_copy_kernel_half(uint64_t *dst_root, const uint64_t *src_root);
