/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm.c
 * Description: Process MM: user unmap, PML4 create, VA overlap, mmap list clone/free.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/mm.h>

static bool process_va_ranges_overlap(uintptr_t a_start, size_t a_len,
				      uintptr_t b_start, size_t b_len)
{
	uintptr_t a_end;
	uintptr_t b_end;

	if (a_len == 0 || b_len == 0)
		return false;

	a_end = a_start + a_len;
	b_end = b_start + b_len;
	return a_start < b_end && b_start < a_end;
}

bool process_user_va_range_overlaps(process_t *proc, uintptr_t addr, size_t length)
{
	struct mmap_region *r;

	if (!proc || length == 0)
		return false;

	if (process_heap_end(proc) > process_heap_start(proc) &&
	    process_va_ranges_overlap(addr, length, process_heap_start(proc),
				      (size_t)(process_heap_end(proc) - process_heap_start(proc))))
		return true;

	if (process_stack_size(proc) > 0 &&
	    process_va_ranges_overlap(addr, length, process_stack_start(proc),
				      (size_t)process_stack_size(proc)))
		return true;

	for (r = process_mmap_list(proc); r; r = r->next)
	{
		if (process_va_ranges_overlap(addr, length, (uintptr_t)r->addr,
					      r->length))
			return true;
	}

	return false;
}

uint64_t *process_pt_child(uint64_t *table, size_t index)
{
	if (!(table[index] & PAGE_PRESENT))
		return NULL;
	if (table[index] & PAGE_SIZE_2MB_FLAG)
		return NULL;
	return (uint64_t *)(table[index] & PAGE_FRAME_MASK);
}

uint64_t mm_count_resident_user_pages(const mm_struct_t *mm)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!mm || !mm->page_directory)
		return 0;

	pml4 = mm->page_directory;
	for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;

		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);
			uint64_t pdpt_ent;

			if (!pd)
			{
				pdpt_ent = pdpt[i3];
				if ((pdpt_ent & PAGE_PRESENT) && (pdpt_ent & PAGE_USER) &&
				    (pdpt_ent & PAGE_SIZE_2MB_FLAG))
					count += 512;
				continue;
			}

			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t pd_ent = pd[i2];
				uint64_t *pt;

				if (!(pd_ent & PAGE_PRESENT))
					continue;
				if ((pd_ent & PAGE_USER) && (pd_ent & PAGE_SIZE_2MB_FLAG))
				{
					count += 512;
					continue;
				}

				pt = process_pt_child(pd, i2);
				if (!pt)
					continue;

				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];

					if ((ent & PAGE_PRESENT) && (ent & PAGE_USER))
						count++;
				}
			}
		}
	}

	return count;
}

uint64_t process_count_resident_user_pages(const process_t *p)
{
	if (!p || !p->mm)
		return 0;

	return mm_count_resident_user_pages(p->mm);
}

/*
 * Drop every present PAGE_USER mapping under PML4 indices 0..255 so PMM
 * frames are returned and the address space can be discarded safely while
 * another process is active (CR3 unrelated).
 */
void process_unmap_user_pages_all(uint64_t *pml4,
					 process_reclaim_stats_t *stats)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;

	if (!pml4)
		return;

	for (i4 = 0; i4 < (size_t)mm_user_root_slots(); i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		if (stats)
			stats->pdpt_present++;

		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			if (stats)
				stats->pd_present++;

			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				if (stats)
					stats->pt_present++;

				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];
					uintptr_t virt;

					if (!(ent & PAGE_PRESENT) || !(ent & PAGE_USER))
						continue;
					if (stats)
					{
						stats->mapped_pages++;
						stats->leaf_present++;
					}

					virt = ((uintptr_t)i4 << 39) | ((uintptr_t)i3 << 30) |
					       ((uintptr_t)i2 << 21) | ((uintptr_t)i1 << 12);
					if (unmap_page_in_directory(pml4, virt) == 0)
					{
						if (stats)
						{
							stats->freed_pages++;
							stats->leaf_freed++;
						}
					}
					else if (stats)
					{
						stats->missing_pages++;
					}
				}
			}
		}
	}
}


void process_unmap_user_address_space(process_t *p)
{
	process_reclaim_stats_t stats;
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;
	memset(&stats, 0, sizeof(stats));

	process_unmap_user_pages_all(process_pgd(p), &stats);
	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	if (IR0_DEBUG_PROC)
	{
	}
}


struct mmap_region *process_clone_mmap_list(struct mmap_region *parent_list)
{
	struct mmap_region *head = NULL;
	struct mmap_region *tail = NULL;
	struct mmap_region *walk;

	for (walk = parent_list; walk; walk = walk->next)
	{
		struct mmap_region *node = kmalloc_try(sizeof(*node));

		if (!node)
		{
			while (head)
			{
				struct mmap_region *next = head->next;

				kfree(head);
				head = next;
			}
			return NULL;
		}

		memcpy(node, walk, sizeof(*node));
		node->next = NULL;

		if (!head)
			head = node;
		else
			tail->next = node;
		tail = node;
	}

	return head;
}

void process_fork_destroy_child_mm(process_t *child)
{
	if (!child)
		return;

	if (child->mm)
	{
		mm_put(child->mm);
		child->mm = NULL;
		process_fase43_note_mm_destroyed();
	}
}

void process_fork_free_mmap_list(process_t *child)
{
	struct mmap_region *r;
	struct mmap_region *next;

	if (!child)
		return;

	r = process_mmap_list(child);
	while (r)
	{
		next = r->next;
		kfree(r);
		r = next;
	}
	process_mm_set_mmap_list(child, NULL);
}

uint64_t create_process_page_directory(void)
{
	uint64_t *pml4;
	uint64_t kernel_cr3;
	uint64_t *kernel_pml4;

	/* Allocate page-aligned memory for PML4 */
	pml4 = kmalloc_aligned_try(4096, 4096);
	if (!pml4)
	{
		paging_fase43_note_oom("create_process_page_directory",
				       paging_fase43_classify_current());
		return 0;
	}

	memset(pml4, 0, 4096);
	/*
	 * Kernel half must come from the pinned boot PML4, not current CR3.
	 * Fork under a user mm must still share kstack / high kernel PTEs
	 * (switch loads next CR3 while RSP is still on the previous kstack).
	 */
	kernel_cr3 = paging_get_kernel_cr3();
	if (!kernel_cr3)
	{
		kernel_cr3 = get_current_page_directory();
		paging_pin_kernel_cr3(kernel_cr3);
	}
	kernel_pml4 = (uint64_t *)(uintptr_t)kernel_cr3;

	/*
	 * Copy kernel half of the root table only (user half stays empty).
	 * Slot counts are ISA-private (mm_copy_kernel_half).
	 */
	mm_copy_kernel_half(pml4, kernel_pml4);

	/*
	 * Map kernel low memory with 4 KiB supervisor pages so timer IRQ (TSS
	 * RSP0), syscall handlers, and kernel text/data are reachable under
	 * process CR3.  Do not cover the user ELF load window (0x400000+): table
	 * entries without PAGE_USER there block user code fetch (Linux requires
	 * PAGE_USER on every level for user mappings).
	 */
	{
		/*
		 * Supervisor identity under process CR3:
		 *  - [0, 4MiB) + kbd…6MiB: kernel image / IRQ
		 *  - [6MiB, 32MiB): kmalloc heap (still low identity)
		 * Kstacks are at IR0_KSTACK_VA_BASE (high). Do NOT map PMM
		 * [32MiB, 512MiB) — user brk/mmap + frames use demand-zero /
		 * boot-CR3 phys access (Linux direct-map split).
		 */
		const uint64_t supervisor_kbd_end = 0x00600000UL;

		if (map_supervisor_identity_low(pml4, 0, 0x00400000UL) != 0)
		{
			kfree_aligned(pml4);
			return 0;
		}
		if (map_supervisor_identity_low(pml4, KEYBOARD_BUFFER_ADDR,
						supervisor_kbd_end) != 0)
		{
			kfree_aligned(pml4);
			return 0;
		}
		if (map_supervisor_identity_2mb(pml4, supervisor_kbd_end,
						PMM_PHYS_BASE) != 0)
		{
			kfree_aligned(pml4);
			return 0;
		}
	}

	/*
	 * Copy canonical kernel-half entries if present (future high-half kernel).
	 */

	/*
	 * Explicitly map framebuffer into process so console output is visible.
	 * Framebuffer is often above 32MB (e.g. 0xFD000000) and may not be
	 * in the copied low-memory mapping.
	 */
#if CONFIG_ENABLE_VBE
	if (video_backend_is_available() && video_backend_get_fb_phys() != 0)
	{
#if !defined(IR0_USERSPACE_INIT_BOOT) || !IR0_USERSPACE_INIT_BOOT
		uint32_t fb_phys = video_backend_get_fb_phys();
		uint32_t fb_size = video_backend_get_fb_size();
		/* Cap: avoid multi-second page-table walks on large framebuffers at spawn. */
		if (fb_size > (4U * 1024U * 1024U))
			fb_size = 4U * 1024U * 1024U;
		for (uint32_t off = 0; off < fb_size; off += 4096)
		{
			uint64_t p = fb_phys + off;
			if (map_page_in_directory(pml4, p, p, PAGE_PRESENT | PAGE_RW) != 0)
				break;
		}
#endif
	}
#endif

	paging_ir0_mm_note_pml4_created((uint64_t)(uintptr_t)pml4);
	process_fase43_note_mm_created();
	return (uint64_t)pml4;
}

