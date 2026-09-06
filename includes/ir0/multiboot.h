/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: multiboot.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Multiboot 1 specification structures
 * Reference: https://www.gnu.org/software/grub/manual/multiboot/
 * 
 */

#pragma once

#include <stdint.h>

#define MULTIBOOT_FLAG_MEM      (1 << 0)
#define MULTIBOOT_FLAG_BOOTDEV  (1 << 1)
#define MULTIBOOT_FLAG_CMDLINE  (1 << 2)
#define MULTIBOOT_FLAG_MODS    (1 << 3)
#define MULTIBOOT_FLAG_MMAP    (1 << 6)
#define MULTIBOOT_FLAG_VBE     (1 << 11)
#define MULTIBOOT_FLAG_FB      (1 << 12)

#define MULTIBOOT_FB_TYPE_INDEXED  0
#define MULTIBOOT_FB_TYPE_RGB      1
#define MULTIBOOT_FB_TYPE_EGA      2

struct multiboot_info
{
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    
    /* VBE (flags bit 11) */
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    /* Framebuffer (flags bit 12) */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

/*
 * Boot loader handoff (Multiboot info pointer). Set once from arch entry;
 * portable code reads via get_boot_params() — not arch_cpu.h.
 */
void set_boot_params(void *params);
void *get_boot_params(void);
