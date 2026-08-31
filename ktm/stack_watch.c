/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stack_watch.c
 * Description: Kernel stack headroom high-water mark
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/stack_watch.h>
#include <ir0/ktm/deferred.h>
#include <config.h>

/*
 * A kernel stack overflow is only visible after the fact: RSP walks into the
 * guard page and the resulting fault escalates to #DF, by which point the
 * chain that consumed the stack is gone. Sampling headroom from the
 * interrupt entry catches the same chains while they are still running,
 * because an interrupt lands at whatever depth the interrupted code had
 * reached.
 *
 * Two comparisons and, rarely, a deferred row. Emitting here is not an
 * option: this is the interrupt path.
 */

#define KSTACK_WARN_HEADROOM (8u * 1024u)
/* Guard page at the base of every slot (process_kernel_stack_alloc). */
#define KSTACK_GUARD_BYTES   (4096ULL)

static uint64_t ktm_stack_min_headroom = ~0ULL;

void ktm_stack_watch(uint64_t rsp)
{
	uint64_t off;
	uint64_t slot;
	uint64_t bottom;
	uint64_t headroom;

	if (rsp < IR0_KSTACK_VA_BASE)
		return;

	off = rsp - (uint64_t)IR0_KSTACK_VA_BASE;
	slot = off / (uint64_t)IR0_KSTACK_SLOT_SIZE;
	if (slot >= (uint64_t)IR0_KSTACK_MAX_SLOTS)
		return;

	/* First page of every slot is the unmapped guard. */
	bottom = (uint64_t)IR0_KSTACK_VA_BASE +
		 slot * (uint64_t)IR0_KSTACK_SLOT_SIZE + KSTACK_GUARD_BYTES;
	if (rsp < bottom)
		return;

	headroom = rsp - bottom;
	if (headroom >= ktm_stack_min_headroom)
		return;

	ktm_stack_min_headroom = headroom;

	/* Only the record-breaking samples, and only once they matter. */
	if (headroom < KSTACK_WARN_HEADROOM)
		ktm_deferred_record(KTM_DEFERRED_STACK_LOW, 0, headroom, slot,
				    rsp);
}

uint64_t ktm_stack_min_headroom_get(void)
{
	return ktm_stack_min_headroom == ~0ULL ? 0 : ktm_stack_min_headroom;
}

/*
 * Interrupts nest on the kernel stack of the task they preempt, so a burst
 * arriving while a syscall is already deep multiplies the cost of that
 * chain. Without a separate IRQ stack the only bound is the guard page, and
 * the panic report gave no way to tell nesting apart from a deep call chain.
 */
static unsigned ktm_irq_nest_level;
static unsigned ktm_irq_nest_max;

void ktm_irq_nest_enter(void)
{
	ktm_irq_nest_level++;
	if (ktm_irq_nest_level > ktm_irq_nest_max)
		ktm_irq_nest_max = ktm_irq_nest_level;
}

void ktm_irq_nest_exit(void)
{
	if (ktm_irq_nest_level != 0)
		ktm_irq_nest_level--;
}

unsigned ktm_irq_nest_max_get(void)
{
	return ktm_irq_nest_max;
}

/*
 * True high-water mark for the calling task's kernel stack.
 *
 * The stack is poisoned when the slot is allocated, so everything the task
 * has ever pushed has overwritten the pattern. Scanning up from just above
 * the guard page to the first surviving poison byte gives peak usage
 * directly, rather than the deepest point an interrupt happened to sample.
 */
uint64_t ktm_stack_peak_used(void)
{
	uint64_t here = (uint64_t)(uintptr_t)&here;
	uint64_t off;
	uint64_t slot;
	uint64_t bottom;
	uint64_t top;
	const uint8_t *p;
	uint64_t i;

	if (here < IR0_KSTACK_VA_BASE)
		return 0;

	off = here - (uint64_t)IR0_KSTACK_VA_BASE;
	slot = off / (uint64_t)IR0_KSTACK_SLOT_SIZE;
	if (slot >= (uint64_t)IR0_KSTACK_MAX_SLOTS)
		return 0;

	bottom = (uint64_t)IR0_KSTACK_VA_BASE +
		 slot * (uint64_t)IR0_KSTACK_SLOT_SIZE + KSTACK_GUARD_BYTES;
	top = bottom + (uint64_t)IR0_PROC_KSTACK_SIZE;
	if (here <= bottom || here > top)
		return 0;

	p = (const uint8_t *)(uintptr_t)bottom;
	for (i = 0; i < (uint64_t)IR0_PROC_KSTACK_SIZE; i++)
	{
		if (p[i] != (uint8_t)IR0_KSTACK_POISON)
			break;
	}

	/* Everything above the first touched byte has been used at least once. */
	return (uint64_t)IR0_PROC_KSTACK_SIZE - i;
}
