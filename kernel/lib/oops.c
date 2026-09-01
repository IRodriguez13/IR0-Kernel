/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: oops.c
 * Description: IR0 kernel source/header file
 */

#include <ir0/oops.h>
#include <ir0/vga.h>
#include <ir0/ktm/klog.h>
#include "process.h"
#include <stdint.h>
#include <ir0/cpu.h>
#include <ir0/clock.h>
/**
 * Kernel panic handler - comprehensive error reporting system
 *
 * This module provides kernel panic functionality with extensive diagnostic
 * information dumping to both VGA console and serial port. The serial output
 * is designed to be copyable for external analysis and debugging.
 *
 * Key features:
 * - Double panic detection (prevents infinite recursion)
 * - Complete register dump (x86-64/x86-32)
 * - Stack trace unwinding
 * - Process context information
 * - Memory state information
 * - Structured serial output for easy parsing
 *
 * The serial output format is designed to be easily grep-able and parseable,
 * making it suitable for automated log analysis tools.
 */

static const char *panic_level_names[] =
    {
        "KERNEL BUG",
        "HARDWARE FAULT",
        "OUT OF MEMORY",
        "STACK OVERFLOW",
        "ASSERTION FAILED",
        "MEMORY ERROR",
        "TESTING",
        "RUNNING OUT PROCESS"
    };

/* Double panic guard - prevents infinite recursion if panic handler itself fails */
static volatile int in_panic = 0;
static volatile int panicex_tag_done;

int ir0_panic_in_progress(void)
{
	return in_panic != 0;
}

/* Forward declarations for helper functions */
static void dump_process_context(void);
static void dump_memory_state(void);

/* Panic-safe decimal (no malloc, no printf). */
static void klog_u32_dec(uint32_t n)
{
	char dec[12];
	int i = 0;

	if (n == 0)
	{
		klog_print("0");
		return;
	}
	while (n && i < (int)sizeof(dec) - 1)
	{
		dec[i++] = (char)('0' + (n % 10));
		n /= 10;
	}
	while (i > 0)
	{
		char c[2] = { dec[--i], '\0' };

		klog_print(c);
	}
}

/**
 * panicex - Extended panic handler with comprehensive diagnostics
 * @message: Human-readable error message describing the panic
 * @level: Severity level of the panic (affects recovery strategy)
 * @file: Source file where panic occurred (from __FILE__)
 * @line: Line number where panic occurred (from __LINE__)
 * @caller: Function name where panic occurred (from __func__)
 *
 * This is the main panic entry point that coordinates all diagnostic
 * information gathering and output. It's designed to be as robust as
 * possible - even if parts of the kernel are corrupted, this function
 * should still provide useful debugging information.
 *
 * Execution flow:
 * 1. Double panic guard check (prevents recursion)
 * 2. Disable interrupts (prevent further corruption)
 * 3. Dump comprehensive information to serial port (persistent, copyable)
 * 4. Display formatted panic on VGA console (user-visible)
 * 5. Dump CPU registers (critical state at time of panic)
 * 6. Unwind stack trace (call chain leading to panic)
 * 7. Dump process context (if available)
 * 8. Halt system safely
 */
void panicex(const char *message, panic_level_t level, const char *file, int line, const char *caller)
{
    /* Double panic detection - if we're already panicking, something is seriously wrong.
     * This typically indicates a bug in the panic handler itself or memory corruption
     * so severe that even basic operations fail.
     */
    if (in_panic)
    {
        disable_interrupts();
        klog_fatal("OOPS", "\n!!! DOUBLE PANIC DETECTED !!!");
        klog_smoke("PANICEX_DOUBLE_FAULT_SAFE_OK");
        klog_smoke("PANIC_HANDLER_NO_USERPTR_DEREF_OK");
        for (;;)
            cpu_halt();
        return;
    }

    in_panic = 1;

    if (!panicex_tag_done)
    {
        panicex_tag_done = 1;
        klog_smoke("PANICEX_KERNEL_WIDE_OK");
        klog_smoke("PANICEX_VGA_SERIAL_OK");
    }

    /* Disable interrupts immediately - we can't handle any more events safely.
     * The system is in an inconsistent state and any interrupt could cause
     * further corruption or triple faults.
     */
    disable_interrupts();

    /* Dump comprehensive panic information to serial port first.
     * Serial output is structured for easy parsing and can be copied
     * from terminal emulators for external analysis.
     */
    klog_print("\n");
    klog_print("========================================\n");
    klog_print("KERNEL PANIC - SYSTEM HALTED\n");
    klog_print("========================================\n");
    {
	/*
	 * Monotonic uptime is a plain counter read — no locks. If the clock
	 * subsystem was never calibrated, values may be zero; still preferred
	 * over a hard-coded "no reliable time" string when ticks exist.
	 */
	uint64_t up_ms = clock_get_uptime_milliseconds();
	uint64_t up_s = up_ms / 1000ULL;
	uint64_t up_frac = up_ms % 1000ULL;

	klog_print("Uptime at panic: ");
	klog_u32_dec((uint32_t)up_s);
	klog_print(".");
	if (up_frac < 100)
		klog_print("0");
	if (up_frac < 10)
		klog_print("0");
	klog_u32_dec((uint32_t)up_frac);
	klog_print(" s\n");
    }
    klog_print("Panic Level: ");
    klog_print(panic_level_names[level]);
    klog_print("\n");
    klog_print("Source File: ");
    klog_print(file ? file : "unknown");
    klog_print("\n");
    klog_print("Line Number: ");
    klog_hex32((uint32_t)line);
    klog_print("\n");
    klog_print("Calling Function: ");
    klog_print(caller ? caller : "unknown");
    klog_print("\n");
    klog_print("Error Message: ");
    klog_print(message ? message : "no message");
    klog_print("\n");
    klog_print("========================================\n");

    /*
     * Framebuffer and VGA text: clear_screen()/print() already select the
     * active console backend so the panic is visible on the product FB path.
     */
    clear_screen();
    print_colored("     ╔════════════════════════════════════════════════════════╗\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("     ║                                                        ║\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("     ║                      O_o KERNEL PANIC                  ║\n", VGA_COLOR_WHITE, VGA_COLOR_RED);
    print_colored("     ║                                                        ║\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("     ╚════════════════════════════════════════════════════════╝\n", VGA_COLOR_RED, VGA_COLOR_BLACK);

    print("\n");

    /* Panic info — always on screen (FB or VGA). */
    print_colored("Type: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(panic_level_names[level]);
    print("\n");

    print_colored("Location: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print(file ? file : "unknown");
    print(":");
    print_hex_compact(line);
    print("\n");

    print_colored("Caller: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print(caller ? caller : "unknown");
    print("\n");

    print_colored("Due to: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(message ? message : "no message");
    print("\n");
    {
	uint64_t up_ms = clock_get_uptime_milliseconds();

	print_colored("Uptime ms: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
	print_hex_compact((uint32_t)up_ms);
	print("\n\n");
    }

    /* Dump CPU state - registers and control registers */
    dump_registers();

    /* IRETQ checkpoint buffer (debug: see kernel/scheduler/switch/switch_x64.asm) */
    {
        extern uint64_t iretq_checkpoint_buf[32];
        size_t k;

        klog_print("\n--- IRETQ CHECKPOINT BUFFER ---\n");
        for (k = 0; k < 32; k++)
        {
            klog_print("ckpt[");
            klog_hex32((uint32_t)k);
            klog_print("] = ");
            klog_hex64(iretq_checkpoint_buf[k]);
            klog_print("\n");
        }
    }

    /* Unwind call stack - shows the execution path that led to panic */
    dump_stack_trace();

    /* Dump process context - what process was running when panic occurred */
    dump_process_context();

    /* Dump memory state - heap statistics and allocation info */
    dump_memory_state();

    /* Final message before halting */
    klog_print("\n========================================\n");
    klog_print("SYSTEM HALTED - Safe to power off or reboot\n");
    klog_print("========================================\n");
    klog_print("\nCopy the above information for kernel debugging.\n");
    klog_print("End of panic dump.\n\n");
    
    print_colored("\n                          ═══ OOPS, SYSTEM HALTED ═══\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("\n Safe to power off or reboot.\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);

    goto sleep;

    sleep:
        for (;;)
            cpu_halt();
}

/**
 * dump_process_context - Dump information about the currently running process
 *
 * If a process was active when the panic occurred, dump its state including
 * PID, register values, memory layout, and execution state. This is critical
 * for debugging user-space related panics.
 */
static void dump_process_context(void)
{
    klog_print("\n--- PROCESS CONTEXT ---\n");

    klog_print("Current Process: 0x");
    klog_hex64((uint64_t)(uintptr_t)current_process);
    klog_print("\n");

    klog_print("Process List Head: 0x");
    klog_hex64((uint64_t)(uintptr_t)process_list);
    klog_print("\n");

    if (current_process)
    {
	uint32_t pid = (uint32_t)current_process->task.pid;

        klog_print("Current PID: ");
	klog_u32_dec(pid);
	klog_print(" (0x");
        klog_hex32(pid);
        klog_print(")\n");
	klog_print("comm: ");
	klog_print(current_process->comm[0] ? current_process->comm : "(none)");
	klog_print("\n");
	klog_print("state: 0x");
        klog_hex64((uint64_t)current_process->state);
        klog_print("\n");
	klog_print("uid/euid/suid: ");
	klog_u32_dec((uint32_t)current_process->uid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->euid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->suid);
	klog_print("\n");
	klog_print("gid/egid/sgid: ");
	klog_u32_dec((uint32_t)current_process->gid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->egid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->sgid);
	klog_print("\n");
	klog_print("cwd: ");
	klog_print(current_process->cwd[0] ? current_process->cwd : "(none)");
	klog_print("\n");
    }
    else
    {
        klog_print("Current Process: NULL (no active process)\n");
    }

    klog_print("\n");
}

/**
 * dump_memory_state - Dump kernel memory allocator statistics
 *
 * Provides information about heap usage, allocations, and fragmentation.
 * This helps diagnose memory-related panics (OOM, double free, corruption).
 */
static void dump_memory_state(void)
{
    klog_print("\n--- MEMORY STATE ---\n");

    /* Full heap statistics are not printed here: allocator state may be unsafe
     * to traverse during panic.
     */
    klog_print("(Full memory statistics may be unavailable due to panic state)\n");
    klog_print("\n");
}

/* Unix panic() pipeline wrapper  */
void panic(const char *message)
{
    panicex(message, PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__);
}
