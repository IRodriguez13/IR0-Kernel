/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: host_klog_stub.c
 * Description: No-op klog stubs for host-linked kernel sources
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/klog.h>
#include <stdarg.h>
#include <time.h>

void klog_boot_hold(int on)
{
	(void)on;
}

void klog_set_level(klog_level_t level)
{
	(void)level;
}

klog_level_t klog_get_level(void)
{
	return KLOG_LEVEL_INFO;
}

void klog_apply_profile(klog_profile_t profile)
{
	(void)profile;
}

klog_profile_t klog_get_profile(void)
{
	return KLOG_PROFILE_NORMAL;
}

void klog_set_trace_mask(uint32_t mask)
{
	(void)mask;
}

uint32_t klog_get_trace_mask(void)
{
	return 0;
}

int klog_trace_enabled(uint32_t category)
{
	(void)category;
	return 0;
}

void klog_set_protocol_mirror(klog_protocol_mirror_fn fn)
{
	(void)fn;
}

void klog_print(const char *str)
{
	(void)str;
}

void klog_hex32(uint32_t num)
{
	(void)num;
}

void klog_hex64(uint64_t num)
{
	(void)num;
}

void klog_smoke(const char *tag)
{
	(void)tag;
}

void klog_trace(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_debug(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_info(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_notice(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_warn(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_error(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_fatal(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_trace_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_debug_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_info_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_notice_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_warn_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_error_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_fatal_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

/*
 * Pseudo-FS stat stamps entries with the wall clock. Host tests only assert
 * the registry's routing, so a fixed epoch keeps them deterministic.
 */
time_t clock_get_current_time(void)
{
	return (time_t)1735689600; /* 2025-01-01T00:00:00Z */
}
