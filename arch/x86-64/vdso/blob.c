/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: blob.c
 * Description: Linker symbols for the x86-64 vDSO ELF blob (objcopy embed).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>
#include <stdint.h>

extern const uint8_t _binary_vdso_so_start[];
extern const uint8_t _binary_vdso_so_end[];

const uint8_t *vdso_blob_start(void)
{
	return _binary_vdso_so_start;
}

size_t vdso_blob_size(void)
{
	return (size_t)(_binary_vdso_so_end - _binary_vdso_so_start);
}
