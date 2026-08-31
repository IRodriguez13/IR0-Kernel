/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_init.h
 * Description: Ordered boot-phase helpers called from kmain (portable init).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Early arch + heap + console (before serial banner). */
void boot_early(uint32_t multiboot_info);

/* PMM, logging, serial, version banner, platform/hypervisor note. */
void boot_memory_serial(uint32_t multiboot_info);

/* Selectable drivers, root block check, VFS root, bare-metal sentinel. */
void boot_drivers_rootfs(void);

/* Process/IPC/sched/syscalls + IRQ enable. */
void boot_runtime(void);

/* KTM, optional hostshare log, in-kernel ktests. */
void boot_diagnostics(void);

/* Load /sbin/init, spawn idle, first schedule into userspace (does not return). */
void boot_enter_userspace(void);
