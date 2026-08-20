/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: paging.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4
#define PAGE_WRITETHROUGH 0x8
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY 0x40
#define PAGE_SIZE_2MB_FLAG 0x80
#define PAGE_GLOBAL 0x100
/*
 * Software-available PTE bit 9 (ignored by hardware). Marks a present user
 * page that was shared read-only at fork and must break on write.
 */
#define PAGE_COW 0x200

/* PTE bit 63: no-execute when IA32_EFER.NXE is set (not stored in low 12 bits of API flags) */
#define PAGE_NX (1ULL << 63)
/* Software flag for map_page*: page must remain executable (omit PAGE_NX in PTE) */
#define PAGE_EXEC (1ULL << 52)


#define PAGE_SIZE_4KB (4 * 1024)
#define PAGE_SIZE_2MB (2 * 1024 * 1024)
#define PAGE_SIZE_1GB (1024 * 1024 * 1024)

/* Physical frame: align/strip low 12 bits from an address */
#define PAGE_FRAME_MASK     (~0xFFFULL)
/*
 * x86-64 PTE/PMD/PUD/PGD PFN field (Linux PTE_PFN_MASK): bits 51:12 only.
 * Do NOT use PAGE_FRAME_MASK on table entries — NX (bit 63) would leak into
 * pointers when walking page tables (see Linux pud_page()/pte_pfn()).
 */
#define PAGE_PTE_PFN_MASK   0x000FFFFFFFFFF000ULL
/* Nine-bit index into PML4/PDPT/PD/PT (512 entries) */
#define PAGE_INDEX_MASK     0x1FF

/* CR0: paging and write-protect */
#define CR0_PG              (1UL << 31)
#define CR0_WP              (1UL << 16)
/* CR4: physical address extension (required before long-mode paging) */
#define CR4_PAE             (1UL << 5)


/**
 * Enable paging (sets CR0.PG bit)
 */
void enable_paging(void);

/**
 * Complete paging setup: configure tables and enable paging
 * This function handles all paging configuration from C code
 */
void setup_and_enable_paging(void);

/**
 * Load page directory root via mm_activate (ISA-neutral).
 */
void load_page_directory(uint64_t pml4_addr);

/**
 * Get current page directory root via mm_current_root.
 */
uint64_t get_current_page_directory(void);

/**
 * Check if paging is enabled
 */
int is_paging_enabled(void);

/**
 * Map a virtual address to a physical address
 * Note: This is a simple implementation for testing
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * Map a virtual address to a physical address in a specific page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to map
 * @phys_addr: Physical address to map to
 * @flags: Page flags (PAGE_USER, PAGE_RW, etc.)
 * Returns: 0 on success, -1 on failure
 */
int map_page_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * Check if a virtual address is mapped in a page directory
 * @pml4: PML4 table address (page directory)
 * @virt_addr: Virtual address to check
 * @flags_out: Optional output for page flags (can be NULL)
 * Returns: 1 if mapped (including 2 MiB/1 GiB leaves), 0 if not mapped, -1 on error
 */
int is_page_mapped_in_directory(uint64_t *pml4, uint64_t virt_addr, uint64_t *flags_out);

/**
 * Walk 4-level paging to the PTE for @a vaddr in @a pml4.
 * Returns NULL if any level is missing or a huge page is encountered.
 * The returned pointer is valid even when the leaf PTE is not present.
 */
uint64_t *paging_get_pte(uint64_t *pml4, uintptr_t vaddr);

/**
 * Unmap a 4KB page in an explicit page directory (PML4 root).
 * Does not assume current CR3 matches @pml4.
 */
int unmap_page_in_directory(uint64_t *pml4, uintptr_t virt_addr);

/**
 * Unmap a virtual address in the current address space (current CR3)
 */
int unmap_page(uint64_t virt_addr);

/* Map user page with U/S=1 */
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags);

/* Map user region in current page directory */
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags);

/* Map user region in a specific page directory */
int map_user_region_in_directory(uint64_t *pml4, uintptr_t virtual_start, size_t size, uint64_t flags);

/*
 * Map a supervisor-only identity region with 4 KiB pages (not boot 2 MiB huge
 * pages). User mappings added later override overlapping VAs (e.g. ELF at
 * 0x400000). Required so IRQ/syscall handlers and TSS RSP0 work under
 * process CR3 while the kernel image lives in low canonical memory.
 */
int map_supervisor_identity_low(uint64_t *pml4, uint64_t start, uint64_t end);

/*
 * Supervisor-only 2 MiB identity map (PD-level PS). Use for heap/PMM ranges that
 * do not overlap the user ELF load window (0x400000); COW break uses memcpy via
 * physical VA under process CR3.
 */
int map_supervisor_identity_2mb(uint64_t *pml4, uint64_t start, uint64_t end);

/* Copy/zero user VAs via page-table walk (kernel CR3, no user CR3 switch) */
int copy_to_user_region_in_directory(uint64_t *pml4, uintptr_t dst,
                                     const void *src, size_t n);
int copy_from_user_region_in_directory(uint64_t *pml4, uintptr_t src,
                                       void *dst, size_t n);
int zero_user_region_in_directory(uint64_t *pml4, uintptr_t dst, size_t n);

struct process;

/**
 * Create a new page directory for a user process
 * Returns physical address of the new PML4 table
 */
uint64_t create_process_page_directory(void);

/**
 * Copy memory from parent process to child process
 * Used for fork() implementation
 */
int copy_process_memory(struct process *parent, struct process *child);
void paging_reclaim_lower_half_tables(uint64_t *pml4);

/*
 * IR0 MM frame/PT bookkeeping: emit page-table/frame balance counters tagged by phase.
 */
void paging_ir0_mm_note_pml4_created(uint64_t pml4_phys);
void paging_ir0_mm_note_pml4_freed(uint64_t pml4_phys);
void paging_ir0_mm_checkpoint(const char *tag, int32_t pid);
void paging_ir0_mm_category_stats(uint64_t *user_alloc, uint64_t *user_free,
                                  uint64_t *pt_alloc, uint64_t *pt_free,
                                  uint64_t *kernel_alloc, uint64_t *kernel_free);
void paging_fase47_steady_state_audit(const char *tag, uint64_t frames_baseline,
                                      uint64_t mm_created, uint64_t mm_destroyed);

typedef enum
{
    FASE43_OOM_BOOT_FATAL = 0,
    FASE43_OOM_KERNEL_FATAL = 1,
    FASE43_OOM_USER_RECOVERABLE = 2
} fase43_oom_class_t;

void paging_fase43_note_oom(const char *site, fase43_oom_class_t cls);
void paging_fase43_oom_audit(const char *tag);
fase43_oom_class_t paging_fase43_classify_current(void);