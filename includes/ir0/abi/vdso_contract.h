/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vdso_contract.h
 * Description: Per-ISA vDSO user mapping contract (AT_SYSINFO_EHDR).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#if defined(CONFIG_ARCH_X86_64) && CONFIG_ARCH_X86_64
#define VDSO_HAVE 1
/* Must match arch/x86-64/vdso/vdso.lds VDSO_BASE. */
#define VDSO_USER_BASE 0x7FD00000UL
#elif defined(CONFIG_ARCH_ARM64) && CONFIG_ARCH_ARM64
#define VDSO_HAVE 0
/* Reserved for future aarch64 vDSO; unused until a blob is linked. */
#define VDSO_USER_BASE 0xFFFF000000000000UL
#else
#define VDSO_HAVE 0
#define VDSO_USER_BASE 0UL
#endif

static inline int vdso_have(void)
{
	return VDSO_HAVE != 0;
}

static inline uint64_t vdso_user_base(void)
{
	return (uint64_t)VDSO_USER_BASE;
}
