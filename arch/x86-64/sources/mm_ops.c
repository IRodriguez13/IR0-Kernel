/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_ops.c
 * Description: x86-64 MM activate / TLB / IRQ save behind arch_portable (F8-facade-mm).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>

#include <stddef.h>
#include <stdint.h>

/* Match mm/paging.h leaf/table descriptor bits (x86-64 4K). */
#define X86_PTE_PRESENT   0x1ULL
#define X86_PTE_RW        0x2ULL
#define X86_PTE_USER      0x4ULL
#define X86_PTE_LARGE     0x80ULL
#define X86_PTE_NX        (1ULL << 63)
#define X86_PTE_PFN_MASK  0x000FFFFFFFFFF000ULL
#define X86_INDEX_MASK    0x1FFUL

void mm_activate(uintptr_t root)
{
	__asm__ volatile("mov %0, %%cr3" :: "r"(root) : "memory");
}

uintptr_t mm_current_root(void)
{
	uintptr_t root;

	__asm__ volatile("mov %%cr3, %0" : "=r"(root));
	return root;
}

void tlb_invalidate_page(uintptr_t va)
{
	unsigned long flags;
	uintptr_t rsp;

	/*
	 * Never invlpg the page backing the live RSP. Post-LOGIN #DF had
	 * fault_rip on the reload of saved flags after invlpg with
	 * cr2 inside the current kstack page: TLB drop + missing/stale PTE
	 * under process CR3 cannot be recovered without an IRQ/DF IST walk.
	 */
	__asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
	if ((va & ~0xFFFUL) == (rsp & ~0xFFFUL))
		return;

	/*
	 * Keep IRQs off for the invlpg + return: IR0 has no IRQ stack, so a
	 * timer nest on a deep #PF path can #DF (seen post-ash as fault_rip in
	 * this function's leave epilogue with IF=1).
	 */
	flags = irq_save();
	__asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
	irq_restore(flags);
}

void tlb_invalidate_all(void)
{
	uintptr_t root = mm_current_root();

	/* Reload CR3 to flush local non-global TLB entries. */
	mm_activate(root);
}

unsigned long irq_save(void)
{
	unsigned long flags;

	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
	return flags;
}

void irq_restore(unsigned long flags)
{
	__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

uint64_t mm_read_ctrl0(void)
{
	uint64_t v;

	__asm__ volatile("mov %%cr0, %0" : "=r"(v));
	return v;
}

void mm_write_ctrl0(uint64_t value)
{
	__asm__ volatile("mov %0, %%cr0" :: "r"(value) : "memory");
}

uint64_t mm_read_ctrl1(void)
{
	uint64_t v;

	__asm__ volatile("mov %%cr4, %0" : "=r"(v));
	return v;
}

void mm_va_indices(uintptr_t va, size_t idx[4])
{
	idx[0] = ((uint64_t)va >> 39) & X86_INDEX_MASK;
	idx[1] = ((uint64_t)va >> 30) & X86_INDEX_MASK;
	idx[2] = ((uint64_t)va >> 21) & X86_INDEX_MASK;
	idx[3] = ((uint64_t)va >> 12) & X86_INDEX_MASK;
}

int mm_pte_present(uint64_t e)
{
	return (e & X86_PTE_PRESENT) != 0;
}

int mm_pte_large(uint64_t e)
{
	return mm_pte_present(e) && ((e & X86_PTE_LARGE) != 0);
}

uintptr_t mm_pte_phys(uint64_t e)
{
	return (uintptr_t)(e & X86_PTE_PFN_MASK);
}

uint64_t mm_make_table_pte(uintptr_t phys, int user)
{
	uint64_t e = ((uint64_t)phys & X86_PTE_PFN_MASK) | X86_PTE_PRESENT | X86_PTE_RW;

	if (user)
		e |= X86_PTE_USER;
	return e;
}

uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec)
{
	uint64_t e = ((uint64_t)phys & X86_PTE_PFN_MASK) | (flags12 & 0xFFFULL) |
		     X86_PTE_PRESENT;

	if (!exec)
		e |= X86_PTE_NX;
	return e;
}

void mm_pte_set_user(uint64_t *e)
{
	if (e)
		*e |= X86_PTE_USER;
}
