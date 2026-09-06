/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: debug_dump.c
 * Description: ISA-local panic register/stack dump (x86); panic-safe unwind.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops.h>
#include <ir0/vga.h>
#include <ir0/ktm/klog.h>
#include <stdint.h>

void dump_registers(void)
{
#ifdef __x86_64__
	uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
	uint64_t rflags, rip;
	uint64_t cr0, cr2, cr3, cr4;

	__asm__ volatile(
		"movq %%rax, %0\n"
		"movq %%rbx, %1\n"
		"movq %%rcx, %2\n"
		"movq %%rdx, %3\n"
		"movq %%rsi, %4\n"
		"movq %%rdi, %5\n"
		"movq %%rsp, %6\n"
		"movq %%rbp, %7\n"
		"movq %%r8, %8\n"
		"movq %%r9, %9\n"
		"movq %%r10, %10\n"
		"movq %%r11, %11\n"
		"movq %%r12, %12\n"
		"movq %%r13, %13\n"
		"movq %%r14, %14\n"
		"movq %%r15, %15\n"
		"pushfq\n"
		"popq %16\n"
		"leaq (%%rip), %17\n"
		: "=m"(rax), "=m"(rbx), "=m"(rcx), "=m"(rdx),
		  "=m"(rsi), "=m"(rdi), "=m"(rsp), "=m"(rbp),
		  "=m"(r8), "=m"(r9), "=m"(r10), "=m"(r11),
		  "=m"(r12), "=m"(r13), "=m"(r14), "=m"(r15),
		  "=m"(rflags), "=r"(rip)
		:
		: "memory");

	__asm__ volatile("movq %%cr0, %0" : "=r"(cr0));
	__asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
	__asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
	__asm__ volatile("movq %%cr4, %0" : "=r"(cr4));

	klog_debug_fmt("KERN",
		       "--- CPU REGISTERS (x86-64) --- "
		       "RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx "
		       "RSI=0x%llx RDI=0x%llx RSP=0x%llx RBP=0x%llx "
		       "R8=0x%llx R9=0x%llx R10=0x%llx R11=0x%llx "
		       "R12=0x%llx R13=0x%llx R14=0x%llx R15=0x%llx "
		       "RIP=0x%llx RFLAGS=0x%llx CR0=0x%llx CR2=0x%llx CR3=0x%llx CR4=0x%llx",
		       (unsigned long long)rax, (unsigned long long)rbx,
		       (unsigned long long)rcx, (unsigned long long)rdx,
		       (unsigned long long)rsi, (unsigned long long)rdi,
		       (unsigned long long)rsp, (unsigned long long)rbp,
		       (unsigned long long)r8, (unsigned long long)r9,
		       (unsigned long long)r10, (unsigned long long)r11,
		       (unsigned long long)r12, (unsigned long long)r13,
		       (unsigned long long)r14, (unsigned long long)r15,
		       (unsigned long long)rip, (unsigned long long)rflags,
		       (unsigned long long)cr0, (unsigned long long)cr2,
		       (unsigned long long)cr3, (unsigned long long)cr4);

	print_colored("--- REGISTER DUMP (64-bit) ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	print("RAX: ");
	print_hex64(rax);
	print("  ");
	print("RBX: ");
	print_hex64(rbx);
	print("\n");
	print("RIP: ");
	print_hex64(rip);
	print("  ");
	print("RSP: ");
	print_hex64(rsp);
	print("\n");

#else
	uint32_t eax, ebx, ecx, edx, esi, edi, esp, ebp;
	uint32_t eflags;

	__asm__ volatile(
		"movl %%eax, %0\n"
		"movl %%ebx, %1\n"
		"movl %%ecx, %2\n"
		"movl %%edx, %3\n"
		"movl %%esi, %4\n"
		"movl %%edi, %5\n"
		"movl %%esp, %6\n"
		"movl %%ebp, %7\n"
		"pushfl\n"
		"popl %8\n"
		: "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx),
		  "=m"(esi), "=m"(edi), "=m"(esp), "=m"(ebp), "=m"(eflags)
		:
		: "memory");

	print_colored("--- REGISTER DUMP (32-bit) ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	print("EAX: ");
	print_hex_compact(eax);
	print("  EBX: ");
	print_hex_compact(ebx);
	print("\n");
#endif
}

/**
 * dump_stack_trace - Panic-safe stub
 *
 * Full RBP unwind on a per-task kstack under process CR3 nested-faulted into
 * #DF and replaced the primary panic site with this function's RIP. Keep the
 * stub until dumps run only on IST / with kstacks mapped in the panic CR3.
 * Cause attribution is FAULT FRAME + serial banners (see panic_note_*).
 */
void dump_stack_trace(void)
{
	klog_print("\n--- STACK TRACE ---\n");
	klog_print("STACK_TRACE_DISABLED (panic-safe)\n");
	print_colored("--- STACK TRACE ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	print_warning("STACK_TRACE_DISABLED\n");
}
