/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault.c
 * Description: Portable page fault policy (demand paging, anon mmap, COW, SIGSEGV).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <config.h>
#include <ir0/vga.h>
#include <ir0/paging.h>
#include <ir0/oops.h>
#include <ir0/cpu.h>
#include <ir0/cpu.h>
#include <ir0/tlb.h>
#include <ir0/page_fault.h>
#include <ir0/process.h>
#include <ir0/pmm.h>
#include <ir0/signals.h>
#include <ir0/copy_user.h>
#include <ir0/arch_task.h>
#include <ir0/ktm/klog.h>
#include <ir0/ktm/event.h>
#include <ir0/ktm/deferred.h>
#include <ir0/ktm/user_canary.h>
#include <ir0/page_fault.h>
#include <ir0/arch_pf_debug.h>
#include <mm/paging.h>
#include <ir0/abi/mmap_contract.h>
#include <ktm.h>
#include <ktm_probe_diag.h>

#define PF_USER_SPACE_START 0x00400000UL
#define PF_USER_SPACE_END   0x00007FFFFFFFFFFFUL
/* Supervisor 2MiB identity under process CR3 (create_process_page_directory). */
#define PF_IDENTITY_USER_FLOOR 0x00600000UL

static int pf_addr_in_heap(process_t *p, uint64_t fa)
{
	uint64_t heap_lo;
	uint64_t heap_hi;

	if (!p)
		return 0;

	heap_lo = (uint64_t)process_heap_start(p);
	heap_hi = (uint64_t)process_heap_end(p);
	if (heap_hi <= heap_lo)
		return 0;
	/*
	 * heap_start==0 with a live brk end: do NOT snap to USER_HEAP_BASE
	 * (0x20000000) — that excluded the low brk image and skipped identity
	 * promote (write-fault no-COW pte=…063 !USER at ~0x9fb000).
	 */
	if (heap_lo == 0)
		heap_lo = PF_USER_SPACE_START;

	return (fa >= heap_lo && fa < heap_hi);
}

static int pf_addr_in_stack(process_t *p, uint64_t fa)
{
	uint64_t lo;
	uint64_t hi;

	if (!p)
		return 0;

	lo = (uint64_t)process_stack_start(p);
	hi = lo + (uint64_t)process_stack_size(p);
	if (lo == 0 || hi <= lo)
	{
		lo = (uint64_t)USER_STACK_TOP - (uint64_t)USER_STACK_SIZE;
		hi = (uint64_t)USER_STACK_TOP;
	}
	return (fa >= lo && fa < hi);
}

static struct mmap_region *pf_mmap_region_for(process_t *p, uint64_t fa)
{
	struct mmap_region *r;

	if (!p)
		return NULL;
	for (r = process_mmap_list(p); r != NULL; r = r->next)
	{
		uintptr_t base = (uintptr_t)r->addr;
		uint64_t end = base + (uint64_t)r->length;

		if (fa >= (uint64_t)base && fa < end)
			return r;
	}
	return NULL;
}

/*
 * Linux do_anonymous_page only inside a VMA. IR0 also sees present+!USER
 * identity leaves after a 2MiB PDE split (sibling slots stay PA==VA).
 */
static int pf_addr_may_promote_identity(process_t *p, uint64_t fa)
{
	uint64_t heap_hi;

	if (!p)
		return 0;
	if (pf_addr_in_heap(p, fa) || pf_addr_in_stack(p, fa) ||
	    pf_mmap_region_for(p, fa) != NULL)
		return 1;
	/*
	 * Brk cursor may lag heap_start bookkeeping; identity below heap_end
	 * in the supervisor window is still a heap hole (Linux pte_none).
	 */
	heap_hi = (uint64_t)process_heap_end(p);
	if (heap_hi > PF_IDENTITY_USER_FLOOR &&
	    fa >= PF_IDENTITY_USER_FLOOR && fa < heap_hi &&
	    fa < (uint64_t)USER_MMAP_START)
		return 1;
	return 0;
}

static void pf_user_segv(process_t *p, uint64_t *stack, uint64_t fault_addr,
			 const struct page_fault_info *info);

/*
 * Linux do_anonymous_page (mm/memory.c): install a private zeroed USER leaf.
 * Heap, stack, and anonymous mmap VMAs share this path for !present faults.
 */
static void pf_demand_zero_page(process_t *current, uint64_t fault_addr,
				uint64_t map_flags, uint64_t *stack,
				const struct page_fault_info *info)
{
	uintptr_t phys_addr;
	uint64_t vaddr_aligned;

	phys_addr = pmm_alloc_frame();
	if (phys_addr == 0)
	{
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	vaddr_aligned = fault_addr & ~0xFFFUL;
	if (map_page_in_directory(process_pgd(current), vaddr_aligned,
				  phys_addr, map_flags) != 0)
	{
		pmm_free_frame(phys_addr);
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	paging_zero_phys_page(phys_addr);
}

/*
 * IR0 artifact: process CR3 keeps supervisor 2MiB identity for PMM access.
 * After fork, heap/stack holes remain present+!USER (Linux would be pte_none).
 * Treat as do_anonymous_page — never memcpy identity (PMM garbage → musl abort).
 * Refs: Linux do_anonymous_page; Gorman ch.4 demand paging.
 *
 * Returns 0 on success, -1 on OOM / map failure (caller delivers SEGV).
 */
static int pf_identity_to_anon_zero(process_t *current, uint64_t vaddr_aligned,
				    uint64_t map_flags)
{
	uintptr_t new_phys;

	new_phys = pmm_alloc_frame();
	if (!new_phys)
		return -1;
	paging_zero_phys_page(new_phys);
	if (map_page_in_directory(process_pgd(current), vaddr_aligned, new_phys,
				  map_flags) != 0)
	{
		pmm_free_frame(new_phys);
		return -1;
	}
	tlb_invalidate_page((uintptr_t)vaddr_aligned);
	return 0;
}


static void pf_audit_classify(uint64_t *stack,
			      const struct page_fault_info *info)
{
#if !DEBUG_PAGE_FAULTS
	(void)stack;
	(void)info;
	return;
#else
	process_t *current = process_get_current();
	uint64_t fault_addr;
	int not_present;
	uint64_t fault_rip;
	uint64_t fault_cs;
	uint64_t fault_rsp;
	uint64_t *pte = NULL;
	uint64_t pte_flags = 0;
	int mapped = 0;
	int in_vma = 0;
	int in_userspace_range = 0;

	if (!info || ir0_panic_in_progress())
		return;

	fault_addr = (uint64_t)info->address;
	not_present = !info->present;
	fault_rip = stack ? stack[2] : (uint64_t)info->ip;
	fault_cs = stack ? stack[3] : 0;
	fault_rsp = stack ? stack[5] : (uint64_t)info->sp;

	if (current && process_pgd(current))
	{
		mapped = is_page_mapped_in_directory(process_pgd(current),
						   fault_addr, &pte_flags);
		pte = paging_get_pte(process_pgd(current),
				     (uintptr_t)(fault_addr & ~0xFFFULL));
	}

	in_vma = pf_addr_in_heap(current, fault_addr) ||
		 pf_addr_in_stack(current, fault_addr) ||
		 (pf_mmap_region_for(current, fault_addr) != NULL);
	in_userspace_range = (fault_addr >= PF_USER_SPACE_START &&
			      fault_addr <= PF_USER_SPACE_END);

	klog_debug_fmt("PF", "[PF_AUDIT][FAULT] addr=%llx present=%llx write=%llx user=%llx reserved=%llx insn_fetch=%llx rip=%llx cs=%llx rsp=%llx mode=%s pid=%x comm=%s", (unsigned long long)(fault_addr), (unsigned long long)(info->present ? 1 : 0), (unsigned long long)(info->write ? 1 : 0), (unsigned long long)(info->user ? 1 : 0), (unsigned long long)(info->reserved ? 1 : 0), (unsigned long long)(info->exec ? 1 : 0), (unsigned long long)(fault_rip), (unsigned long long)(fault_cs), (unsigned long long)(fault_rsp), info->user ? "user" : "kernel", (unsigned)(current ? (uint32_t)current->task.pid : 0), current ? current->comm : "(none)");

	klog_debug_fmt("PF", "[PF_AUDIT][VMA] in_allowed_vma=%llx in_heap=%llx in_stack=%llx in_mmap=%llx pte_present=%llx pte_user=%llx pte_rw=%llx pte_nx=%llx", (unsigned long long)(in_vma ? 1 : 0), (unsigned long long)(pf_addr_in_heap(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_addr_in_stack(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_mmap_region_for(current, fault_addr) != NULL ? 1 : 0), (unsigned long long)((mapped > 0 && pte && (*pte & PAGE_PRESENT)) ? 1 : 0), (unsigned long long)(pte_flags & PAGE_USER ? 1 : 0), (unsigned long long)(pte_flags & PAGE_RW ? 1 : 0), (unsigned long long)(pte && (*pte & PAGE_NX) ? 1 : 0));

	if (!info->user && in_userspace_range)
		klog_debug("PF", "CLASSIFY KERNEL_DEREF_USERPTR addr_in_userspace=1");

	if (info->user && not_present && in_vma && mapped <= 0)
		klog_debug("PF", "CLASSIFY PF_ADDR_IN_VMA_NOT_MAPPED");
	else if (info->user && not_present && !in_vma)
		klog_debug("PF", "CLASSIFY PF_ADDR_NOT_IN_VMA");
	else if (info->user && not_present && in_vma)
		klog_debug("PF", "CLASSIFY USER_PF_SHOULD_BE_HANDLED");

	if (!info->user && fault_rip != 0)
		klog_debug_fmt("PF", "CLASSIFY kernel_fault_rip=%llx", (unsigned long long)(fault_rip));
#endif /* DEBUG_PAGE_FAULTS */
}

static void pf_user_segv(process_t *p, uint64_t *stack, uint64_t fault_addr,
			 const struct page_fault_info *info)
{
	/*
	 * Always emit before delivery/kill so session smokes see the fault
	 * site even when a handler runs or console only prints
	 * CONSOLE_SESSION_SEGV.
	 */
	ir0_log_user_fault_frame(
		14, (unsigned long long)fault_addr,
		stack ? (unsigned long long)stack[1] : 0ULL,
		info ? (unsigned long long)info->ip
		     : (stack ? (unsigned long long)stack[2] : 0ULL),
		stack ? (unsigned long long)stack[3] : 0ULL,
		info ? (unsigned long long)info->sp
		     : (stack ? (unsigned long long)stack[5] : 0ULL),
		info ? info->present : 0, info ? info->write : 0,
		info ? info->exec : 0,
		p ? (unsigned)((uint32_t)p->task.pid) : 0,
		p ? p->comm : "(none)");

	if (signals_deliver_from_irq_frame(p, SIGSEGV, stack, fault_addr))
		return;

	ktm_probe_diag_pf(p, fault_addr, info ? info->ip : 0);

	if (!p)
		panic("[PF] userspace fault without process");

	/*
	 * Unhandled segv: the process is already dead, so the ring is worth
	 * more on serial than in memory. Dumping here means every smoke gets
	 * the history leading to a crash without wiring the ioctl itself —
	 * the host-side harness only reads serial and cannot ask for it.
	 * Reached only when no SIGSEGV handler took the fault above.
	 *
	 * MM belongs in the mask: the deferred page-fault rows are the fault
	 * history for this crash, and filtering to IPC|SCHED dropped exactly
	 * them, leaving scheduler churn and no faults.
	 */
	/*
	 * Name the failure when the address sits in the page just above the
	 * stack: "segv at 7ffff000" reads as a wild pointer, but it is the
	 * first address past USER_STACK_TOP and means something walked off the
	 * top of the initial argv/envp image.
	 */
	if (fault_addr >= (uint64_t)USER_STACK_TOP &&
	    fault_addr < (uint64_t)USER_STACK_TOP + PAGE_SIZE_4KB)
	{
		uint64_t sp_words[4] = { 0, 0, 0, 0 };

		klog_notice_fmt("PF",
				"[PF] STACK_TOP_OVERRUN pid=%x addr=%llx rip=%llx write=%llx off=%llx\n",
				(unsigned)((uint32_t)p->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)(info ? (unsigned long long)info->ip : 0ULL),
				(unsigned long long)(info && info->write ? 1 : 0),
				(unsigned long long)(fault_addr - (uint64_t)USER_STACK_TOP));

		/*
		 * The fault RIP lands inside the libc block copy, so it names
		 * memcpy and not whoever asked for the bad length. Snapshot the
		 * top of the user stack instead: musl's x86-64 memcpy pushes
		 * nothing, so the first word is the caller's return address.
		 *
		 * That last part is a SysV assumption. On link-register ISAs the
		 * caller lives in a register and this only shows stack words;
		 * recovering the caller there needs an arch facade for the GPR
		 * set, which does not exist yet.
		 */
		if (info && info->sp &&
		    copy_from_user_region_in_directory(process_pgd(p),
						       (uintptr_t)info->sp,
						       sp_words,
						       sizeof(sp_words)) == 0)
			klog_notice_fmt("PF",
					"[PF] STACK_TOP_OVERRUN usp=%llx w0=%llx w1=%llx w2=%llx w3=%llx\n",
					(unsigned long long)info->sp,
					(unsigned long long)sp_words[0],
					(unsigned long long)sp_words[1],
					(unsigned long long)sp_words[2],
					(unsigned long long)sp_words[3]);
	}

	ktm_user_canary_check(process_pgd(p), (uint64_t)USER_STACK_TOP,
			      (uint32_t)p->task.pid, "segv");

	ktm_event_ring_dump(48, (1u << KTM_SUBSYS_IPC) | (1u << KTM_SUBSYS_SCHED) |
				(1u << KTM_SUBSYS_MM));

	/*
	 * NOTICE (not DEBUG): smoke-mm-cow-lazy greps this on serial; default
	 * klog level drops DEBUG.
	 */
	klog_notice_fmt("PF",
			"[PF] userspace segv pid=%x addr=%llx write=%llx present=%llx rip=%llx user=%llx handler=%llx proc_mask=%x ignored=%x (no handler)\n",
			(unsigned)((uint32_t)p->task.pid),
			(unsigned long long)fault_addr,
			(unsigned long long)(info && info->write ? 1 : 0),
			(unsigned long long)(info && info->present ? 1 : 0),
			(unsigned long long)(info ? (unsigned long long)info->ip : 0ULL),
			(unsigned long long)(info && info->user ? 1 : 0),
			(unsigned long long)((uint64_t)(uintptr_t)p->signal_handlers[SIGSEGV]),
			(unsigned)(p->signal_mask),
			(unsigned)(p->signal_ignored));
	klog_notice_fmt("PF",
			"[PF] segv diag pid=%x rdi=%llx rsi=%llx rsp=%llx "
			"sigframe_sp=%llx mask_saved=%x enter_pend=%x "
			"saved_ctx=%x\n",
			(unsigned)((uint32_t)p->task.pid),
			(unsigned long long)task_get_rdi(&p->task),
			(unsigned long long)task_get_rsi(&p->task),
			(unsigned long long)(info ? info->sp : 0ULL),
			(unsigned long long)p->signal_frame_sp,
			(unsigned)(p->signal_mask_saved_valid ? 1U : 0U),
			(unsigned)(process_signal_enter_pending(p) ? 1U : 0U),
			(unsigned)(process_saved_context_present(p) ? 1U : 0U));

	/*
	 * A user fault with P=1 is a permission violation, not a missing page,
	 * so the PTE flags say which bit denied it (U/S clear => a supervisor
	 * mapping is covering user VA). Without this the log could not tell a
	 * leftover kernel mapping from an honest unmapped access.
	 */
	{
		uint64_t pte_flags = 0;
		int mapped = is_page_mapped_in_directory(process_pgd(p),
							 (uint64_t)fault_addr,
							 &pte_flags);

		klog_notice_fmt("PF",
				"[PF] segv pte pid=%x addr=%llx mapped=%llx flags=%llx user=%llx rw=%llx\n",
				(unsigned)((uint32_t)p->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)(uint64_t)mapped,
				(unsigned long long)pte_flags,
				(unsigned long long)((pte_flags & PAGE_USER) ? 1ULL : 0ULL),
				(unsigned long long)((pte_flags & PAGE_RW) ? 1ULL : 0ULL));
	}

	/* Locate the fault against the address space: heap vs mmap vs neither. */
	{
		struct mmap_region *mr = pf_mmap_region_for(p, fault_addr);

		/*
		 * Split across two records: klog_notice_fmt drops arguments
		 * past the eighth, which silently corrupted the tail fields.
		 */
		klog_notice_fmt("PF",
				"[PF] segv vma pid=%x addr=%llx heap=[%llx,%llx) stack=%llx\n",
				(unsigned)((uint32_t)p->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)(uint64_t)process_heap_start(p),
				(unsigned long long)(uint64_t)process_heap_end(p),
				(unsigned long long)(pf_addr_in_stack(p, fault_addr) ? 1ULL : 0ULL));
		if (mr)
			klog_notice_fmt("PF",
					"[PF] segv vma rgn pid=%x vma=%llx len=%llx prot=%llx flags=%llx hint=%llx\n",
					(unsigned)((uint32_t)p->task.pid),
					(unsigned long long)(uint64_t)(uintptr_t)mr->addr,
					(unsigned long long)(uint64_t)mr->length,
					(unsigned long long)(uint64_t)mr->prot,
					(unsigned long long)(uint64_t)mr->flags,
					(unsigned long long)(uint64_t)(uintptr_t)mr->hint_addr);
	}

	/*
	 * Linux wait status must be WIFSIGNALED(SIGSEGV), not exited(139).
	 * Ash prints "Segmentation fault" only when exit_signal is set.
	 */
	if (!process_signal_default_kill(p, SIGSEGV))
	{
		p->exit_signal = SIGSEGV;
		process_exit(0);
	}
}

void mm_page_fault_handle(const struct page_fault_info *info, void *irq_frame)
{
	uint64_t *stack = (uint64_t *)irq_frame;
	uint64_t fault_addr;
	process_t *current;
	int not_present;
	int write;
	int user;
	int insn_fetch;

	if (!info)
		return;

	pf_audit_classify(stack, info);
	pf_debug_stack_adjacent(stack, (uint64_t)info->address, info,
				process_get_current());
	pf_debug_memmove_fault(stack, (uint64_t)info->address, info,
			       process_get_current());

	if (ir0_panic_in_progress())
	{
		cpu_relax();
		return;
	}

	fault_addr = (uint64_t)info->address;
	not_present = !info->present;
	write = info->write;
	user = info->user;
	insn_fetch = info->exec;

	/*
	 * Recorded, not emitted: this runs on every demand-paging fault, so
	 * formatting or event emission here would dominate the path it
	 * measures. The rows only reach the ring on a dump, which is what
	 * turns a later crash into a fault history instead of a single
	 * address.
	 */
	{
		process_t *pf_cur = process_get_current();

		ktm_deferred_record(KTM_DEFERRED_PAGE_FAULT,
				    pf_cur ? (uint32_t)pf_cur->task.pid : 0,
				    fault_addr,
				    (uint64_t)(unsigned)((not_present ? 1u : 0u) |
							 (write ? 2u : 0u) |
							 (user ? 4u : 0u) |
							 (insn_fetch ? 8u : 0u)),
				    (uint64_t)info->ip);
	}

	if (user && not_present)
	{
		if (fault_addr < PF_USER_SPACE_START || fault_addr > PF_USER_SPACE_END)
		{
			current = process_get_current();
			if (current)
				pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		current = process_get_current();
		if (!current || !process_pgd(current))
			return;

		{
			struct mmap_region *mr;

			mr = pf_mmap_region_for(current, fault_addr);
			if (mr != NULL)
			{
				if ((mr->flags & IR0_MAP_ANONYMOUS) == 0)
				{
					pf_user_segv(current, stack, fault_addr, info);
					return;
				}
				if (mr->prot == 0)
				{
					pf_user_segv(current, stack, fault_addr, info);
					return;
				}
				if (write && (mr->prot & 0x2) == 0) /* PROT_WRITE */
				{
					pf_user_segv(current, stack, fault_addr, info);
					return;
				}
				if (insn_fetch && (mr->prot & 0x4) == 0) /* PROT_EXEC */
				{
					pf_user_segv(current, stack, fault_addr, info);
					return;
				}
				{
					uint64_t map_flags = PAGE_USER;

					if (mr->prot & 0x2) /* PROT_WRITE */
						map_flags |= PAGE_RW;
					if (mr->prot & 0x4) /* PROT_EXEC */
						map_flags |= PAGE_EXEC;
					pf_demand_zero_page(current, fault_addr, map_flags,
							    stack, info);
				}
				return;
			}
		}

		if (!pf_addr_in_heap(current, fault_addr) &&
		    !pf_addr_in_stack(current, fault_addr))
		{
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		{
			uint64_t map_flags = PAGE_USER | PAGE_RW;

			if (insn_fetch)
				map_flags |= PAGE_EXEC;
			pf_demand_zero_page(current, fault_addr, map_flags, stack, info);
		}
		return;
	}

	if (user && !not_present && write)
	{
		uint64_t *pte;
		uint64_t entry;
		uintptr_t old_phys;
		uintptr_t new_phys;
		uint64_t vaddr_aligned;
		uint64_t map_flags;
		unsigned long irq_flags;
		unsigned pinned = 0;

		current = process_get_current();
		if (!current || !process_pgd(current))
			return;

		vaddr_aligned = fault_addr & ~0xFFFUL;
		/*
		 * IRQ-off + optional PMM pin: sibling pipe-stage COW must not
		 * free the shared frame under memcpy (ash pipelines).
		 */
		irq_flags = irq_save();
		/*
		 * Write into an unsplit 2MiB PDE: paging_get_pte returns NULL
		 * (logged as pte=0). Split first; COW still requires USER|COW.
		 */
		if (paging_ensure_4k_leaf(process_pgd(current),
					  vaddr_aligned) != 0)
		{
			irq_restore(irq_flags);
			klog_notice_fmt("PF",
					"[PF] write-fault no-COW pid=%x addr=%llx pte=0\n",
					(unsigned)((uint32_t)current->task.pid),
					(unsigned long long)fault_addr);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
		pte = paging_get_pte(process_pgd(current), vaddr_aligned);
		/*
		 * After huge-PDE break, slots are pte_none (Linux). Demand-zero
		 * inside heap/stack/mmap — same as !present path above.
		 */
		if (!pte || !(*pte & PAGE_PRESENT))
		{
			irq_restore(irq_flags);
			if (pf_addr_may_promote_identity(current, fault_addr))
			{
				uint64_t map_flags = PAGE_USER | PAGE_RW;

				if (insn_fetch)
					map_flags |= PAGE_EXEC;
				pf_demand_zero_page(current, fault_addr, map_flags,
						    stack, info);
				return;
			}
			klog_notice_fmt("PF",
					"[PF] write-fault no-COW pid=%x addr=%llx pte=0\n",
					(unsigned)((uint32_t)current->task.pid),
					(unsigned long long)fault_addr);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
		/*
		 * present+!USER identity leftover: install anon zero
		 * (do_anonymous_page), never memcpy PMM.
		 */
		if (pte && (*pte & PAGE_PRESENT) && !(*pte & PAGE_USER) &&
		    pf_addr_may_promote_identity(current, fault_addr))
		{
			if (pf_identity_to_anon_zero(current, vaddr_aligned,
						     PAGE_USER | PAGE_RW) != 0)
			{
				irq_restore(irq_flags);
				pf_user_segv(current, stack, fault_addr, info);
				return;
			}
			irq_restore(irq_flags);
			return;
		}
		if (!pte || !(*pte & PAGE_PRESENT) || !(*pte & PAGE_USER) ||
		    !(*pte & PAGE_COW) || (*pte & PAGE_RW))
		{
			uint64_t pte_val = pte ? *pte : 0;

			irq_restore(irq_flags);
			klog_notice_fmt("PF",
					"[PF] write-fault no-COW pid=%x addr=%llx pte=%llx\n",
					(unsigned)((uint32_t)current->task.pid),
					(unsigned long long)fault_addr,
					(unsigned long long)pte_val);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		entry = *pte;
		old_phys = (uintptr_t)(entry & PAGE_PTE_PFN_MASK);
		if (pmm_frame_refcount(old_phys) > 0)
		{
			pmm_frame_get(old_phys);
			pinned = 1;
		}

		/*
		 * Always copy on PAGE_COW — never promote the shared frame in
		 * place. A undercounted pmm_frame_get after fork (or a raced
		 * sibling) would otherwise leave the parent mapped RO+COW to
		 * the same phys while the child writes through a RW PTE,
		 * corrupting the parent's .data/.bss (seen as RIP/addr=0 SEGV
		 * in multi-fork ash/pipeline smokes).
		 */
		new_phys = pmm_alloc_frame();
		if (!new_phys)
		{
			size_t tot = 0;
			size_t used = 0;
			size_t free_fr = 0;

			if (pinned)
				pmm_frame_put(old_phys);
			irq_restore(irq_flags);
			pmm_stats(&tot, &used, &free_fr);
			klog_notice_fmt("PF",
					"[PF] COW OOM pid=%x addr=%llx refs=%u used=%u free=%u\n",
					(unsigned)((uint32_t)current->task.pid),
					(unsigned long long)fault_addr,
					(unsigned)pmm_frame_refcount(old_phys),
					(unsigned)used,
					(unsigned)free_fr);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		paging_copy_phys_page(new_phys, old_phys);

		map_flags = (entry & 0xFFF) | PAGE_USER | PAGE_RW;
		map_flags &= ~(PAGE_COW | PAGE_GLOBAL);
		if (!(entry & PAGE_NX))
			map_flags |= PAGE_EXEC;

		if (map_page_in_directory(process_pgd(current), vaddr_aligned,
					  new_phys, map_flags) != 0)
		{
			pmm_free_frame(new_phys);
			if (pinned)
				pmm_frame_put(old_phys);
			irq_restore(irq_flags);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		if (pinned)
			pmm_frame_put(old_phys);
		pmm_frame_put(old_phys);
		tlb_invalidate_page((uintptr_t)vaddr_aligned);
		irq_restore(irq_flags);
		return;
	}

	/*
	 * Read fault on present supervisor leaf in a user VMA — same as write:
	 * Linux do_anonymous_page (zero), not memcpy of identity PMM.
	 */
	if (user && !not_present && !write)
	{
		uint64_t *pte;
		uint64_t vaddr_aligned;
		unsigned long irq_flags;

		current = process_get_current();
		if (!current || !process_pgd(current))
			return;

		vaddr_aligned = fault_addr & ~0xFFFUL;
		irq_flags = irq_save();
		if (paging_ensure_4k_leaf(process_pgd(current),
					  vaddr_aligned) != 0)
		{
			irq_restore(irq_flags);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
		pte = paging_get_pte(process_pgd(current), vaddr_aligned);
		if (!pte || !(*pte & PAGE_PRESENT))
		{
			irq_restore(irq_flags);
			if (pf_addr_may_promote_identity(current, fault_addr))
			{
				pf_demand_zero_page(current, fault_addr,
						    PAGE_USER | PAGE_RW, stack,
						    info);
				return;
			}
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
		if (pte && (*pte & PAGE_PRESENT) && !(*pte & PAGE_USER) &&
		    pf_addr_may_promote_identity(current, fault_addr))
		{
			if (pf_identity_to_anon_zero(current, vaddr_aligned,
						     PAGE_USER | PAGE_RW) != 0)
			{
				irq_restore(irq_flags);
				pf_user_segv(current, stack, fault_addr, info);
				return;
			}
			irq_restore(irq_flags);
			return;
		}
		irq_restore(irq_flags);
	}

	if (user)
	{
		current = process_get_current();
		if (current)
			pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	if (ir0_panic_in_progress())
	{
		cpu_relax();
		return;
	}

	if (fault_addr >= PF_USER_SPACE_START && fault_addr <= PF_USER_SPACE_END)
	{
		current = process_get_current();

		/*
		 * Kernel uaccess #PF while serving a user task: kill the task
		 * with SIGSEGV. Must log USER_FAULT_FRAME (INFO) — DEBUG was
		 * invisible under the default klog profile and session smokes
		 * only saw CONSOLE_SESSION_SEGV with no fault site.
		 */
		ir0_log_user_fault_frame(
			14, (unsigned long long)fault_addr,
			stack ? (unsigned long long)stack[1] : 0ULL,
			stack ? (unsigned long long)stack[2]
			      : (unsigned long long)info->ip,
			stack ? (unsigned long long)stack[3] : 0ULL,
			stack ? (unsigned long long)stack[5]
			      : (unsigned long long)info->sp,
			info->present, info->write, info->exec,
			current ? (unsigned)((uint32_t)current->task.pid) : 0,
			current ? current->comm : "(none)");
		klog_info_fmt("PF",
			      "CLASSIFY KERNEL_UACCESS_FAULT addr=%llx write=%x "
			      "pid=%x rip=%llx",
			      (unsigned long long)fault_addr,
			      (unsigned)(write ? 1 : 0),
			      (unsigned)(current ? (uint32_t)current->task.pid : 0),
			      (unsigned long long)(stack ? stack[2]
							: (uint64_t)info->ip));
		if (current && current->mode == USER_MODE)
		{
			if (!process_signal_default_kill(current, SIGSEGV))
			{
				current->exit_signal = SIGSEGV;
				process_exit(0);
			}
		}
		panic("Unhandled kernel page fault (uaccess, no user task)");
	}

	{
		uint64_t fault_rip = stack ? stack[2] : (uint64_t)info->ip;
		uint64_t fault_cs = stack ? stack[3] : 0;
		uint64_t fault_rflags = stack ? stack[4] : 0;
		uint64_t fault_rsp = stack ? stack[5] : (uint64_t)info->sp;
		uint64_t fault_ss = stack ? stack[6] : 0;
		process_t *cur = process_get_current();
		int stack_overflow = 0;

		print("[PF] Kernel page fault addr=");
		print_hex64(fault_addr);
		print(" write=");
		print_hex((uintptr_t)write);
		print(" user=");
		print_hex((uintptr_t)user);
		print(" present=");
		print_hex((uintptr_t)info->present);
		print(" rip=");
		print_hex64(fault_rip);
		print(" cs=");
		print_hex64(fault_cs);
		print(" rsp=");
		print_hex64(fault_rsp);
		print(" np=");
		print_hex((uintptr_t)not_present);
		print(" pid=");
		print_hex((uintptr_t)(cur ? (uint32_t)cur->task.pid : 0));
		print("\n");
		klog_error_fmt("PF",
			       "kernel_pf addr=%llx write=%llx user=%llx rip=%llx cs=%llx rsp=%llx pid=%x",
			       (unsigned long long)fault_addr,
			       (unsigned long long)(write ? 1 : 0),
			       (unsigned long long)(user ? 1 : 0),
			       (unsigned long long)fault_rip,
			       (unsigned long long)fault_cs,
			       (unsigned long long)fault_rsp,
			       (unsigned)(cur ? (uint32_t)cur->task.pid : 0));
		if (fault_addr >= (uint64_t)(intptr_t)-4095 &&
		    fault_addr <= (uint64_t)(intptr_t)-1)
		{
			print("[PF] addr looks like ERR_PTR(-errno) errno=");
			print_hex((uintptr_t)(-(intptr_t)fault_addr));
			print("\n");
		}

		if (fault_rsp >= IR0_KSTACK_VA_BASE)
		{
			uint64_t off = fault_rsp - IR0_KSTACK_VA_BASE;
			uint64_t in_slot = off % (uint64_t)IR0_KSTACK_SLOT_SIZE;

			if (in_slot < (uint64_t)PAGE_SIZE_4KB)
				stack_overflow = 1;
		}

		/*
		 * Seed FAULT FRAME before panic() so a nested #DF during the
		 * panic dump still leaves the primary #PF site in the banner.
		 */
		panic_note_exception_frame(
			14, stack ? stack[1] : 0, fault_rip, fault_cs,
			fault_rflags, fault_rsp, fault_ss, stack_overflow,
			cur ? (uint32_t)cur->task.pid : 0,
			cur ? cur->comm : "(none)");
	}

	panic("Unhandled kernel page fault");
}
