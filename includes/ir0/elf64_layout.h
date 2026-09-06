/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: elf64_layout.h
 * Description: Minimal ELF64 header layout (host + kernel, LP64).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define ELF64_MAGIC0 0x7f
#define ELF64_MAGIC1 'E'
#define ELF64_MAGIC2 'L'
#define ELF64_MAGIC3 'F'

typedef struct
{
	unsigned char e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} elf64_ehdr_t;

static inline int elf64_image_valid(const void *blob, size_t size)
{
	const elf64_ehdr_t *eh;

	if (!blob || size < sizeof(elf64_ehdr_t))
		return 0;

	eh = (const elf64_ehdr_t *)blob;
	if (eh->e_ident[0] != ELF64_MAGIC0 || eh->e_ident[1] != ELF64_MAGIC1 ||
	    eh->e_ident[2] != ELF64_MAGIC2 || eh->e_ident[3] != ELF64_MAGIC3)
		return 0;
	if (eh->e_phoff == 0 || eh->e_phnum == 0)
		return 0;
	if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size)
		return 0;

	return 1;
}
