/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging.c
 * Description: Page tables, 2 MiB identity split, and leaf install.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <config.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <ir0/mm.h>
#include <ir0/debug_runtime.h>
#include <ir0/video_backend.h>
#include "paging.h"
#include <string.h>
#include <mm/allocator.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <ir0/validation.h>
#include <ir0/ahci_api.h>
#include <ir0/arch_port.h>

/*
 * Boot/kernel CR3 with full PMM identity. Pinned once before any process mm
 * (see paging_pin_kernel_cr3). Used like Linux kmap for phys page copy/zero
 * after process tables punch identity holes to pte_none.
 */
static uint64_t paging_kernel_cr3;

void paging_pin_kernel_cr3(uint64_t cr3)
{
	if (paging_kernel_cr3 || !cr3)
		return;
	paging_kernel_cr3 = cr3;
}
#include <ir0/arch_cpu.h>

static uint32_t fase40_copy_diag_events;

typedef enum
{
    IR0_MM_FRAME_UNKNOWN = 0,
    IR0_MM_FRAME_USER = 1,
    IR0_MM_FRAME_PT = 2,
    IR0_MM_FRAME_KERNEL = 3
} ir0_mm_frame_type_t;

static uint64_t ir0_mm_pml4_created;
static uint64_t ir0_mm_pml4_freed;
static uint64_t ir0_mm_pdpt_created;
static uint64_t ir0_mm_pdpt_freed;
static uint64_t ir0_mm_pd_created;
static uint64_t ir0_mm_pd_freed;
static uint64_t ir0_mm_pt_created;
static uint64_t ir0_mm_pt_freed;
static uint64_t ir0_mm_leaf_created;
static uint64_t ir0_mm_leaf_freed;

static uint64_t ir0_mm_frame_user_alloc;
static uint64_t ir0_mm_frame_user_free;
static uint64_t ir0_mm_frame_pt_alloc;
static uint64_t ir0_mm_frame_pt_free;
static uint64_t ir0_mm_frame_kernel_alloc;
static uint64_t ir0_mm_frame_kernel_free;

static uint8_t *ir0_mm_frame_type_map;
static size_t ir0_mm_frame_type_map_frames;
#if IR0_DEBUG_PROC
static uint32_t ir0_mm_frame_log_events;
#endif

static uint64_t fase43_oom_boot_fatal;
static uint64_t fase43_oom_kernel_fatal;
static uint64_t fase43_oom_user_recoverable;

static fase43_oom_class_t paging_classify_oom(void)
{
    if (current_process && current_process->mode == USER_MODE)
        return FASE43_OOM_USER_RECOVERABLE;
    if (!current_process)
        return FASE43_OOM_BOOT_FATAL;
    return FASE43_OOM_KERNEL_FATAL;
}

void paging_fase43_note_oom(const char *site, fase43_oom_class_t cls)
{
    (void)site;
    switch (cls)
    {
    case FASE43_OOM_BOOT_FATAL:
        fase43_oom_boot_fatal++;
        break;
    case FASE43_OOM_KERNEL_FATAL:
        fase43_oom_kernel_fatal++;
        break;
    case FASE43_OOM_USER_RECOVERABLE:
        fase43_oom_user_recoverable++;
        break;
    default:
        break;
    }

}

void paging_fase43_oom_audit(const char *tag)
{
    (void)tag;
}

fase43_oom_class_t paging_fase43_classify_current(void)
{
    return paging_classify_oom();
}

static int ir0_mm_frame_type_ensure(void)
{
    size_t total_frames = 0;

    if (ir0_mm_frame_type_map)
        return 0;

    pmm_stats(&total_frames, NULL, NULL);
    if (total_frames == 0)
        return -1;

    ir0_mm_frame_type_map = kmalloc(total_frames);
    if (!ir0_mm_frame_type_map)
        return -1;

    memset(ir0_mm_frame_type_map, 0, total_frames);
    ir0_mm_frame_type_map_frames = total_frames;
    return 0;
}

static long ir0_mm_frame_index(uint64_t phys_addr)
{
    uintptr_t start = pmm_get_start();
    uintptr_t end = pmm_get_end();

    if (phys_addr < start || phys_addr >= end)
        return -1;
    return (ssize_t)((phys_addr - start) / PAGE_SIZE_4KB);
}

static ir0_mm_frame_type_t ir0_mm_get_frame_type(uint64_t phys_addr)
{
    long idx;

    if (ir0_mm_frame_type_ensure() != 0)
        return IR0_MM_FRAME_UNKNOWN;

    idx = ir0_mm_frame_index(phys_addr);
    if (idx < 0 || (size_t)idx >= ir0_mm_frame_type_map_frames)
        return IR0_MM_FRAME_UNKNOWN;
    return (ir0_mm_frame_type_t)ir0_mm_frame_type_map[idx];
}

static void ir0_mm_set_frame_type(uint64_t phys_addr, ir0_mm_frame_type_t t)
{
    long idx;

    if (ir0_mm_frame_type_ensure() != 0)
        return;
    idx = ir0_mm_frame_index(phys_addr);
    if (idx < 0 || (size_t)idx >= ir0_mm_frame_type_map_frames)
        return;
    ir0_mm_frame_type_map[idx] = (uint8_t)t;
}

__attribute__((unused))
static const char *ir0_mm_frame_type_name(ir0_mm_frame_type_t t)
{
    switch (t)
    {
    case IR0_MM_FRAME_USER:
        return "FRAME_USER";
    case IR0_MM_FRAME_PT:
        return "FRAME_PT";
    case IR0_MM_FRAME_KERNEL:
        return "FRAME_KERNEL";
    default:
        return "FRAME_UNKNOWN";
    }
}

static void ir0_mm_log_frame_type(const char *kind, uint64_t frame, ir0_mm_frame_type_t t)
{
#if !IR0_DEBUG_PROC
    (void)kind;
    (void)frame;
    (void)t;
    return;
#else
    if (ir0_mm_frame_log_events >= 2048U)
        return;

    ir0_mm_frame_log_events++;
#endif
}

static int page_table_is_empty(const uint64_t *table)
{
    for (size_t i = 0; i < 512; i++)
    {
        if (mm_pte_present(table[i]))
            return 0;
    }
    return 1;
}

static void ir0_mm_free_table_frame(uint64_t frame, int level)
{
    if (!frame)
        return;

    if (level == 1)
        ir0_mm_pdpt_freed++;
    else if (level == 2)
        ir0_mm_pd_freed++;
    else if (level == 3)
        ir0_mm_pt_freed++;

    ir0_mm_set_frame_type(frame, IR0_MM_FRAME_UNKNOWN);
    ir0_mm_frame_pt_free++;
    ir0_mm_log_frame_type("FREE", frame, IR0_MM_FRAME_PT);
    kfree_aligned((void *)(uintptr_t)frame);
}

/*
 * Linux mm/x86: pte_pfn(), pud_page() — extract the physical pointer from a
 * descriptor without software/NX bits (via mm_pte_phys).
 */
static inline uintptr_t paging_entry_pfn(uint64_t entry)
{
	return mm_pte_phys(entry);
}

static inline int paging_entry_large(uint64_t entry)
{
	return mm_pte_large(entry);
}

static inline uint64_t *paging_entry_table(uint64_t entry)
{
	if (!mm_pte_present(entry))
		return NULL;
	if (paging_entry_large(entry))
		return NULL;
	return (uint64_t *)paging_entry_pfn(entry);
}

void enable_paging(void)
{
	uint64_t cr0;

	cr0 = mm_read_ctrl0();
	/* PG and WP: supervisor respects read-only PTEs */
	cr0 |= CR0_PG | CR0_WP;
	mm_write_ctrl0(cr0);
	paging_pin_kernel_cr3(get_current_page_directory());
}

void setup_and_enable_paging(void)
{
	/* SILENT safety checks */
	/* DO NOT use print/log during critical setup */

	/* 1. Verify we're in 64-bit mode */
	uint64_t cr0, cr4;

	cr0 = mm_read_ctrl0();
	cr4 = mm_read_ctrl1();
	(void)cr0;

	if (!(cr4 & CR4_PAE))
	{
		/* PAE not enabled - critical failure, will triple fault */
		return;
	}

	/* Verify paging is enabled */
	if (!is_paging_enabled())
	{
		/* Paging not enabled - enable it */
		enable_paging();
	}
	else
		paging_pin_kernel_cr3(get_current_page_directory());
}

void paging_sync_kernel_half(uint64_t *proc_pml4)
{
	uint64_t kcr3 = paging_kernel_cr3;

	if (!proc_pml4 || !kcr3)
		return;
	if ((uint64_t)(uintptr_t)proc_pml4 == kcr3)
		return;
	mm_copy_kernel_half(proc_pml4, (uint64_t *)(uintptr_t)kcr3);
}

void load_page_directory(uint64_t pml4_addr)
{
	/*
	 * Switch while RSP may sit on a per-task kstack: ensure this mm still
	 * links the shared kernel-half PDPT (new kstack slots after fork).
	 */
	if (pml4_addr)
		paging_sync_kernel_half((uint64_t *)(uintptr_t)pml4_addr);
	mm_activate((uintptr_t)pml4_addr);
}

uint64_t get_current_page_directory(void)
{
	return (uint64_t)mm_current_root();
}

uint64_t paging_get_kernel_cr3(void)
{
	/* Never lazy-capture from a process CR3 (that caused #DF). */
	return paging_kernel_cr3;
}

static void paging_phys_map_begin(uint64_t *saved_cr3)
{
	*saved_cr3 = get_current_page_directory();
	if (paging_kernel_cr3 && paging_kernel_cr3 != *saved_cr3)
		load_page_directory(paging_kernel_cr3);
}

static void paging_phys_map_end(uint64_t saved_cr3)
{
	if (paging_kernel_cr3 && paging_kernel_cr3 != saved_cr3)
	{
		/* load_page_directory syncs kernel half before mov CR3. */
		load_page_directory(saved_cr3);
	}
}

/*
 * Phys↔phys page copy (Linux copy_user_highpage / kmap spirit): run under the
 * pinned boot CR3 so process tables may leave heap holes as pte_none.
 */
void paging_copy_phys_page(uintptr_t dst_phys, uintptr_t src_phys)
{
	uint64_t saved;

	paging_phys_map_begin(&saved);
	memcpy((void *)dst_phys, (void *)src_phys, 0x1000);
	paging_phys_map_end(saved);
}

void paging_zero_phys_page(uintptr_t phys)
{
	uint64_t saved;

	paging_phys_map_begin(&saved);
	memset((void *)phys, 0, 0x1000);
	paging_phys_map_end(saved);
}

/*
 * Poison a page so unused kernel stack stays recognisable.
 *
 * Sampling headroom from the interrupt path only sees the depths an
 * interrupt happens to land on, and it missed a getdents chain that spent
 * 20 KiB. Filling the stack at allocation lets the untouched remainder be
 * measured directly, the way Linux DEBUG_STACK_USAGE does.
 */
void paging_poison_phys_page(uintptr_t phys, uint8_t pattern)
{
	uint64_t saved;

	paging_phys_map_begin(&saved);
	memset((void *)phys, (int)pattern, 0x1000);
	paging_phys_map_end(saved);
}

int is_paging_enabled(void)
{
	return (mm_read_ctrl0() & CR0_PG) != 0;
}

/**
 * Check if a virtual address is mapped in a page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to check
 * @flags_out: Optional output for page flags (can be NULL)
 * Returns: 1 if mapped, 0 if not mapped, -1 on error
 */
int is_page_mapped_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t *flags_out)
{
	size_t idx[4];
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	if (!pml4)
		return -1;

	mm_va_indices((uintptr_t)virt_addr, idx);

	if (!mm_pte_present(pml4[idx[0]]))
		return 0;
	if (paging_entry_large(pml4[idx[0]]))
	{
		if (flags_out)
			*flags_out = pml4[idx[0]] & 0xFFFu;
		return 1;
	}

	pdpt = paging_entry_table(pml4[idx[0]]);
	if (!pdpt || !mm_pte_present(pdpt[idx[1]]))
		return 0;
	if (paging_entry_large(pdpt[idx[1]]))
	{
		if (flags_out)
			*flags_out = pdpt[idx[1]] & 0xFFFu;
		return 1;
	}

	pd = paging_entry_table(pdpt[idx[1]]);
	if (!pd || !mm_pte_present(pd[idx[2]]))
		return 0;
	if (paging_entry_large(pd[idx[2]]))
	{
		/* Linux pmd_present && pmd_large: a 2 MiB leaf is mapped. */
		if (flags_out)
			*flags_out = pd[idx[2]] & 0xFFFu;
		return 1;
	}

	pt = paging_entry_table(pd[idx[2]]);
	if (!pt || !mm_pte_present(pt[idx[3]]))
		return 0;

	if (flags_out)
		*flags_out = pt[idx[3]] & 0xFFF;

	return 1;
}

uint64_t *paging_get_pte(uint64_t *pml4, uintptr_t vaddr)
{
	size_t idx[4];
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	if (!pml4)
		return NULL;

	mm_va_indices(vaddr, idx);

	if (!mm_pte_present(pml4[idx[0]]))
		return NULL;
	if (paging_entry_large(pml4[idx[0]]))
		return NULL;

	pdpt = paging_entry_table(pml4[idx[0]]);
	if (!pdpt || !mm_pte_present(pdpt[idx[1]]))
		return NULL;
	if (paging_entry_large(pdpt[idx[1]]))
		return NULL;

	pd = paging_entry_table(pdpt[idx[1]]);
	if (!pd || !mm_pte_present(pd[idx[2]]))
		return NULL;
	if (paging_entry_large(pd[idx[2]]))
		return NULL;

	pt = paging_entry_table(pd[idx[2]]);
	if (!pt)
		return NULL;
	return &pt[idx[3]];
}

/*
 * Copy or zero user memory via mapped physical frames. Process CR3 may omit
 * PMM identity (Linux-like holes); switch to the pinned boot CR3 for PA
 * access (same as paging_copy_phys_page / paging_zero_phys_page).
 */

/*
 * FASE 23: snapshot of the last copy_to_user_region_in_directory attempt.
 *   [0] dst (initial)        [4] last page being processed
 *   [1] n   (initial)        [5] last pte present (0/1)
 *   [2] dst + n              [6] last phys frame address
 *   [3] pml4                 [7] call sequence number
 */
uint64_t fase23_copy_region_probe[8];
static uint64_t fase23_copy_region_seq;

/**
 * Kernel writes via PA identity must not touch RO+COW shared frames (that
 * bypasses hardware WP and corrupts the sibling mm). Break COW like #PF.
 */
static int paging_cow_break_leaf(uint64_t *pml4, uint64_t *pte, uintptr_t vaddr)
{
	uint64_t entry;
	uintptr_t old_phys;
	uintptr_t new_phys;
	uint64_t map_flags;
	unsigned long irq_flags;

	if (!pte || !(*pte & PAGE_PRESENT))
		return -1;
	if ((*pte & PAGE_RW))
		return 0;
	if (!(*pte & PAGE_COW) || !(*pte & PAGE_USER))
		return -1;

	irq_flags = irq_save();
	entry = *pte;
	old_phys = (uintptr_t)(entry & PAGE_PTE_PFN_MASK);
	new_phys = pmm_alloc_frame();
	if (!new_phys)
	{
		irq_restore(irq_flags);
		return -1;
	}
	paging_copy_phys_page(new_phys, old_phys);
	map_flags = (entry & 0xFFF) | PAGE_USER | PAGE_RW;
	map_flags &= ~(PAGE_COW | PAGE_GLOBAL);
	if (!(entry & PAGE_NX))
		map_flags |= PAGE_EXEC;
	if (map_page_in_directory(pml4, vaddr & ~0xFFFUL, new_phys, map_flags) != 0)
	{
		pmm_free_frame(new_phys);
		irq_restore(irq_flags);
		return -1;
	}
	pmm_frame_put(old_phys);
	tlb_invalidate_page(vaddr & ~0xFFFUL);
	irq_restore(irq_flags);
	return 0;
}

int copy_to_user_region_in_directory(uint64_t *pml4, uintptr_t dst,
                                     const void *src, size_t n)
{
    const uint8_t *s = src;
    uintptr_t dst0 = dst;
    size_t n0 = n;

    fase23_copy_region_probe[0] = (uint64_t)dst0;
    fase23_copy_region_probe[1] = (uint64_t)n0;
    fase23_copy_region_probe[2] = (uint64_t)(dst0 + n0);
    fase23_copy_region_probe[3] = (uint64_t)(uintptr_t)pml4;
    fase23_copy_region_probe[7] = ++fase23_copy_region_seq;

    if (!pml4 || !src)
        return -1;

    while (n > 0)
    {
        uintptr_t page = dst & (uintptr_t)PAGE_FRAME_MASK;
        uint64_t *pte = paging_get_pte(pml4, page);
        uintptr_t phys;
        size_t off;
        size_t chunk;

        fase23_copy_region_probe[4] = (uint64_t)page;
        fase23_copy_region_probe[5] = (pte && mm_pte_present(*pte)) ? 1ULL : 0ULL;

        if (!pte || !mm_pte_present(*pte))
            return -1;

	if (!(*pte & PAGE_RW) && (*pte & PAGE_COW))
	{
		if (paging_cow_break_leaf(pml4, pte, page) != 0)
			return -1;
		pte = paging_get_pte(pml4, page);
		if (!pte || !mm_pte_present(*pte) || !(*pte & PAGE_RW))
			return -1;
	}

        phys = paging_entry_pfn(*pte);
        fase23_copy_region_probe[6] = (uint64_t)phys;
        off = dst & 0xFFFU;
        chunk = (size_t)(0x1000U - off);
        if (chunk > n)
            chunk = n;

	{
		uint64_t saved;

		paging_phys_map_begin(&saved);
		memcpy((void *)(phys + off), s, chunk);
		paging_phys_map_end(saved);
	}
        dst += chunk;
        s += chunk;
        n -= chunk;
    }

    return 0;
}

int copy_from_user_region_in_directory(uint64_t *pml4, uintptr_t src,
                                       void *dst, size_t n)
{
    uint8_t *d = dst;
    uintptr_t src0 = src;
    size_t n0 = n;

    if (!pml4 || !dst)
        return -1;

    while (n > 0)
    {
        uintptr_t page = src & (uintptr_t)PAGE_FRAME_MASK;
        uint64_t *pte = paging_get_pte(pml4, page);
        uintptr_t phys;
        size_t off;
        size_t chunk;

        if (!pte || !mm_pte_present(*pte))
            return -1;

        phys = paging_entry_pfn(*pte);
        off = src & 0xFFFU;
        chunk = (size_t)(0x1000U - off);
        if (chunk > n)
            chunk = n;

	{
		uint64_t saved;

		paging_phys_map_begin(&saved);
		memcpy(d, (const void *)(phys + off), chunk);
		paging_phys_map_end(saved);
	}
        src += chunk;
        d += chunk;
        n -= chunk;
    }

    (void)src0;
    (void)n0;
    return 0;
}

int zero_user_region_in_directory(uint64_t *pml4, uintptr_t dst, size_t n)
{
    if (!pml4)
        return -1;

    while (n > 0)
    {
        uintptr_t page = dst & (uintptr_t)PAGE_FRAME_MASK;
        uint64_t *pte = paging_get_pte(pml4, page);
        uintptr_t phys;
        size_t off;
        size_t chunk;

        if (!pte || !mm_pte_present(*pte))
            return -1;

	if (!(*pte & PAGE_RW) && (*pte & PAGE_COW))
	{
		if (paging_cow_break_leaf(pml4, pte, page) != 0)
			return -1;
		pte = paging_get_pte(pml4, page);
		if (!pte || !mm_pte_present(*pte) || !(*pte & PAGE_RW))
			return -1;
	}

        phys = paging_entry_pfn(*pte);
        off = dst & 0xFFFU;
        chunk = (size_t)(0x1000U - off);
        if (chunk > n)
            chunk = n;

	{
		uint64_t saved;

		paging_phys_map_begin(&saved);
		memset((void *)(phys + off), 0, chunk);
		paging_phys_map_end(saved);
	}
        dst += chunk;
        n -= chunk;
    }

    return 0;
}

/**
 * Simple page table getter - only returns existing tables
 * NO dynamic allocation to avoid complexity
 */
static uint64_t *get_existing_table(uint64_t *table, size_t index)
{
    if (!mm_pte_present(table[index]))
        return NULL;

    if (paging_entry_large(table[index]))
        return NULL;

    return paging_entry_table(table[index]);
}

/**
 * Allocate a new page table if it doesn't exist
 * Returns physical address of the table, or 0 on failure
 */
static uint64_t alloc_page_table(int level)
{
    void *page = kmalloc_aligned_try(4096, 4096);
    fase43_oom_class_t oom_cls;

    if (!page)
    {
        oom_cls = paging_classify_oom();
        paging_fase43_note_oom("alloc_page_table", oom_cls);
        return 0;
    }
    
    /* Zero out the page */
    memset(page, 0, 4096);

    switch (level)
    {
    case 1:
        ir0_mm_pdpt_created++;
        break;
    case 2:
        ir0_mm_pd_created++;
        break;
    case 3:
        ir0_mm_pt_created++;
        break;
    default:
        break;
    }
    ir0_mm_set_frame_type((uint64_t)(uintptr_t)page, IR0_MM_FRAME_PT);
    ir0_mm_frame_pt_alloc++;
    ir0_mm_log_frame_type("ALLOC", (uint64_t)(uintptr_t)page, IR0_MM_FRAME_PT);

    /* Return physical address (identity mapped for now) */
    return (uint64_t)page;
}

/*
 * Split a 2 MiB PDE to 512×4 KiB leaves so a USER PTE can be installed.
 *
 * Linux huge-break: untouched slots are pte_none. Below PMM_PHYS_BASE keep
 * supervisor identity (kmalloc). In the PMM window use pte_none — kstacks
 * no longer live there (IR0_KSTACK_VA_BASE). Phys COW/zero: boot CR3.
 */
static int paging_split_large_pde(uint64_t *pd, size_t index)
{
	uint64_t pde;
	uint64_t phys_base;
	uint64_t pt_phys;
	uint64_t *pt;
	uint64_t leaf_flags;
	int exec;
	size_t i;

	if (!pd)
		return -1;
	pde = pd[index];
	if (!mm_pte_present(pde) || !paging_entry_large(pde))
		return -1;

	pt_phys = alloc_page_table(3);
	if (pt_phys == 0)
		return -1;
	pt = (uint64_t *)(uintptr_t)pt_phys;
	phys_base = paging_entry_pfn(pde);
	leaf_flags = PAGE_RW;
	if (pde & PAGE_NX)
		exec = 0;
	else
		exec = 1;

	for (i = 0; i < 512; i++)
	{
		uintptr_t pa = phys_base + (i * PAGE_SIZE_4KB);

		if (pa >= (uintptr_t)PMM_PHYS_BASE)
			pt[i] = 0;
		else
			pt[i] = mm_make_leaf_pte(pa, leaf_flags, exec);
	}

	pd[index] = mm_make_table_pte((uintptr_t)pt_phys, 0);
	tlb_invalidate_all();
	return 0;
}

/*
 * Ensure @vaddr is addressable via a 4KiB PTE (split a PD-level 2MiB leaf if
 * needed). Does not allocate a missing leaf or change an existing 4KiB PTE.
 * Returns 0 if paging_get_pte can proceed, -1 on failure.
 */
int paging_ensure_4k_leaf(uint64_t *pml4, uintptr_t vaddr)
{
	size_t idx[4];
	uint64_t *pdpt;
	uint64_t *pd;

	if (!pml4)
		return -1;

	mm_va_indices(vaddr, idx);

	if (!mm_pte_present(pml4[idx[0]]) || paging_entry_large(pml4[idx[0]]))
		return -1;
	pdpt = paging_entry_table(pml4[idx[0]]);
	if (!pdpt || !mm_pte_present(pdpt[idx[1]]) ||
	    paging_entry_large(pdpt[idx[1]]))
		return -1;
	pd = paging_entry_table(pdpt[idx[1]]);
	if (!pd || !mm_pte_present(pd[idx[2]]))
		return -1;
	if (paging_entry_large(pd[idx[2]]))
	{
		if (paging_split_large_pde(pd, idx[2]) != 0)
			return -1;
	}
	return 0;
}

/**
 * Get or create a page table at the specified level
 * @pml4: PML4 table address
 * @index: Index into the table
 * @create: If 1, create the table if it doesn't exist
 * Returns: Virtual address of the table (NULL if not present and create=0)
 */
static uint64_t *get_or_create_table(uint64_t *parent, size_t index, int create,
                                     uint64_t map_flags, int level)
{
	int user = !!(map_flags & PAGE_USER);

	if (!mm_pte_present(parent[index]))
	{
		uint64_t phys_addr;

		if (!create)
			return NULL;

		phys_addr = alloc_page_table(level);
		if (phys_addr == 0)
			return NULL;

		/*
		 * Linux: table entries use _KERNPG_TABLE / pgtable flags only — never
		 * NX on non-leaf levels.  Encode via mm_make_table_pte.
		 */
		parent[index] = mm_make_table_pte((uintptr_t)phys_addr, user);
		return (uint64_t *)paging_entry_pfn(phys_addr);
	}

	if (paging_entry_large(parent[index]))
	{
		if (!create || level != 3)
			return NULL;
		if (paging_split_large_pde(parent, index) != 0)
			return NULL;
	}

	/*
	 * Propagate PAGE_USER onto existing table levels (e.g. supervisor identity
	 * map created pdpt/pd/pt without U/S).  Linux requires user at all levels.
	 */
	if (user)
		mm_pte_set_user(&parent[index]);

	return paging_entry_table(parent[index]);
}

/**
 * Map a single 4KB page in a specific page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to map
 * @phys_addr: Physical address to map to
 * @flags: Page flags (PAGE_USER, PAGE_RW, etc.)
 * Returns: 0 on success, -1 on failure
 */
int map_page_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
	size_t idx[4];
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	if (!pml4)
		return -1;

	/*
	 * USER maps over the supervisor identity window are allowed: brk growth
	 * and ELF BSS split 2MiB leaves via paging_split_large_pde.
	 */

	mm_va_indices((uintptr_t)virt_addr, idx);

	pdpt = get_or_create_table(pml4, idx[0], 1, flags, 1);
	if (!pdpt)
		return -1;

	pd = get_or_create_table(pdpt, idx[1], 1, flags, 2);
	if (!pd)
		return -1;

	pt = get_or_create_table(pd, idx[2], 1, flags, 3);
	if (!pt)
		return -1;

	{
		uint64_t entry;
		int had_leaf;

		had_leaf = mm_pte_present(pt[idx[3]]);
		entry = mm_make_leaf_pte((uintptr_t)phys_addr, flags & 0xFFFULL,
					     !!(flags & PAGE_EXEC));
		pt[idx[3]] = entry;
		if (!had_leaf)
			ir0_mm_leaf_created++;

		if (flags & PAGE_USER)
		{
			ir0_mm_set_frame_type(phys_addr, IR0_MM_FRAME_USER);
			ir0_mm_frame_user_alloc++;
			ir0_mm_log_frame_type("ALLOC", phys_addr, IR0_MM_FRAME_USER);
		}
		else
		{
			ir0_mm_set_frame_type(phys_addr, IR0_MM_FRAME_KERNEL);
			ir0_mm_frame_kernel_alloc++;
			ir0_mm_log_frame_type("ALLOC", phys_addr, IR0_MM_FRAME_KERNEL);
		}
	}

	/* Flush TLB — skip invlpg: mapping runs under kernel CR3 and foreign
	 * user VAs may be absent from the active page tables; CR3 reload on
	 * context switch flushes user TLB entries anyway.
	 */

	return 0;
}

/**
 * Map a single 4KB page - SIMPLIFIED
 * Only works with existing page tables from boot
 * NO dynamic allocation (uses current CR3)
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    /* Get current CR3 (PML4 address) */
    uint64_t cr3 = get_current_page_directory();
    uint64_t *pml4 = (uint64_t *)cr3;
    
    return map_page_in_directory(pml4, virt_addr, phys_addr, flags);
}

/**
 * Unmap a single 4KB page in the page directory rooted at @pml4.
 * Safe when CR3 points at a different address space than @pml4.
 */
int unmap_page_in_directory(uint64_t *pml4, uintptr_t virt_addr)
{
	size_t idx[4];
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;
	uint64_t phys_frame;
	uint64_t pt_frame;
	uint64_t pd_frame;
	uint64_t pdpt_frame;
	ir0_mm_frame_type_t freed_type;

	if (!pml4)
		return -1;

	mm_va_indices(virt_addr, idx);

	if (!mm_pte_present(pml4[idx[0]]))
		return -1;
	if (paging_entry_large(pml4[idx[0]]))
		return -1;
	pdpt = paging_entry_table(pml4[idx[0]]);
	if (!pdpt || !mm_pte_present(pdpt[idx[1]]))
		return -1;
	if (paging_entry_large(pdpt[idx[1]]))
		return -1;
	pd = paging_entry_table(pdpt[idx[1]]);
	if (!pd || !mm_pte_present(pd[idx[2]]))
		return -1;
	if (paging_entry_large(pd[idx[2]]))
		return -1;
	pt = paging_entry_table(pd[idx[2]]);
	if (!pt)
		return -1;

	if (!mm_pte_present(pt[idx[3]]))
		return -1;

	phys_frame = paging_entry_pfn(pt[idx[3]]);
	pt[idx[3]] = 0;
    ir0_mm_leaf_freed++;

    tlb_invalidate_page((uintptr_t)virt_addr);

    /*
     * Only return frames that the PMM allocated from RAM. MMIO and other
     * identity-mapped device pages must not touch the bitmap.
     */
    if (phys_frame &&
        phys_frame >= pmm_get_start() && phys_frame < pmm_get_end())
    {
        freed_type = ir0_mm_get_frame_type(phys_frame);
        if (freed_type == IR0_MM_FRAME_USER)
            ir0_mm_frame_user_free++;
        else if (freed_type == IR0_MM_FRAME_KERNEL)
            ir0_mm_frame_kernel_free++;
        else if (freed_type == IR0_MM_FRAME_PT)
            ir0_mm_frame_pt_free++;
        ir0_mm_set_frame_type(phys_frame, IR0_MM_FRAME_UNKNOWN);
        ir0_mm_log_frame_type("FREE", phys_frame, freed_type);
        pmm_free_frame(phys_frame);
    }

	if (page_table_is_empty(pt))
	{
		pt_frame = paging_entry_pfn(pd[idx[2]]);
		pd[idx[2]] = 0;
		ir0_mm_free_table_frame(pt_frame, 3);
	}

	if (page_table_is_empty(pd))
	{
		pd_frame = paging_entry_pfn(pdpt[idx[1]]);
		pdpt[idx[1]] = 0;
		ir0_mm_free_table_frame(pd_frame, 2);
	}

	if (idx[0] < 256 && page_table_is_empty(pdpt))
	{
		pdpt_frame = paging_entry_pfn(pml4[idx[0]]);
		pml4[idx[0]] = 0;
		ir0_mm_free_table_frame(pdpt_frame, 1);
	}

	return 0;
}

/**
 * Unmap a single 4KB page in the current address space
 */
int unmap_page(uint64_t virt_addr)
{
    uint64_t cr3 = get_current_page_directory();

    return unmap_page_in_directory((uint64_t *)cr3, (uintptr_t)virt_addr);
}


/* Map user page with U/S=1 permissions */
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags)
{
    /* Add user flag (U/S=1) */
    flags |= PAGE_USER;

    /* Map the page */
    return map_page(virtual_addr, physical_addr, flags);
}

/* Map user memory region in current page directory */
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags)
{
    uint64_t cr3 = get_current_page_directory();
    uint64_t *pml4 = (uint64_t *)cr3;
    return map_user_region_in_directory(pml4, virtual_start, size, flags);
}

/* Map user memory region in a specific page directory */
int map_user_region_in_directory(uint64_t *pml4, uintptr_t virtual_start, size_t size, uint64_t flags)
{
    if (!pml4)
        return -1;
    
    /* Align to 4KB */
    virtual_start &= (uintptr_t)PAGE_FRAME_MASK;
    size = (size + 0xFFF) & (size_t)PAGE_FRAME_MASK;

    /* Add user flag */
    flags |= PAGE_USER;

    /* Map each page; on failure, rollback all mappings we made */
    for (size_t offset = 0; offset < size; offset += 0x1000)
    {
        uintptr_t virt_addr = virtual_start + offset;

        uintptr_t phys_addr = pmm_alloc_frame();
        if (phys_addr == 0)
        {
            /* Rollback: unmap everything we mapped so far */
            for (size_t rb = 0; rb < offset; rb += 0x1000)
                unmap_page_in_directory(pml4, virtual_start + rb);
            return -1;
        }

        /* Linux: clear_highpage() before exposing anonymous user pages */
        paging_zero_phys_page(phys_addr);

        if (map_page_in_directory(pml4, virt_addr, phys_addr, flags) != 0)
        {
            pmm_free_frame(phys_addr);
            for (size_t rb = 0; rb < offset; rb += 0x1000)
                unmap_page_in_directory(pml4, virtual_start + rb);
            return -1;
        }
    }

    return 0;
}

/*
 * copy_process_memory - Share user pages from parent to child (fork COW)
 * @parent: Parent process
 * @child: Child process
 *
 * Two phases so a failed fork never mutates the parent address space:
 *
 * 1. Map each present PAGE_USER 4KB leaf into the child (share PFN via
 *    pmm_frame_get). Child PTEs for formerly writable pages are RO+PAGE_COW.
 * 2. Only after all child maps succeed, mark the same pages RO+COW in
 *    the parent and flush TLB.
 *
 * Returns: 0 on success, -1 on failure
 */
int copy_process_memory(struct process *parent, struct process *child)
{
    size_t i4;
    size_t i3;
    size_t i2;
    size_t i1;
    uint64_t *parent_pml4;
    uint64_t *child_pml4;

    if (!parent || !child)
        return -1;

    if (!process_pgd(parent) || !process_pgd(child))
        return -1;

    parent_pml4 = process_pgd(parent);
    child_pml4 = process_pgd(child);

    for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
    {
        uint64_t *pdpt = get_existing_table(parent_pml4, i4);

        if (!pdpt)
            continue;

        for (i3 = 0; i3 < 512; i3++)
        {
            uint64_t *pd = get_existing_table(pdpt, i3);

            if (!pd)
                continue;

            for (i2 = 0; i2 < 512; i2++)
            {
                uint64_t *pt = get_existing_table(pd, i2);

                if (!pt)
                    continue;

                for (i1 = 0; i1 < 512; i1++)
                {
                    uint64_t page_entry = pt[i1];
                    uint64_t parent_phys;
                    uint64_t flags;
                    uintptr_t virt_addr;
                    int was_writable;

                    if (!mm_pte_present(page_entry) ||
                        !(page_entry & PAGE_USER))
                        continue;

                    parent_phys = paging_entry_pfn(page_entry);
                    virt_addr = ((uintptr_t)i4 << 39) |
                                ((uintptr_t)i3 << 30) |
                                ((uintptr_t)i2 << 21) |
                                ((uintptr_t)i1 << 12);

                    was_writable = (page_entry & PAGE_RW) != 0;
                    flags = page_entry & 0xFFF;
                    flags &= ~PAGE_GLOBAL;
                    flags |= PAGE_USER;
                    if (was_writable || (page_entry & PAGE_COW))
                    {
                        flags &= ~PAGE_RW;
                        flags |= PAGE_COW;
                    }
                    if (!(page_entry & PAGE_NX))
                        flags |= PAGE_EXEC;

                    pmm_frame_get(parent_phys);

                    if (map_page_in_directory(child_pml4, virt_addr,
                                              parent_phys, flags) != 0)
                    {
                        pmm_frame_put(parent_phys);
                        return -1;
                    }

                    if (DEBUG_FORK && fase40_copy_diag_events < 256U)
                        fase40_copy_diag_events++;
                }
            }
        }
    }

    for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
    {
        uint64_t *pdpt = get_existing_table(parent_pml4, i4);

        if (!pdpt)
            continue;

        for (i3 = 0; i3 < 512; i3++)
        {
            uint64_t *pd = get_existing_table(pdpt, i3);

            if (!pd)
                continue;

            for (i2 = 0; i2 < 512; i2++)
            {
                uint64_t *pt = get_existing_table(pd, i2);

                if (!pt)
                    continue;

                for (i1 = 0; i1 < 512; i1++)
                {
                    uint64_t page_entry = pt[i1];

                    if (!mm_pte_present(page_entry) ||
                        !(page_entry & PAGE_USER) ||
                        !(page_entry & PAGE_RW))
                        continue;

                    pt[i1] = (page_entry & ~(PAGE_RW | PAGE_GLOBAL)) | PAGE_COW;
                }
            }
        }
    }

    /*
     * Flush against the parent's CR3 value. Syscalls keep user CR3, but if
     * we ever run under a different root, reloading only that root would leave
     * stale RW TLB entries and let the parent write shared frames without COW.
     */
    {
        uintptr_t parent_root = (uintptr_t)process_mm_root(parent);
        uintptr_t saved_root = mm_current_root();

        if (parent_root != 0 && saved_root != parent_root)
            mm_activate(parent_root);
        tlb_invalidate_all();
        if (parent_root != 0 && saved_root != parent_root)
            mm_activate(saved_root);
    }
    return 0;
}


#if CONFIG_ENABLE_VBE
/*
 * Supervisor-only identity map for the linear framebuffer MMIO window.
 * Syscalls run ring 0 with process CR3; /dev/console FB writes need this
 * mapping in every process page table (not userspace mmap).
 */
static void map_supervisor_framebuffer_mmio(uint64_t *pml4)
{
    uint32_t fb_phys;
    uint32_t fb_size;
    uint32_t off;

    if (!pml4 || !video_backend_is_available())
        return;

    fb_phys = video_backend_get_fb_phys();
    fb_size = video_backend_get_fb_size();
    if (fb_phys == 0 || fb_size == 0)
        return;
    if (fb_size > (4U * 1024U * 1024U))
        fb_size = 4U * 1024U * 1024U;

    for (off = 0; off < fb_size; off += PAGE_SIZE_4KB)
    {
        uint64_t p = (uint64_t)fb_phys + off;

        if (map_page_in_directory(pml4, p, p, PAGE_PRESENT | PAGE_RW) != 0)
            break;
    }
}
#else
static void map_supervisor_framebuffer_mmio(uint64_t *pml4)
{
    (void)pml4;
}
#endif

/*
 * map_supervisor_identity_low - 4 KiB identity map for kernel low memory
 *
 * Boot uses 2 MiB huge pages in PML4[0]; inheriting that tree blocks user
 * ELF at 0x400000. Each process instead gets fresh 4 KiB supervisor mappings
 * for the low identity range so timer IRQ (TSS RSP0) and syscall entry can
 * run with process CR3 loaded (Linux/BSD: kernel always reachable from user mm).
 */
int map_supervisor_identity_low(uint64_t *pml4, uint64_t start, uint64_t end)
{
    uint64_t va;

    if (!pml4 || end <= start)
        return -1;

    start &= ~(uint64_t)(PAGE_SIZE_4KB - 1);
    end = (end + PAGE_SIZE_4KB - 1) & ~(uint64_t)(PAGE_SIZE_4KB - 1);

    for (va = start; va < end; va += PAGE_SIZE_4KB)
    {
        /*
         * IRQ/syscall entry runs kernel text under process CR3; omit NX on
         * the identity map (Linux keeps kernel text executable in every mm).
         */
        if (map_page_in_directory(pml4, va, va,
                                  PAGE_PRESENT | PAGE_RW | PAGE_EXEC) != 0)
            return -1;
    }

    map_supervisor_framebuffer_mmio(pml4);
    /*
     * AHCI ABAR is high MMIO (not in low identity). Map into every process
     * CR3 so ir0_block_* DMA setup can touch port registers under syscall.
     */
    ahci_map_mmio_in_directory(pml4);

    return 0;
}

/*
 * map_supervisor_identity_2mb - 2 MiB supervisor identity (PD-level PS bit)
 *
 * Cheaper than 4 KiB walks for the kernel heap and PMM frame pool. Callers must
 * not overlap [0x400000, 0x600000): a 2 MiB slot there would block user ELF.
 */
int map_supervisor_identity_2mb(uint64_t *pml4, uint64_t start, uint64_t end)
{
    uint64_t va;

#if !CONFIG_ARCH_X86_64
    return map_supervisor_identity_low(pml4, start, end);
#else
    if (!pml4 || end <= start)
        return -1;

    start = (start + PAGE_SIZE_2MB - 1) & ~((uint64_t)PAGE_SIZE_2MB - 1);
    end &= ~((uint64_t)PAGE_SIZE_2MB - 1);
    if (end <= start)
        return 0;

    for (va = start; va < end; va += PAGE_SIZE_2MB)
    {
        size_t idx[4];
        uint64_t *pdpt;
        uint64_t *pd;
        uint64_t entry;

        mm_va_indices((uintptr_t)va, idx);

        pdpt = get_or_create_table(pml4, idx[0], 1, 0, 1);
        if (!pdpt)
            return -1;

        pd = get_or_create_table(pdpt, idx[1], 1, 0, 2);
        if (!pd)
            return -1;

        if (mm_pte_present(pd[idx[2]]))
        {
            if (paging_entry_large(pd[idx[2]]))
                continue;
            return -1;
        }

        entry = mm_make_leaf_pte((uintptr_t)va,
                                 PAGE_RW | PAGE_SIZE_2MB_FLAG, 1);
        pd[idx[2]] = entry;
        ir0_mm_leaf_created++;
    }

    return 0;
#endif
}

void paging_reclaim_lower_half_tables(uint64_t *pml4)
{
    size_t i4;

    if (!pml4)
        return;

    for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
    {
        uint64_t pml4e = pml4[i4];
        uint64_t *pdpt;
        size_t i3;

        if (!mm_pte_present(pml4e) || paging_entry_large(pml4e))
            continue;

        pdpt = paging_entry_table(pml4e);
        if (!pdpt)
            continue;

        for (i3 = 0; i3 < 512; i3++)
        {
            uint64_t pdpte = pdpt[i3];
            uint64_t *pd;
            size_t i2;

            if (!mm_pte_present(pdpte) || paging_entry_large(pdpte))
                continue;

            pd = paging_entry_table(pdpte);
            if (!pd)
                continue;

            for (i2 = 0; i2 < 512; i2++)
            {
                uint64_t pde = pd[i2];
                uint64_t *pt;
                size_t i1;

                if (!mm_pte_present(pde))
                    continue;

                /* Supervisor 2 MiB identity: drop PDE only (PFN is not a PT). */
                if (paging_entry_large(pde))
                {
                    pd[i2] = 0;
                    continue;
                }

                pt = paging_entry_table(pde);
                if (!pt)
                    continue;

                for (i1 = 0; i1 < 512; i1++)
                    pt[i1] = 0;

                pd[i2] = 0;
                ir0_mm_free_table_frame(paging_entry_pfn(pde), 3);
            }

            pdpt[i3] = 0;
            ir0_mm_free_table_frame(paging_entry_pfn(pdpte), 2);
        }

        pml4[i4] = 0;
        ir0_mm_free_table_frame(paging_entry_pfn(pml4e), 1);
    }
}

void paging_ir0_mm_note_pml4_created(uint64_t pml4_phys)
{
    ir0_mm_pml4_created++;
    ir0_mm_set_frame_type(pml4_phys, IR0_MM_FRAME_PT);
    ir0_mm_frame_pt_alloc++;
    ir0_mm_log_frame_type("ALLOC", pml4_phys, IR0_MM_FRAME_PT);
}

void paging_ir0_mm_note_pml4_freed(uint64_t pml4_phys)
{
    ir0_mm_pml4_freed++;
    ir0_mm_set_frame_type(pml4_phys, IR0_MM_FRAME_UNKNOWN);
    ir0_mm_frame_pt_free++;
    ir0_mm_log_frame_type("FREE", pml4_phys, IR0_MM_FRAME_PT);
}

void paging_ir0_mm_checkpoint(const char *tag, int32_t pid)
{
#if !IR0_DEBUG_PROC
    (void)tag;
    (void)pid;
    return;
#else

#endif
}

void paging_ir0_mm_category_stats(uint64_t *user_alloc, uint64_t *user_free,
                                  uint64_t *pt_alloc, uint64_t *pt_free,
                                  uint64_t *kernel_alloc, uint64_t *kernel_free)
{
    if (user_alloc)
        *user_alloc = ir0_mm_frame_user_alloc;
    if (user_free)
        *user_free = ir0_mm_frame_user_free;
    if (pt_alloc)
        *pt_alloc = ir0_mm_frame_pt_alloc;
    if (pt_free)
        *pt_free = ir0_mm_frame_pt_free;
    if (kernel_alloc)
        *kernel_alloc = ir0_mm_frame_kernel_alloc;
    if (kernel_free)
        *kernel_free = ir0_mm_frame_kernel_free;
}

static ir0_mm_frame_type_t paging_fase47_frame_type(uintptr_t phys)
{
    return ir0_mm_get_frame_type((uint64_t)phys);
}

void paging_fase47_steady_state_audit(const char *tag, uint64_t frames_baseline,
                                      uint64_t mm_created, uint64_t mm_destroyed)
{
    size_t total_frames = 0;
    size_t used_frames = 0;
    size_t i;
    size_t nframes;
    uint64_t alive_user_leaf = 0;
    uint64_t alive_page_table = 0;
    uint64_t alive_kernel_heap = 0;
    uint64_t alive_process_struct = 0;
    uint64_t alive_vma_meta = 0;
    uint64_t alive_file_cache = 0;
    uint64_t alive_unknown = 0;
    uint64_t leaf_alive;
    uint64_t mm_alive;
    uint64_t orphan_frames = 0;
    uint64_t double_free = 0;
    uint64_t alive_owner_missing = 0;
    int inv_leaf;
    int inv_mm;
    int inv_frames;
    int class_steady;
    int class_resident;
    int class_accounting;
    int class_leak;
    const char *memory_class;

    pmm_stats(&total_frames, &used_frames, NULL);
    nframes = pmm_fase47_total_frames();

    for (i = 0; i < nframes; i++)
    {
        ir0_mm_frame_type_t ft;
        int32_t owner;
        uintptr_t phys;

        if (!pmm_fase47_frame_is_used(i))
            continue;

        phys = pmm_fase47_frame_phys(i);
        ft = paging_fase47_frame_type(phys);
        owner = pmm_fase47_frame_owner(i);

        if (ft == IR0_MM_FRAME_USER)
            alive_user_leaf++;
        else if (ft == IR0_MM_FRAME_PT)
            alive_page_table++;
        else if (ft == IR0_MM_FRAME_KERNEL)
        {
            if (owner > 0)
                alive_process_struct++;
            else
                alive_kernel_heap++;
        }
        else
            alive_unknown++;
    }

    leaf_alive = (ir0_mm_leaf_created >= ir0_mm_leaf_freed) ?
                 (ir0_mm_leaf_created - ir0_mm_leaf_freed) : 0;
    mm_alive = (mm_created >= mm_destroyed) ?
               (mm_created - mm_destroyed) : 0;

    inv_leaf = (ir0_mm_leaf_created == ir0_mm_leaf_freed + leaf_alive);
    inv_mm = (mm_created == mm_destroyed + mm_alive);
    inv_frames = (frames_baseline == 0 ||
                  (uint64_t)used_frames <= (frames_baseline * 110ULL / 100ULL));

    pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);

    (void)alive_user_leaf;
    (void)alive_page_table;
    (void)alive_kernel_heap;
    (void)alive_process_struct;
    (void)alive_vma_meta;
    (void)alive_file_cache;
    (void)alive_unknown;
    (void)double_free;
    (void)tag;

    class_accounting = (!inv_leaf || !inv_mm);
    class_leak = (orphan_frames > 0 || alive_owner_missing > 0 ||
                  (!inv_leaf && leaf_alive > alive_user_leaf + alive_page_table));
    class_steady = (inv_leaf && inv_mm && inv_frames && !class_leak);
    class_resident = (!inv_frames && inv_leaf && inv_mm && !class_leak);

    if (class_steady)
        memory_class = "MEMORY_STEADY_OK";
    else if (class_accounting)
        memory_class = "MEMORY_ACCOUNTING_BROKEN";
    else if (class_leak)
        memory_class = "MEMORY_LEAK_CONFIRMED";
    else if (class_resident)
        memory_class = "MEMORY_RESIDENT_EXPECTED";
    else
        memory_class = "MEMORY_RESIDENT_EXPECTED";
    (void)memory_class;
}
