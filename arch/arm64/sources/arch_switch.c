/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_switch.c
 * Description: ARM64 switch_to — TTBR0, SP_EL0/SP_EL1, TPIDR_EL0 TLS, EL1 switch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_switch.h>
#include <ir0/arch_task.h>
#include <ir0/arch_cpu.h>
#include <ir0/process.h>
#include <stdint.h>

extern void switch_context_arm64(task_t *prev, task_t *next);

void arch_set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	/*
	 * Exception entry from EL0 uses SP_EL1. Cooperative EL1 switch keeps
	 * the running SP; this primes the banked stack for the next EL0 drop.
	 */
	__asm__ volatile("msr sp_el1, %0" :: "r"(proc->kstack_top) : "memory");
}

void arch_switch_save_user_rsp(struct process *prev)
{
	process_t *proc = (process_t *)prev;
	uint64_t sp_el0;

	if (!proc)
		return;

	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	proc->saved_user_rsp = sp_el0;
}

void arch_switch_to(task_t *prev, task_t *next)
{
	process_t *prev_proc;
	process_t *next_proc;

	if (!next)
		return;

	prev_proc = prev ? task_to_process(prev) : NULL;
	next_proc = task_to_process(next);

	if (prev_proc)
		arch_switch_save_user_rsp(prev_proc);

	if (next_proc)
	{
		if (task_mm_root(next) == 0 && process_pgd(next_proc))
			task_set_mm_root(next,
					 (uint64_t)(uintptr_t)process_pgd(next_proc));
		arch_set_current_kernel_stack(next_proc);
		/*
		 * Always program SP_EL0 — skipping when saved_user_rsp==0 left
		 * a stale value from the previous task (Bugbot).
		 */
		{
			uint64_t sp_el0 = next_proc->saved_user_rsp;

			if (!sp_el0)
				sp_el0 = task_get_sp(next);
			__asm__ volatile("msr sp_el0, %0" :: "r"(sp_el0)
					 : "memory");
		}
		set_tls(process_tls_get(next_proc));
	}

	switch_context_arm64(prev, next);
}

void set_fs_base(uint64_t base)
{
	__asm__ volatile("msr tpidr_el0, %0" :: "r"(base) : "memory");
}

uint64_t get_fs_base(void)
{
	uint64_t base;

	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(base));
	return base;
}

void arch_restore_user_fs_base(void)
{
	if (current_process)
		set_fs_base(process_tls_get(current_process));
}
