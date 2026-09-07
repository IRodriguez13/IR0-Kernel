/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_syscall_frame.c
 * Description: ARM64 syscall/IRQ frame capture (vectors.S exc_entry_frame).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_syscall_frame.h>
#include <ir0/arch_task.h>
#include <ir0/errno.h>
#include <kernel/process.h>
#include <stddef.h>
#include <string.h>

static void arm64_copy_exc_gprs(arch_syscall_frame_t *sf, const uint64_t *frame)
{
	memcpy(&sf->x0, frame, 31 * sizeof(uint64_t));
}

void syscall_capture_frame_at_entry(struct process *p,
						 uint64_t *frame_base,
						 uint64_t rip_hw)
{
	arch_syscall_frame_t *sf;
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	arm64_copy_exc_gprs(sf, frame_base);

	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));

	sf->elr = elr;
	if (!sf->elr && rip_hw)
		sf->elr = rip_hw;
	sf->spsr = spsr;
	sf->sp = sp_el0;
	p->syscall_frame_fresh = 1;
	process_sync_task_user_ip_from_syscall_frame(p);
}

void syscall_restore_exit_regs(struct process *p,
					    uint64_t *stack_r9_slot)
{
	const arch_syscall_frame_t *sf;

	if (!stack_r9_slot || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	memcpy(stack_r9_slot, &sf->x0, 31 * sizeof(uint64_t));

	__asm__ volatile("msr elr_el1, %0" :: "r"(sf->elr) : "memory");
	__asm__ volatile("msr spsr_el1, %0" :: "r"(sf->spsr) : "memory");
	__asm__ volatile("msr sp_el0, %0" :: "r"(sf->sp) : "memory");
}

int exception_frame_is_user(const void *opaque_frame)
{
	uint64_t spsr;

	(void)opaque_frame;
	/*
	 * SPSR_EL1.M[3:0] == 0 → EL0t (AArch64). Frame pointer alone does not
	 * encode privilege; exception entry left SPSR in the system register.
	 */
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	return ((spsr & 0xfu) == 0u) ? 1 : 0;
}
