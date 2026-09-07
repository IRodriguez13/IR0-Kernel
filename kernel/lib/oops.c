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
#include <ir0/console_backend.h>
#include <ir0/ktm/klog.h>
#include "process.h"
#include <ir0/arch_task.h>
#include <stdint.h>
#include <ir0/cpu.h>
#include <ir0/clock.h>
#include <ir0/arch_io.h>
#include <stddef.h>
#include <string.h>
#include <config.h>
#include <mm/paging.h>

/* Exception frame captured before panicex (CPU fault site, not reporter). */
static struct
{
	int valid;
	int stack_overflow;
	unsigned vector;
	uint32_t pid;
	uint64_t err;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
	uint64_t cr2; /* frozen at note time — nested #DF must not rewrite */
	char comm[16];
} panic_exc_frame;

void panic_note_exception_frame(unsigned vector, unsigned long long err,
				unsigned long long rip, unsigned long long cs,
				unsigned long long rflags,
				unsigned long long rsp, unsigned long long ss,
				int stack_overflow, unsigned int pid,
				const char *comm)
{
	size_t i;

	/*
	 * Keep the first (primary) frame. Nested #DF during panic dump must
	 * not replace a kernel #PF / #GP site with dump_stack_trace's RIP.
	 * Also freeze CR2 here: reading CR2 later in panicex sees the nested
	 * fault address and falsely makes every panic look like a stack #DF.
	 */
	if (panic_exc_frame.valid)
		return;

	panic_exc_frame.valid = 1;
	panic_exc_frame.stack_overflow = stack_overflow ? 1 : 0;
	panic_exc_frame.vector = vector;
	panic_exc_frame.err = err;
	panic_exc_frame.rip = rip;
	panic_exc_frame.cs = cs;
	panic_exc_frame.rflags = rflags;
	panic_exc_frame.rsp = rsp;
	panic_exc_frame.ss = ss;
	/*
	 * CR2 is only defined for #PF (vector 14). Freezing live CR2 on #UD/#GP/#DF
	 * reprints a stale userspace address and makes every panic look like an
	 * MM fault (false positive).
	 */
	panic_exc_frame.cr2 =
		(vector == 14) ? (uint64_t)read_fault_address() : 0;
	panic_exc_frame.pid = pid;
	panic_exc_frame.comm[0] = '\0';
	if (comm)
	{
		for (i = 0; i < sizeof(panic_exc_frame.comm) - 1 && comm[i]; i++)
			panic_exc_frame.comm[i] = comm[i];
		panic_exc_frame.comm[i] = '\0';
	}
}

unsigned long long ir0_panic_fault_rsp(void)
{
	return panic_exc_frame.valid ? panic_exc_frame.rsp : 0;
}

void ir0_log_user_fault_frame(unsigned vector, unsigned long long cr2,
			      unsigned long long err, unsigned long long rip,
			      unsigned long long cs, unsigned long long rsp,
			      int present, int write, int exec,
			      unsigned int pid, const char *comm)
{
	const char *classify = "USER_FAULT";
	process_t *cur = process_get_current();
	unsigned char opc = 0;
	unsigned long long retaddr = 0;
	int have_opc = 0;
	int have_ret = 0;

	/*
	 * present/write/exec are page-fault error bits. For #GP/#UD they are
	 * always passed as 0 from the ISR — do not mislabel as USER_NOT_PRESENT.
	 */
	if (vector == 13)
		classify = "USER_GENERAL_PROTECTION";
	else if (vector == 6)
		classify = "USER_INVALID_OPCODE";
	else if (vector == 0 || vector == 4 || vector == 19)
		classify = "USER_ARITHMETIC";
	else if (cr2 >= (unsigned long long)USER_STACK_TOP &&
		 cr2 < (unsigned long long)USER_STACK_TOP + (unsigned long long)PAGE_SIZE_4KB)
		classify = "USER_STACK_TOP_OVERRUN";
	else if (cr2 >= (unsigned long long)IR0_KSTACK_VA_BASE &&
		 cr2 < (unsigned long long)IR0_KSTACK_VA_BASE +
			   (unsigned long long)IR0_KSTACK_MAX_SLOTS *
				   (unsigned long long)IR0_KSTACK_SLOT_SIZE)
		classify = "USER_TOUCHED_KSTACK_VA";
	else if (!present)
		classify = "USER_NOT_PRESENT";
	else if (write)
		classify = "USER_PROT_WRITE";
	else if (exec)
		classify = "USER_PROT_EXEC";
	else
		classify = "USER_PROT_READ";

	/*
	 * Peek opcode + [rsp] return address when the faulting mm is current.
	 * #GP: also detect musl a_crash / abort fallback (endbr64; hlt).
	 * #PF: retaddr identifies the caller (e.g. setjmp site → lineedit/ash).
	 */
	if ((vector == 13 || vector == 14) && cur && process_pgd(cur))
	{
		if (vector == 13 &&
		    copy_from_user_region_in_directory(process_pgd(cur),
						      (uintptr_t)rip, &opc, 1) == 0)
		{
			have_opc = 1;
			if (opc == 0xf4)
				classify = "USER_ABORT_HLT";
		}
		if (copy_from_user_region_in_directory(process_pgd(cur),
						       (uintptr_t)rsp, &retaddr,
						       sizeof(retaddr)) == 0)
			have_ret = 1;
	}

	/*
	 * INFO (not only NOTICE): default profile keeps INFO+; greppable even
	 * if NOTICE mirroring is quieted. No panic / no page-table walks.
	 */
	klog_info_fmt("FAULT",
		      "USER_FAULT_FRAME vector=%x cr2=%llx err=%llx rip=%llx "
		      "cs=%llx rsp=%llx",
		      (unsigned)vector, (unsigned long long)cr2,
		      (unsigned long long)err, (unsigned long long)rip,
		      (unsigned long long)cs, (unsigned long long)rsp);
	if (cur)
		klog_info_fmt("FAULT",
			      "USER_FAULT_FRAME rdi=%llx rsi=%llx",
			      (unsigned long long)task_get_rdi(&cur->task),
			      (unsigned long long)task_get_rsi(&cur->task));
	klog_info_fmt("FAULT",
		      "USER_FAULT_FRAME pid=%x comm=%s present=%x write=%x "
		      "exec=%x CLASSIFY %s",
		      (unsigned)pid, comm && comm[0] ? comm : "(none)",
		      (unsigned)(present ? 1 : 0), (unsigned)(write ? 1 : 0),
		      (unsigned)(exec ? 1 : 0), classify);
	if (have_opc || have_ret)
		klog_info_fmt("FAULT",
			      "USER_FAULT_FRAME opc=%x ret=%llx",
			      (unsigned)(have_opc ? opc : 0),
			      (unsigned long long)(have_ret ? retaddr : 0));
	/* Raw tag for harnesses that strip klog prefixes. */
	klog_print("USER_FAULT_FRAME\n");
}
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

/*
 * Human-readable uptime for the panic screen (seconds + milliseconds).
 * Serial dump uses the same format via klog_u32_dec; this is for FB/GTK.
 */
static void panic_uptime_human(char *buf, size_t cap)
{
	uint64_t up_ms = clock_get_uptime_milliseconds();
	uint64_t up_s = up_ms / 1000ULL;
	uint64_t up_frac = up_ms % 1000ULL;

	if (!buf || cap == 0)
		return;
	snprintf(buf, cap, "%llu.%03llu s",
		 (unsigned long long)up_s, (unsigned long long)up_frac);
}

/*
 * Final panic frame for FB/GTK: compact, readable, stays on screen.
 * Full register/process dumps stay on serial only (no scroll-away).
 */
static void panic_print_final_screen(const char *message, panic_level_t level,
				     const char *file, int line,
				     const char *caller)
{
	char uptime[32];
	char linebuf[384];

	panic_uptime_human(uptime, sizeof(uptime));

	print_screen_only("\n");
	print_screen_only("========================================\n");
	print_screen_only("KERNEL PANIC - SYSTEM HALTED\n");
	print_screen_only("========================================\n");
	print_screen_only("\n");

	print_screen_only("     +------------------------------------------------+\n");
	print_screen_only("     |                                                |\n");
	print_screen_only("     |              O_o KERNEL PANIC                  |\n");
	print_screen_only("     |                                                |\n");
	print_screen_only("     +------------------------------------------------+\n\n");

	snprintf(linebuf, sizeof(linebuf), "Reason: %s\n",
		 message ? message : "no message");
	print_screen_only(linebuf);
	snprintf(linebuf, sizeof(linebuf), "Origin: %s:%d (%s)\n",
		 file ? file : "unknown", line, caller ? caller : "unknown");
	print_screen_only(linebuf);
	snprintf(linebuf, sizeof(linebuf), "Level: %s\n", panic_level_names[level]);
	print_screen_only(linebuf);
	snprintf(linebuf, sizeof(linebuf), "Uptime: %s\n\n", uptime);
	print_screen_only(linebuf);

	print_screen_only("========================================\n");
	print_screen_only("SYSTEM HALTED - Safe to power off or reboot\n");
	print_screen_only("========================================\n");
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
        /*
         * Nested fault while already panicking (often #DF after a primary
         * #PF/#GP). Do not run a second full banner that looks like a new
         * primary DOUBLE FAULT — keep the first FAULT FRAME as the cause.
         */
        klog_fatal("OOPS", "\nCLASSIFY NESTED_PANIC_OR_FAULT");
        klog_fatal("OOPS", "PRIMARY_FAULT_FRAME_KEPT (ignore nested vector as cause)");
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

    /*
     * Do not load_page_directory(kernel) here while RSP may still sit on a
     * per-task kstack: if that slot is missing from kernel CR3, the next
     * stack access becomes #PF → #DF and the FAULT FRAME blames the dump.
     */

    /*
     * Re-enable FB/GTK output for the final panic frame. Verbose dump goes to
     * serial only so register stacks do not scroll the user-visible summary
     * off a 25-row (or scaled) console.
     */
    console_backend_panic_screen_on();
    klog_set_screen_sink(NULL);

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
    /*
     * FAULT FRAME is the CPU exception site (cause). Source/Caller below are
     * only where panicex was invoked (often isr_handler64_dispatch).
     */
    if (panic_exc_frame.valid)
    {
	klog_print("--- FAULT FRAME (cause) ---\n");
	klog_print("vector=");
	klog_hex32(panic_exc_frame.vector);
	klog_print(" err=");
	klog_hex64(panic_exc_frame.err);
	klog_print("\n");
	klog_print("fault_rip=");
	klog_hex64(panic_exc_frame.rip);
	klog_print(" fault_cs=");
	klog_hex64(panic_exc_frame.cs);
	klog_print("\n");
	klog_print("fault_rsp=");
	klog_hex64(panic_exc_frame.rsp);
	klog_print(" fault_ss=");
	klog_hex64(panic_exc_frame.ss);
	klog_print("\n");
	klog_print("fault_rflags=");
	klog_hex64(panic_exc_frame.rflags);
	klog_print("\n");
	klog_print("cr2=");
	if (panic_exc_frame.vector == 14)
	{
		klog_hex64(panic_exc_frame.cr2);
		klog_print(" (frozen at #PF note)\n");
	}
	else
		klog_print("n/a (not a #PF; do not trust live CR2)\n");
	klog_print("pid=");
	klog_hex32(panic_exc_frame.pid);
	klog_print(" comm=");
	klog_print(panic_exc_frame.comm[0] ? panic_exc_frame.comm : "(none)");
	klog_print("\n");
	if (panic_exc_frame.vector == 8)
		klog_print("CLASSIFY PRIMARY_VECTOR_DOUBLE_FAULT\n");
	else if (panic_exc_frame.vector == 14)
		klog_print("CLASSIFY PRIMARY_VECTOR_PAGE_FAULT\n");
	else if (panic_exc_frame.vector == 13)
		klog_print("CLASSIFY PRIMARY_VECTOR_GENERAL_PROTECTION\n");
	else if (panic_exc_frame.vector == 6)
		klog_print("CLASSIFY PRIMARY_VECTOR_INVALID_OPCODE\n");
	if (panic_exc_frame.stack_overflow)
		klog_print("CLASSIFY KERNEL_STACK_OVERFLOW\n");
	klog_print("--- panic site (reporter, not cause) ---\n");
    }
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

    /* Dump CPU state - registers and control registers (serial only). */
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

    /* Never unwind during panic — see dump_stack_trace() stub rationale. */
    klog_print("STACK TRACE skipped (panic-safe)\n");

    /* Dump process context - what process was running when panic occurred */
    dump_process_context();

    /* Dump memory state - heap statistics and allocation info */
    dump_memory_state();

    /* Final message before halting */
    klog_print("\n========================================\n");
    klog_print("SYSTEM HALTED - Safe to power off or reboot\n");
    klog_print("========================================\n");
    klog_print("Copy the above information for kernel debugging.\n");
    klog_print("End of panic dump.\n\n");

    clear_screen();
    panic_print_final_screen(message, level, file, line, caller);

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
