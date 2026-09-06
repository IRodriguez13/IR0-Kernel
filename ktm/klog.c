/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: klog.c
 * Description: KTM kernel logging hub — kprintf + levelled human channel.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ir0/early_clock.h>
#include <ir0/clock.h>
#include <ir0/ktm/klog.h>
#include <ir0/klog_event.h>
#include <ir0/kmem.h>
#include <ir0/serial_io.h>
#include <ir0/clock.h>
#include <ir0/console_backend.h>
#include <ir0/typewriter.h>

static klog_level_t g_klog_level = KLOG_LEVEL_INFO;
static klog_profile_t g_klog_profile = KLOG_PROFILE_NORMAL;
static uint32_t g_klog_trace_mask;
static klog_protocol_mirror_fn g_protocol_mirror;
/* Suppress serial until BOOT banner (see klog_boot_hold in kmain). */
static int g_boot_hold = 1;

#define KLOG_EARLY_RECORDS 256
#define KLOG_NORMAL_RECORDS 1024
static klog_record_t g_records[KLOG_EARLY_RECORDS];
static klog_record_t *g_record_ring = g_records;
static size_t g_record_capacity = KLOG_EARLY_RECORDS;
static uint64_t g_record_sequence;
static size_t g_record_head;
static size_t g_record_count;
static klog_boot_phase_t g_boot_phase = KLOG_BOOT_EARLY_ARCH;

void klog_boot_hold(int on)
{
	g_boot_hold = on ? 1 : 0;
}

void klog_set_boot_phase(klog_boot_phase_t phase)
{
	if (phase <= KLOG_BOOT_READY)
		g_boot_phase = phase;
}

klog_boot_phase_t klog_get_boot_phase(void)
{
	return g_boot_phase;
}

const char *klog_boot_phase_string(klog_boot_phase_t phase)
{
	switch (phase)
	{
	case KLOG_BOOT_EARLY_ARCH:
		return "EARLY_ARCH";
	case KLOG_BOOT_MEMORY:
		return "MEMORY";
	case KLOG_BOOT_PLATFORM:
		return "PLATFORM";
	case KLOG_BOOT_TIME:
		return "TIME";
	case KLOG_BOOT_INTERRUPTS:
		return "INTERRUPTS";
	case KLOG_BOOT_BUS_ENUMERATION:
		return "BUS_ENUM";
	case KLOG_BOOT_DRIVERS:
		return "DRIVERS";
	case KLOG_BOOT_STORAGE:
		return "STORAGE";
	case KLOG_BOOT_ROOTFS:
		return "ROOTFS";
	case KLOG_BOOT_USERSPACE:
		return "USERSPACE";
	case KLOG_BOOT_READY:
		return "READY";
	default:
		return "UNKNOWN";
	}
}

static const char *klog_level_string(klog_level_t level)
{
	switch (level)
	{
	case KLOG_LEVEL_TRACE:
		return "TRACE";
	case KLOG_LEVEL_DEBUG:
		return "DEBUG";
	case KLOG_LEVEL_INFO:
		return "INFO";
	case KLOG_LEVEL_NOTICE:
		return "NOTICE";
	case KLOG_LEVEL_WARN:
		return "WARN";
	case KLOG_LEVEL_ERROR:
		return "ERROR";
	case KLOG_LEVEL_FATAL:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

static void klog_write_raw(const char *str)
{
	/*
	 * Serial only. Mirroring to console_backend here re-enters UI paths and
	 * duplicated every line on -serial stdio smokes. Screen output stays on
	 * console_puts/typewriter; use kprintf only as the log channel.
	 */
	if (!str || g_boot_hold)
		return;
	serial_print(str);
}

static int klog_should_mirror_screen(const klog_record_t *record)
{
	if (record->severity >= KLOG_LEVEL_NOTICE)
		return 1;
	return strcmp(record->component, "BOOT") == 0 ||
	       strcmp(record->component, "INIT") == 0 ||
	       strcmp(record->component, "CLOCK") == 0 ||
	       strcmp(record->component, "DRIVERS") == 0 ||
	       strcmp(record->component, "PLATFORM") == 0 ||
	       strcmp(record->component, "HYPERVISOR") == 0;
}

static void klog_append_u64(char *out, size_t out_sz, size_t *off,
			    uint64_t value, unsigned min_width)
{
	char rev[24];
	unsigned n = 0;

	do
	{
		rev[n++] = (char)('0' + (value % 10));
		value /= 10;
	} while (value && n < sizeof(rev));
	while (n < min_width && n < sizeof(rev))
		rev[n++] = '0';
	while (n > 0 && *off + 1 < out_sz)
		out[(*off)++] = rev[--n];
	out[*off] = '\0';
}

static void klog_append_text(char *out, size_t out_sz, size_t *off,
			     const char *text)
{
	size_t i;

	if (!text || *off >= out_sz)
		return;
	for (i = 0; text[i] && *off + 1 < out_sz; i++)
		out[(*off)++] = text[i];
	out[*off] = '\0';
}

static void klog_append_record_timestamp(const klog_record_t *record, char *out,
					 size_t out_sz, size_t *off)
{
	uint64_t uptime_ms;
	uint64_t seconds;
	uint32_t milliseconds;

	if (record->clock_state != KLOG_CLOCK_MONOTONIC)
	{
		klog_append_text(out, out_sz, off, "[    ?.???]");
		return;
	}

	uptime_ms = record->timestamp_ns / 1000000ULL;
	seconds = uptime_ms / 1000;
	milliseconds = (uint32_t)(uptime_ms % 1000);
	klog_append_text(out, out_sz, off, "[");
	klog_append_u64(out, out_sz, off, seconds, 1);
	klog_append_text(out, out_sz, off, ".");
	klog_append_u64(out, out_sz, off, milliseconds, 3);
	klog_append_text(out, out_sz, off, "]");
}

static size_t klog_render_record(const klog_record_t *record, char *line,
				 size_t line_size)
{
	size_t off = 0;
	int n;

	klog_append_text(line, line_size, &off, "[#");
	klog_append_u64(line, line_size, &off, record->sequence, 6);
	klog_append_text(line, line_size, &off, "] [");
	klog_append_text(line, line_size, &off,
			 klog_boot_phase_string((klog_boot_phase_t)record->boot_phase));
	klog_append_text(line, line_size, &off, "] ");
	klog_append_record_timestamp(record, line, line_size, &off);
	n = snprintf(line + off, line_size - off, " [%s] [%s] %s\n",
		     klog_level_string((klog_level_t)record->severity),
		     record->component, record->message);
	if (n > 0)
		off += (size_t)n;
	if (off >= line_size)
		off = line_size - 1;
	line[off] = '\0';
	return off;
}

/*
 * Optional screen sink for the panic dump. klog_* is serial-only by design;
 * during a panic oops.c installs print_screen_only here so the same text also
 * reaches the FB/GTK console. NULL in normal operation (no extra work, no
 * ktm→vga coupling: the sink is a plain callback owned by the caller).
 */
static void (*g_klog_screen_sink)(const char *);

void klog_set_screen_sink(void (*sink)(const char *))
{
	g_klog_screen_sink = sink;
}

/* Fixed-width uppercase hex, matching serial_print_hex32/64 (no 0x prefix). */
static void klog_screen_hex(uint64_t num, int nibbles)
{
	static const char hex[] = "0123456789ABCDEF";
	char buf[17];
	int i;

	if (nibbles > 16)
		nibbles = 16;
	for (i = nibbles - 1; i >= 0; i--)
	{
		buf[i] = hex[num & 0xF];
		num >>= 4;
	}
	buf[nibbles] = '\0';
	g_klog_screen_sink(buf);
}

void klog_print(const char *str)
{
	serial_print(str);
	if (g_klog_screen_sink)
		g_klog_screen_sink(str);
}

void klog_hex32(uint32_t num)
{
	serial_print_hex32(num);
	if (g_klog_screen_sink)
		klog_screen_hex(num, 8);
}

void klog_hex64(uint64_t num)
{
	serial_print_hex64(num);
	if (g_klog_screen_sink)
		klog_screen_hex(num, 16);
}

void klog_set_level(klog_level_t level)
{
	if (level > KLOG_LEVEL_FATAL)
		level = KLOG_LEVEL_INFO;
	g_klog_level = level;
}

klog_level_t klog_get_level(void)
{
	return g_klog_level;
}

void klog_apply_profile(klog_profile_t profile)
{
	g_klog_profile = profile;
	switch (profile)
	{
	case KLOG_PROFILE_QUIET:
		g_klog_level = KLOG_LEVEL_NOTICE;
		break;
	case KLOG_PROFILE_DEBUG:
		g_klog_level = KLOG_LEVEL_DEBUG;
		break;
	case KLOG_PROFILE_TRACE:
		g_klog_level = KLOG_LEVEL_TRACE;
		g_klog_trace_mask = KLOG_TRACE_ALL;
		break;
	case KLOG_PROFILE_NORMAL:
	default:
		g_klog_profile = KLOG_PROFILE_NORMAL;
		g_klog_level = KLOG_LEVEL_INFO;
		break;
	}
}

klog_profile_t klog_get_profile(void)
{
	return g_klog_profile;
}

void klog_set_trace_mask(uint32_t mask)
{
	g_klog_trace_mask = mask;
}

uint32_t klog_get_trace_mask(void)
{
	return g_klog_trace_mask;
}

int klog_trace_enabled(uint32_t category)
{
	if (g_klog_level > KLOG_LEVEL_TRACE)
		return 0;
	if (!category)
		return 1;
	return (g_klog_trace_mask & category) != 0;
}

void klog_set_protocol_mirror(klog_protocol_mirror_fn fn)
{
	g_protocol_mirror = fn;
}

static klog_record_t *klog_capture_record(klog_level_t level,
					  uint32_t event_id,
					  uint16_t subsystem,
					  const char *component,
					  const char *message)
{
	klog_record_t *record;
	size_t slot;

	slot = __atomic_fetch_add(&g_record_head, 1, __ATOMIC_RELAXED);
	record = &g_record_ring[slot % g_record_capacity];
	memset(record, 0, sizeof(*record));
	record->sequence = __atomic_add_fetch(&g_record_sequence, 1,
					     __ATOMIC_RELAXED);
	record->event_id = event_id;
	record->subsystem = subsystem;
	record->severity = (uint8_t)level;
	record->boot_phase = (uint8_t)g_boot_phase;
	record->cpu_id = 0;
	if (clock_is_ready())
	{
		record->clock_state = KLOG_CLOCK_MONOTONIC;
		record->timestamp_ns =
			clock_get_uptime_milliseconds() * 1000000ULL;
	}
	else if (early_clock_available())
	{
		record->clock_state = KLOG_CLOCK_RAW;
		record->raw_ticks = early_clock_read();
	}
	else
	{
		record->clock_state = KLOG_CLOCK_UNAVAILABLE;
	}
	strncpy(record->component, component, sizeof(record->component) - 1);
	strncpy(record->message, message, sizeof(record->message) - 1);
	if (g_record_count < g_record_capacity)
		__atomic_add_fetch(&g_record_count, 1, __ATOMIC_RELAXED);
	return record;
}

int klog_promote_normal_ring(void)
{
	klog_record_t *normal;
	size_t count;
	size_t head;
	size_t start;
	size_t i;

	if (g_record_capacity != KLOG_EARLY_RECORDS)
		return 0;
	normal = kmalloc_try(sizeof(*normal) * KLOG_NORMAL_RECORDS);
	if (!normal)
		return -1;
	memset(normal, 0, sizeof(*normal) * KLOG_NORMAL_RECORDS);
	count = g_record_count;
	head = g_record_head;
	start = head > count ? head - count : 0;
	for (i = 0; i < count; i++)
		normal[i] = g_record_ring[(start + i) % g_record_capacity];
	g_record_ring = normal;
	g_record_capacity = KLOG_NORMAL_RECORDS;
	g_record_head = count;
	return 0;
}

size_t klog_record_count(void)
{
	return g_record_count;
}

int klog_read_records(char *buf, size_t size)
{
	size_t count;
	size_t head;
	size_t start;
	size_t off = 0;
	size_t i;

	if (!buf || size == 0)
		return -1;
	count = g_record_count;
	head = g_record_head;
	start = head > count ? head - count : 0;
	for (i = 0; i < count && off + 1 < size; i++)
	{
		char line[256];
		size_t line_len;
		size_t copy_len;
		klog_record_t snapshot;

		snapshot = g_record_ring[(start + i) % g_record_capacity];
		line_len = klog_render_record(&snapshot, line, sizeof(line));
		copy_len = line_len;
		if (copy_len > size - off - 1)
			copy_len = size - off - 1;
		memcpy(buf + off, line, copy_len);
		off += copy_len;
	}
	buf[off] = '\0';
	return (int)off;
}

void klog_event(uint32_t event_id, uint16_t subsystem, uint8_t severity,
		const char *component, const char *message)
{
	char line[256];
	klog_record_t *record;
	klog_level_t level = (klog_level_t)severity;

	if (level > KLOG_LEVEL_FATAL)
		level = KLOG_LEVEL_INFO;
	if (level < g_klog_level)
		return;
	if (!component)
		component = "?";
	if (!message)
		message = "";

	record = klog_capture_record(level, event_id, subsystem, component,
				     message);
	(void)klog_render_record(record, line, sizeof(line));
	klog_write_raw(line);
	if (!g_boot_hold && console_backend_printk_to_screen() &&
	    klog_should_mirror_screen(record))
		typewriter_vga_print(line, 0x07);

	if (g_protocol_mirror)
		g_protocol_mirror(level, component, message);
}

void klog_emit(klog_level_t level, const char *component, const char *message)
{
	klog_event(0, 0, (uint8_t)level, component, message);
}

void klog_trace(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_TRACE, component, message);
}

void klog_debug(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_DEBUG, component, message);
}

void klog_info(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_INFO, component, message);
}

void klog_notice(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_NOTICE, component, message);
}

void klog_warn(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_WARN, component, message);
}

void klog_error(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_ERROR, component, message);
}

void klog_fatal(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_FATAL, component, message);
}

void klog_smoke(const char *tag)
{
	char line[256];
	klog_record_t *record;

	/*
	 * Smoke/autokill tags must always reach serial regardless of profile
	 * (quiet/normal/debug/trace).
	 */
	if (!tag)
		tag = "?";
	record = klog_capture_record(KLOG_LEVEL_INFO, 0, 0, "SMOKE", tag);
	(void)klog_render_record(record, line, sizeof(line));
	klog_write_raw(line);
	if (g_protocol_mirror)
		g_protocol_mirror(KLOG_LEVEL_INFO, "SMOKE", tag);
}

static void klog_vfmt(klog_level_t level, const char *component, const char *format,
		      va_list args)
{
	char message[512];

	if (level < g_klog_level)
		return;
	vsnprintf(message, sizeof(message), format, args);
	klog_emit(level, component, message);
}

void klog_trace_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_TRACE, component, format, args);
	va_end(args);
}

void klog_debug_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_DEBUG, component, format, args);
	va_end(args);
}

void klog_info_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_INFO, component, format, args);
	va_end(args);
}

void klog_notice_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_NOTICE, component, format, args);
	va_end(args);
}

void klog_warn_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_WARN, component, format, args);
	va_end(args);
}

void klog_error_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_ERROR, component, format, args);
	va_end(args);
}

void klog_fatal_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_FATAL, component, format, args);
	va_end(args);
}

int kprintf_level(klog_level_t level, const char *comp, const char *fmt, ...)
{
	va_list args;
	char message[512];
	int n;

	if (level < g_klog_level)
		return 0;
	va_start(args, fmt);
	n = vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	klog_emit(level, comp ? comp : "KERN", message);
	return n;
}

int kvprintf(const char *fmt, va_list ap)
{
	char buf[512];
	int n;

	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	klog_write_raw(buf);
	return n;
}

int kprintf(const char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = kvprintf(fmt, args);
	va_end(args);
	return n;
}
