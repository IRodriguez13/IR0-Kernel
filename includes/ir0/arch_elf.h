/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_elf.h
 * Description: ELF e_machine accepted by this kernel build (portable loader).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* ELF e_machine values (System V ABI). */
#define ELF_EM_X86_64  62
#define ELF_EM_AARCH64 183

#if defined(ARCH_ARM64) || defined(__aarch64__)
#define ARCH_ELF_MACHINE ELF_EM_AARCH64
#else
#define ARCH_ELF_MACHINE ELF_EM_X86_64
#endif

static inline int arch_elf_machine_supported(uint16_t machine)
{
	return machine == (uint16_t)ARCH_ELF_MACHINE;
}
