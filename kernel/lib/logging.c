/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: logging.c
 * Description: IR0 kernel source/header file
 */

#include <ir0/logging.h>
#include <ir0/vga.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ir0/serial_io.h>
#include <ir0/clock.h>
#include <ir0/klog.h>
#include <ir0/kmem.h>
#include <stddef.h>
#include <stdint.h>

/* LOG BUFFER CONFIGURATION */
#define LOG_BUFFER_MAX_ENTRIES 1024  // Maximum number of log entries to store
#define LOG_BUFFER_ENTRY_SIZE  256   // Maximum size per log entry

/* LOG ENTRY STRUCTURE */
typedef struct {
    uint64_t timestamp_ms;      // Timestamp in milliseconds
    log_level_t level;           // Log level
    char component[32];          // Component name (truncated if needed)
    char message[LOG_BUFFER_ENTRY_SIZE - 32 - 8 - 4];  // Message text
} log_entry_t;

/* GLOBAL VARIABLES */

static log_level_t current_log_level = LOG_LEVEL_INFO;
static bool logging_initialized = false;

/* Circular log buffer */
static log_entry_t *log_buffer = NULL;
static size_t log_buffer_head = 0;    // Next position to write
static size_t log_buffer_count = 0;   // Number of entries in buffer
static bool log_buffer_wrapped = false; // True if buffer has wrapped around

/* INTERNAL FUNCTIONS */
static size_t uint64_to_dec(char *buf, size_t buf_size, uint64_t value);

static const char *get_level_string(log_level_t level)
{
    switch (level)
    {
    case LOG_LEVEL_TRACE:
        return "TRACE";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_NOTICE:
        return "NOTICE";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

/* PUBLIC FUNCTIONS */

void logging_init(void)
{
    if (logging_initialized)
    {
        return;
    }

    /* Allocate log buffer */
    size_t buffer_size = sizeof(log_entry_t) * LOG_BUFFER_MAX_ENTRIES;
    log_buffer = (log_entry_t *)kmalloc(buffer_size);
    if (!log_buffer)
    {
        /* If allocation fails, logging will still work but buffer won't */
        log_buffer = NULL;
        klog_warn_fmt("LOGGING", "Failed to allocate log buffer (size: %u bytes)",
                      (unsigned)buffer_size);
    }
    else
    {
        memset(log_buffer, 0, buffer_size);
        log_buffer_head = 0;
        log_buffer_count = 0;
        log_buffer_wrapped = false;
        klog_info_fmt("LOGGING", "Log buffer allocated successfully (%u entries)",
                      (unsigned)LOG_BUFFER_MAX_ENTRIES);
    }

    current_log_level = LOG_LEVEL_INFO;
    logging_initialized = true;
}

void logging_set_level(log_level_t level)
{
    /* Single source of truth: klog owns the filter threshold. */
    current_log_level = level;
    klog_set_level((klog_level_t)level);
}

log_level_t logging_get_level(void)
{
    current_log_level = (log_level_t)klog_get_level();
    return current_log_level;
}

void log_message(log_level_t level, const char *component, const char *message)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    /* Filter via klog global so profiles apply to LOG_* macros too. */
    if ((klog_level_t)level < klog_get_level())
    {
        return;
    }

    /* Store log entry in circular buffer */
    if (log_buffer)
    {
        log_entry_t *entry = &log_buffer[log_buffer_head];
        entry->timestamp_ms = clock_get_uptime_milliseconds();
        entry->level = level;
        
        /* Copy component name (truncate if too long) */
        size_t comp_len = strlen(component);
        if (comp_len >= sizeof(entry->component))
            comp_len = sizeof(entry->component) - 1;
        memcpy(entry->component, component, comp_len);
        entry->component[comp_len] = '\0';
        
        /* Copy message (truncate if too long) */
        size_t msg_len = strlen(message);
        size_t max_msg_len = sizeof(entry->message) - 1;
        if (msg_len >= max_msg_len)
            msg_len = max_msg_len;
        memcpy(entry->message, message, msg_len);
        entry->message[msg_len] = '\0';
        
        /* Advance buffer head */
        log_buffer_head = (log_buffer_head + 1) % LOG_BUFFER_MAX_ENTRIES;
        if (log_buffer_count < LOG_BUFFER_MAX_ENTRIES)
        {
            log_buffer_count++;
        }
        else
        {
            log_buffer_wrapped = true;
        }
    }

    /* Human channel via klog (honest timestamps; optional KTM mirror). */
    klog_emit((klog_level_t)level, component, message);
}

void log_debug(const char *component, const char *message)
{
    log_message(LOG_LEVEL_DEBUG, component, message);
}

void log_info(const char *component, const char *message)
{
    log_message(LOG_LEVEL_INFO, component, message);
}

void log_warn(const char *component, const char *message)
{
    log_message(LOG_LEVEL_WARN, component, message);
}

void log_error(const char *component, const char *message)
{
    log_message(LOG_LEVEL_ERROR, component, message);
}

void log_fatal(const char *component, const char *message)
{
    log_message(LOG_LEVEL_FATAL, component, message);
}

void log_debug_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_DEBUG < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_DEBUG, component, message);
}

void log_info_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_INFO < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_INFO, component, message);
}

void log_warn_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_WARN < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_WARN, component, message);
}

void log_error_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_ERROR < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_ERROR, component, message);
}

void log_fatal_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_FATAL < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_FATAL, component, message);
}

/* =============================================================================== */
/* SYSTEM-SPECIFIC LOGGING */
/* =============================================================================== */

void log_syscall(const char *syscall, int result, const char *args)
{
    char message[256];
    snprintf(message, sizeof(message), "SYSCALL: %s(%s) = %d", syscall, args ? args : "", result);
    log_debug("SYSCALL", message);
}

void log_filesystem_op(const char *op, const char *path, int result)
{
    char message[256];
    snprintf(message, sizeof(message), "FS: %s('%s') = %d", op, path ? path : "", result);
    log_debug("FILESYSTEM", message);
}

void log_memory_op(const char *op, void *ptr, size_t size, int result)
{
    char message[256];
    char ptr_buf[32];
    char size_buf[24];
    uintptr_t ptr_val = (uintptr_t)ptr;

    if (sizeof(uintptr_t) > sizeof(uint32_t))
    {
        unsigned hi = (unsigned)((ptr_val >> 32) & 0xFFFFFFFFu);
        unsigned lo = (unsigned)(ptr_val & 0xFFFFFFFFu);
        snprintf(ptr_buf, sizeof(ptr_buf), "0x%08x%08x", hi, lo);
    }
    else
    {
        unsigned lo = (unsigned)(ptr_val & 0xFFFFFFFFu);
        snprintf(ptr_buf, sizeof(ptr_buf), "0x%08x", lo);
    }

    uint64_to_dec(size_buf, sizeof(size_buf), (uint64_t)size);
    snprintf(message, sizeof(message), "MEM: %s(ptr=%s, size=%s) = %d",
             op ? op : "op", ptr_buf, size_buf, result);
    log_debug("MEMORY", message);
}

void log_interrupt(uint8_t irq, const char *handler, int result)
{
    char message[256];
    snprintf(message, sizeof(message), "IRQ: %d handled by %s = %d", irq, handler ? handler : "unknown", result);
    log_debug("INTERRUPT", message);
}

void log_subsystem_ok(const char *subsystem_name)
{
    log_info(subsystem_name, "Registered and Initialized OK");
}

/* =============================================================================== */
/* LOG BUFFER ACCESS FUNCTIONS (for dmesg/journalctl-like functionality) */
/* =============================================================================== */

/**
 * logging_print_buffer - Print all logs from the circular buffer
 * 
 * This function prints all log entries stored in the circular buffer,
 * similar to dmesg or journalctl. Logs are printed in chronological order.
 */
void logging_print_buffer(void)
{
    if (!log_buffer)
    {
        print("Log buffer not allocated (no memory available)\n");
        return;
    }
    
    if (log_buffer_count == 0)
    {
        print("No log entries available (buffer is empty)\n");
        print("Buffer initialized: ");
        print(logging_initialized ? "yes" : "no");
        print("\n");
        return;
    }

    size_t start_idx;
    size_t count = log_buffer_count;
    
    /* Determine start index */
    if (log_buffer_wrapped)
    {
        /* Buffer has wrapped, start from head (oldest entry) */
        start_idx = log_buffer_head;
    }
    else
    {
        /* Buffer hasn't wrapped, start from beginning */
        start_idx = 0;
    }

    /* Print header */
    print("=== Kernel Log Buffer (dmesg) ===\n");
    print("Entries: ");
    char buf[32];
    itoa((int)log_buffer_count, buf, 10);
    print(buf);
    print("\n");
    print("-----------------------------------\n");

    /* Print all entries in chronological order */
    for (size_t i = 0; i < count; i++)
    {
        size_t idx = (start_idx + i) % LOG_BUFFER_MAX_ENTRIES;
        log_entry_t *entry = &log_buffer[idx];
        
        /* Format timestamp: [SSSS.mmm] */
        uint64_t seconds = entry->timestamp_ms / 1000;
        uint32_t milliseconds = (uint32_t)(entry->timestamp_ms % 1000);
        
        print("[");
        print_uint64(seconds);
        print(".");
        
        /* Print milliseconds with leading zeros (3 digits) */
        char ms_str[4];
        ms_str[0] = '0' + (char)((milliseconds / 100) % 10);
        ms_str[1] = '0' + (char)((milliseconds / 10) % 10);
        ms_str[2] = '0' + (char)(milliseconds % 10);
        ms_str[3] = '\0';
        print(ms_str);
        print("] ");
        
        /* Print level */
        print("[");
        print(get_level_string(entry->level));
        print("] ");
        
        /* Print component */
        print("[");
        print(entry->component);
        print("] ");
        
        /* Print message */
        print(entry->message);
        print("\n");
    }
}

/**
 * logging_get_buffer_size - Get number of log entries in buffer
 * @return: Number of log entries currently stored
 */
size_t logging_get_buffer_size(void)
{
    return log_buffer_count;
}

/*
 * uint64_to_dec - Write decimal string of value into buf, null-terminated.
 * Returns length written (excluding null). Does not write past buf_size-1.
 */
static size_t uint64_to_dec(char *buf, size_t buf_size, uint64_t value)
{
    char tmp[24];
    size_t i = 0;
    if (value == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (value && i < sizeof(tmp) - 1)
        {
            tmp[i++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    size_t len = i;
    if (len >= buf_size)
        len = buf_size - 1;
    for (size_t j = 0; j < len; j++)
        buf[j] = tmp[len - 1 - j];
    buf[len] = '\0';
    return len;
}

/**
 * logging_read_buffer - Fill buffer with formatted log entries (for /proc/kmsg)
 * @buf: Output buffer
 * @size: Size of buf
 * @return: Number of bytes written, or negative on error
 *
 * Same format as logging_print_buffer but written to buf for read() syscall.
 */
int logging_read_buffer(char *buf, size_t size)
{
    if (!buf || size == 0)
        return -1;
    if (!log_buffer)
    {
        size_t n = 0;
        if (size > 1)
            n = (size_t)snprintf(buf, size, "Log buffer not allocated\n");
        return (int)n;
    }
    if (log_buffer_count == 0)
    {
        size_t n = 0;
        if (size > 1)
            n = (size_t)snprintf(buf, size, "No log entries (buffer empty)\n");
        return (int)n;
    }

    size_t start_idx;
    size_t count = log_buffer_count;

    if (log_buffer_wrapped)
        start_idx = log_buffer_head;
    else
        start_idx = 0;

    size_t off = 0;
    for (size_t i = 0; i < count && off < size; i++)
    {
        size_t idx = (start_idx + i) % LOG_BUFFER_MAX_ENTRIES;
        log_entry_t *entry = &log_buffer[idx];

        uint64_t seconds = entry->timestamp_ms / 1000;
        uint32_t ms = (uint32_t)(entry->timestamp_ms % 1000);
        char sec_buf[24];
        uint64_to_dec(sec_buf, sizeof(sec_buf), seconds);

        int n = snprintf(buf + off, off < size ? size - off : 0,
            "[%s.%03u] [%s] [%s] %s\n",
            sec_buf, ms, get_level_string(entry->level),
            entry->component, entry->message);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    return (int)off;
}
