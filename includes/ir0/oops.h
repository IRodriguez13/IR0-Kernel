/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: oops.h
 * Description: IR0 kernel source/header file
 */

#pragma once

/* Semantic panic levels */
typedef enum
{
    PANIC_KERNEL_BUG = 0,      /* Bug in kernel code */
    PANIC_HARDWARE_FAULT = 1,  /* Hardware malfunction */
    PANIC_OUT_OF_MEMORY = 2,   /* System out of memory */
    PANIC_STACK_OVERFLOW = 3,  /* Stack corruption */
    PANIC_ASSERT_FAILED = 4,   /* Assertion failure */
    PANIC_MEM = 5,             /* Memory operation error (null ptr, invalid access, etc.) */
    TESTING = 6,
    RUNNING_OUT_PROCESS = 7
} panic_level_t;

#ifdef __cplusplus
extern "C" {
#endif

void panic(const char *message);
void panicex(const char *message, panic_level_t level, const char *file, int line, const char *caller); /*Panic with more detailed logs*/
int ir0_panic_in_progress(void);
/*
 * Record the CPU exception frame before panicex so the banner can show the
 * fault site (RIP/RSP) instead of only the panic reporter (__func__).
 * Also seeds dump_stack_trace() to unwind from fault_rsp when set.
 */
void panic_note_exception_frame(unsigned vector, unsigned long long err,
				unsigned long long rip, unsigned long long cs,
				unsigned long long rflags,
				unsigned long long rsp, unsigned long long ss,
				int stack_overflow, unsigned int pid,
				const char *comm);
unsigned long long ir0_panic_fault_rsp(void);
/*
 * Greppable userspace fault line for session smokes (NOTICE). Safe during
 * IRQ; does not panic. Emit before SIGSEGV delivery so handler/kill paths
 * still leave a huntable banner (not only CONSOLE_SESSION_SEGV).
 */
void ir0_log_user_fault_frame(unsigned vector, unsigned long long cr2,
			      unsigned long long err, unsigned long long rip,
			      unsigned long long cs, unsigned long long rsp,
			      int present, int write, int exec,
			      unsigned int pid, const char *comm);
void dump_stack_trace(void);
void dump_registers(void);

#ifdef __cplusplus
}
#endif


#define PANIC(msg) \
    panicex((msg), PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__)

// Macros for better panic message tunning
#define BUG_ON(condition) \
    do { \
        if (unlikely(condition)) \
        { \
            panicex("BUG_ON: " #condition, PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/* For testing */
#define ASSERT(condition) \
    do { \
        if (unlikely(!(condition))) \
        { \
            panicex("ASSERT failed: " #condition, PANIC_ASSERT_FAILED, __FILE__, __LINE__, __func__); \
        } \
    } while(0)
      
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

