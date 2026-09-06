/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vdso.h
 * Description: Userspace vDSO mapping (AT_SYSINFO_EHDR) for musl/BusyBox.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ir0/abi/vdso_contract.h>

struct process;

static inline uint64_t vdso_ehdr(void)
{
	return vdso_user_base();
}

#if VDSO_HAVE
const uint8_t *vdso_blob_start(void);
size_t vdso_blob_size(void);
#endif

int vdso_map(struct process *proc);
