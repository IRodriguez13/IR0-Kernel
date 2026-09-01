/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: user_mode.c
 * Description: x86-64 transition to ring 3 (implements switch_to_user declared in arch_portable.h)
 */

#include <stdint.h>
#include <config.h>
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/ktm/klog.h>
#include <sched/task.h>
#include <arch/common/arch_portable.h>
#include <ir0/process.h>

#define USER_CANON_MIN 0x00400000ULL
#define USER_CANON_MAX 0x00007FFFFFFFFFFFULL

static int arch_user_va_canonical(uint64_t va)
{
    return va >= USER_CANON_MIN && va <= USER_CANON_MAX;
}

static void arch_audit_iret_frame(const task_t *task)
{
    klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][IRET_CHECK] rip=%llx rsp=%llx cs=%llx ss=%llx rflags=%llx", (unsigned long long)(task->arch.rip), (unsigned long long)(task->arch.rsp), (unsigned long long)((uint64_t)task->arch.cs), (unsigned long long)((uint64_t)task->arch.ss), (unsigned long long)(task->arch.rflags));

    if ((task->arch.cs & 0xFFFFU) != (uint16_t)USER_CODE_SEL ||
        (task->arch.ss & 0xFFFFU) != (uint16_t)USER_DATA_SEL)
    {
        klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_CS_SS");
        panic("switch_to_user_task: invalid CS/SS for ring3 iretq");
    }
    if (!arch_user_va_canonical(task->arch.rip))
    {
        klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RIP");
        panic("switch_to_user_task: invalid RIP for ring3 iretq");
    }
    if (!arch_user_va_canonical(task->arch.rsp))
    {
        klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RSP");
        panic("switch_to_user_task: invalid RSP for ring3 iretq");
    }
    klog_debug("WAIT", "CLASSIFY RETURN_TO_PARENT_OK");
}

/*
 * Detect MinGW-w64 cross-compilation
 */
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

#if !MINGW_BUILD
extern void switch_to_user_task_asm(const task_t *task);
#endif

void switch_to_user(arch_addr_t entry, arch_addr_t stack_top)
{
#if MINGW_BUILD
    (void)entry;
    (void)stack_top;
    panic("switch_to_user not supported in Windows build");
#else
    uintptr_t uentry = (uintptr_t)entry;
    uintptr_t ursp = (uintptr_t)stack_top;
    uint64_t fsbase = current_process ? process_tls_get(current_process) : 0;

    /*
     * iretq to user code with user DS/ES; RFLAGS_IF set so device IRQs work.
     *
     * Do NOT load FS/GS with USER_DATA_SEL: on x86-64 that reloads the
     * hidden segment base from the GDT (typically 0) and clobbers the
     * IA32_FS_BASE MSR used for TLS. Install FS base via WRMSR instead
     * (same contract as switch_to_user_task_asm / arch_prctl).
     */
    __asm__ volatile(
        "cli\n"
        "mov %[udsel], %%eax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        /* Null FS/GS selectors; MSR base installed below after this. */
        "xor %%eax, %%eax\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        :
        : [udsel] "i"(USER_DATA_SEL)
        : "rax", "memory");
    /* WRMSR after selector load — loading FS/GS zeros the hidden base. */
    set_fs_base(fsbase);
    __asm__ volatile(
        "pushq %[udsel64]\n"
        "pushq %0\n"
        "pushfq\n"
        "pop %%rax\n"
        "or %[rflags_if], %%rax\n"
        "push %%rax\n"
        "pushq %[ucsel64]\n"
        "pushq %1\n"
        "iretq\n"
        :
        : "r"(ursp), "r"(uentry),
          [udsel64] "i"((uint64_t)USER_DATA_SEL),
          [rflags_if] "i"(RFLAGS_IF), [ucsel64] "i"((uint64_t)USER_CODE_SEL)
        : "rax", "memory");

    panic("Returned from user mode unexpectedly");
#endif
}

void switch_to_user_task(const task_t *task)
{
#if MINGW_BUILD
    (void)task;
    panic("switch_to_user_task not supported in Windows build");
#else
    if (!task)
        panic("switch_to_user_task: null task");

    arch_audit_iret_frame(task);
    switch_to_user_task_asm(task);
    panic("Returned from switch_to_user_task unexpectedly");
#endif
}

void first_switch_to(struct process *next)
{
	process_t *p = (process_t *)next;

	if (!p)
		panic("first_switch_to: null process");

#if MINGW_BUILD
	(void)p;
	panic("first_switch_to not supported in Windows build");
#else
	if (p->mode == KERNEL_MODE)
	{
		uint64_t kds = KERNEL_DATA_SEL;
		uint64_t kcs = KERNEL_CODE_SEL;

		mm_activate((uintptr_t)process_mm_root(p));
		__asm__ volatile(
			"cli\n"
			"mov %w[ds], %%ds\n"
			"mov %w[ds], %%es\n"
			"mov %w[ds], %%fs\n"
			"mov %w[ds], %%gs\n"
			"pushq %[ds]\n"
			"pushq %[rsp_val]\n"
			"pushq %[rflags]\n"
			"pushq %[cs_val]\n"
			"pushq %[rip_val]\n"
			"iretq\n"
			:
			: [rsp_val] "r"(p->task.arch.rsp),
			  [rflags] "r"((uint64_t)RFLAGS_IF),
			  [rip_val] "r"(p->task.arch.rip),
			  [ds] "r"(kds),
			  [cs_val] "r"(kcs)
			: "memory"
		);
	}
	else
	{
		mm_activate((uintptr_t)process_mm_root(p));
		switch_to_user((arch_addr_t)p->task.arch.rip,
				    (arch_addr_t)p->task.arch.rsp);
	}
	panic("Returned from first_switch_to unexpectedly");
#endif
}

#define MSR_IA32_FS_BASE 0xC0000100U

void set_fs_base(uint64_t base)
{
#if MINGW_BUILD
    (void)base;
#else
    uint32_t lo = (uint32_t)(base & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(base >> 32);

    __asm__ volatile("wrmsr" : : "c"(MSR_IA32_FS_BASE), "a"(lo), "d"(hi) : "memory");
#endif
}

uint64_t get_fs_base(void)
{
#if MINGW_BUILD
    return 0;
#else
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_IA32_FS_BASE));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
#endif
}

/*
 * restore_user_fs_base - Re-install TLS FS base before returning to ring 3.
 *
 * Sysret/ISR paths historically only reloaded DS/ES. Anything that clobbers
 * IA32_FS_BASE (or a context switch that restored fs_base=0) left glibc with
 * FS=0 → __ctype_b_loc returns -0x48 → SEGV. Always reload from process_t.
 */
void restore_user_fs_base(void)
{
	if (current_process)
		set_fs_base(process_tls_get(current_process));
}

[[maybe_unused]]void syscall_handler_c(void)
{
}

#if !MINGW_BUILD
void fase24_log_stack_once(void)
{
    /* Temporary trace hook intentionally left as no-op. */
}
#endif
