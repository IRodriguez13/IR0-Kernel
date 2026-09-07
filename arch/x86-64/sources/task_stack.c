/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/process.h>
#include <ir0/arch_port.h>
#include <ir0/errno.h>
#include <ir0/mm.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/tlb.h>
#include <config.h>

static uint32_t kstack_slot_next;

static uint64_t *kernel_page_root(void)
{
	uintptr_t root = paging_kernel_address_space();

	if (!root)
		root = paging_current_address_space();
	return root ? (uint64_t *)(uintptr_t)root : NULL;
}

int process_kernel_stack_alloc(process_t *p)
{
	uint64_t *pml4;
	uint32_t slot;
	uintptr_t va;
	size_t off;
	size_t mapped = 0;

	if (!p)
		return -EINVAL;
	if (p->kstack_base)
		return 0;
	pml4 = kernel_page_root();
	if (!pml4)
		return -ENOMEM;

	slot = __sync_fetch_and_add(&kstack_slot_next, 1u);
	if (slot >= IR0_KSTACK_MAX_SLOTS)
		return -ENOMEM;
	va = (uintptr_t)IR0_KSTACK_VA_BASE +
	     (uintptr_t)slot * (uintptr_t)IR0_KSTACK_SLOT_SIZE + PAGE_SIZE_4KB;

	for (off = 0; off < (size_t)IR0_PROC_KSTACK_SIZE + PAGE_SIZE_4KB;
	     off += PAGE_SIZE_4KB)
	{
		uintptr_t phys = pmm_alloc_frame();

		if (!phys)
			goto rollback;
		paging_poison_phys_page(phys, IR0_KSTACK_POISON);
		if (map_page_in_directory(pml4, va + off, phys, PAGE_RW) != 0)
		{
			pmm_free_frame(phys);
			goto rollback;
		}
		mapped = off + PAGE_SIZE_4KB;
	}

	{
		uintptr_t saved = paging_current_address_space();

		if (saved != (uint64_t)(uintptr_t)pml4)
			paging_activate_address_space((uintptr_t)pml4);
		tlb_invalidate_all();
		if (saved != (uint64_t)(uintptr_t)pml4)
			paging_activate_address_space(saved);
	}

	{
		process_t *it;
		uint64_t irqf = (uint64_t)irq_save();

		for (it = process_list; it; it = it->next)
		{
			uint64_t *proc_pml4 = process_pgd(it);

			if (proc_pml4 && proc_pml4 != pml4)
				mm_copy_kernel_half(proc_pml4, pml4);
		}
		irq_restore((unsigned long)irqf);
	}

	p->kstack_base = (void *)va;
	p->kstack_top = va + (uint64_t)IR0_PROC_KSTACK_SIZE;
	p->saved_user_rsp = 0;
	return 0;

rollback:
	for (off = 0; off < mapped; off += PAGE_SIZE_4KB)
		(void)unmap_page_in_directory(pml4, va + off);
	return -ENOMEM;
}

void process_kernel_stack_free(process_t *p)
{
	uint64_t *pml4;
	uintptr_t va;
	size_t off;

	if (!p || !p->kstack_base)
		return;
	pml4 = kernel_page_root();
	va = (uintptr_t)p->kstack_base;
	if (pml4)
	{
		size_t span = (size_t)IR0_PROC_KSTACK_SIZE + PAGE_SIZE_4KB;

		for (off = 0; off < span; off += PAGE_SIZE_4KB)
			(void)unmap_page_in_directory(pml4, va + off);
	}
	p->kstack_base = NULL;
	p->kstack_top = 0;
	p->saved_user_rsp = 0;
}
