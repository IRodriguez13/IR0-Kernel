/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: klog.h
 * Description: Kernel logging hub (absolute center). Format:
 *   [ts] [LEVEL] [COMP] message
 *
 * Layers (semantic):
 *   - klog = event core (timestamp, severity, subsystem, dmesg ring, serial).
 *     Compiles and runs without the KTM test suite. Boot banner uses klog
 *     (COMP BOOT) after serial_init; VGA/FB also gets a console_puts copy.
 *   - KTM = testing protocol (TEST, ASSERT, INVARIANT, KTM| transport).
 *     Consumes the same serial sink; not the name of the human logger.
 *   - Userspace smoke tags (runit stages, FASE harnesses): bare tokens via
 *     ir0_smoke_tag() / write(1) — same role as klog_smoke() for autokill.
 *   - QEMU host chatter: kept out of the guest serial log (sibling
 *     *.qemu-stderr from scripts/smoke_autokill.py).
 *
 * Include via <ir0/ktm/klog.h> or umbrella <ir0/ktm/ktm.h> (umbrella reexports
 * klog for convenience; fs, net, drivers should not need KTM asserts to log).
 * Protocol KTM| is separate (VERBOSE for CHECKPOINT, PROBE, LOG); product serial
 * still emits ASSERT, SUITE, and smoke tags. serial_print only from ktm/klog.c
 * (plus transport and serial driver stubs).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <ir0/klog_event.h>

typedef enum
{
	KLOG_LEVEL_TRACE = 0,
	KLOG_LEVEL_DEBUG = 1,
	KLOG_LEVEL_INFO = 2,
	KLOG_LEVEL_NOTICE = 3,
	KLOG_LEVEL_WARN = 4,
	KLOG_LEVEL_ERROR = 5,
	KLOG_LEVEL_FATAL = 6
} klog_level_t;

/* Presentation profiles (Kconfig default + ir0.loglevel= override). */
typedef enum
{
	KLOG_PROFILE_QUIET = 0,
	KLOG_PROFILE_NORMAL = 1,
	KLOG_PROFILE_DEBUG = 2,
	KLOG_PROFILE_TRACE = 3
} klog_profile_t;

/* Runtime trace categories (ir0.trace=open_abi,vfs). */
#define KLOG_TRACE_OPEN_ABI (1u << 0)
#define KLOG_TRACE_VFS_STAT (1u << 1)
#define KLOG_TRACE_ALL ((uint32_t)-1)

/*
 * kprintf — Linux printk analogue: formatted kernel output to serial
 * (+ console screen when console_backend_printk_to_screen()).
 * Prefer klog_* / kprintf_level for [ts][LEVEL][COMP] lines.
 */
int kprintf(const char *fmt, ...);
int kvprintf(const char *fmt, va_list ap);
int kprintf_level(klog_level_t level, const char *comp, const char *fmt, ...);

/* Smoke/autokill tags (token preserved as substring). */
void klog_smoke(const char *tag);

/* Raw helpers (legacy audit migration; prefer klog_*_fmt). */
void klog_print(const char *str);
void klog_hex32(uint32_t num);
void klog_hex64(uint64_t num);

/*
 * Install an optional screen sink so the (serial-only) raw dump also renders on
 * the active console. Panic path only; pass NULL to detach. Kept as a callback
 * to avoid ktm→vga coupling.
 */
void klog_set_screen_sink(void (*sink)(const char *));

void klog_emit(klog_level_t level, const char *component, const char *message);
void klog_trace(const char *component, const char *message);
void klog_debug(const char *component, const char *message);
void klog_info(const char *component, const char *message);
void klog_notice(const char *component, const char *message);
void klog_warn(const char *component, const char *message);
void klog_error(const char *component, const char *message);
void klog_fatal(const char *component, const char *message);

void klog_trace_fmt(const char *component, const char *format, ...);
void klog_debug_fmt(const char *component, const char *format, ...);
void klog_info_fmt(const char *component, const char *format, ...);
void klog_notice_fmt(const char *component, const char *format, ...);
void klog_warn_fmt(const char *component, const char *format, ...);
void klog_error_fmt(const char *component, const char *format, ...);
void klog_fatal_fmt(const char *component, const char *format, ...);

void klog_set_level(klog_level_t level);
klog_level_t klog_get_level(void);
void klog_apply_profile(klog_profile_t profile);
klog_profile_t klog_get_profile(void);

void klog_set_trace_mask(uint32_t mask);
uint32_t klog_get_trace_mask(void);
int klog_trace_enabled(uint32_t category);

/* 1 = suppress serial until BOOT banner; 0 = normal. Default held at boot. */
void klog_boot_hold(int on);

typedef void (*klog_protocol_mirror_fn)(klog_level_t level, const char *component,
					const char *message);
void klog_set_protocol_mirror(klog_protocol_mirror_fn fn);

