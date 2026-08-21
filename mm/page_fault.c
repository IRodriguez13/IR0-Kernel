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
#include <ir0/arch_cpu.h>
#include <ir0/process.h>
#include <ir0/pmm.h>
#include <ir0/signals.h>
#include <ir0/copy_user.h>
#include <ir0/ktm/klog.h>
#include <ir0/arch_page_fault.h>
#include <ir0/abi/mmap_contract.h>
#include <ktm.h>
#include <ktm_probe_diag.h>
#include <d1_13_malloc_pf_diag.h>

#define PF_USER_SPACE_START 0x00400000UL
#define PF_USER_SPACE_END   0x00007FFFFFFFFFFFUL

static int pf_addr_in_heap(process_t *p, uint64_t fa)
{
	uint64_t heap_lo;

	if (!p)
		return 0;

	heap_lo = (uint64_t)process_heap_start(p);
	if (heap_lo == 0)
		heap_lo = USER_HEAP_BASE;

	return (fa >= heap_lo && fa < (uint64_t)process_heap_end(p));
}

static int pf_addr_in_stack(process_t *p, uint64_t fa)
{
	if (!p)
		return 0;
	return (fa >= (USER_STACK_TOP - USER_STACK_SIZE) && fa < USER_STACK_TOP);
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

static void pf_user_segv(process_t *p, uint64_t *stack, uint64_t fault_addr,
			 const struct arch_page_fault_info *info);

/*
 * Linux do_anonymous_page: demand-zero a 4 KiB user leaf.
 * Heap, stack, and anonymous mmap VMAs share this path.
 */
static void pf_demand_zero_page(process_t *current, uint64_t fault_addr,
				uint64_t map_flags, uint64_t *stack,
				const struct arch_page_fault_info *info)
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

	memset((void *)(uintptr_t)phys_addr, 0, 0x1000);
}

#if DEBUG_D1_DIAG
static void pf_d110_stack_adjacent_diag(uint64_t *frame, uint64_t fault_addr,
					const struct arch_page_fault_info *info,
					process_t *p)
{
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rip;
	uint64_t rsp;
	uint64_t movsq_end;
	int write_fault;
	int src_touch;
	int dst_touch;
	struct mmap_region *r;
	struct mmap_region *prev_mmap;
	struct mmap_region *next_mmap;
	uint64_t prev_end;
	uint64_t next_start;
	uintptr_t stack_lo;
	uintptr_t stack_hi;
	uintptr_t guard_lo;

	if (!frame || !p || !info || !info->user)
		return;

	stack_lo = (uintptr_t)process_stack_start(p);
	stack_hi = (uintptr_t)(process_stack_start(p) + process_stack_size(p));
	guard_lo = stack_lo - PAGE_SIZE_4KB;

	if (fault_addr < guard_lo - PAGE_SIZE_4KB ||
	    fault_addr >= stack_hi + PAGE_SIZE_4KB)
		return;

	rax = frame[-1];
	rcx = frame[-2];
	rdx = frame[-3];
	rbx = frame[-4];
	rbp = frame[-5];
	rsi = frame[-6];
	rdi = frame[-7];
	rip = frame[2];
	rsp = frame[5];
	write_fault = info->write;

	movsq_end = 0;
	if (rcx > 0)
		movsq_end = (write_fault ? rdi : rsi) + (rcx * 8ULL);

	src_touch = (fault_addr >= (rsi & ~0xFFFULL) &&
		       fault_addr < rsi + (rcx ? rcx * 8ULL : 8ULL));
	dst_touch = (fault_addr >= (rdi & ~0xFFFULL) &&
		     fault_addr < rdi + (rcx ? rcx * 8ULL : 8ULL));

	klog_debug_fmt("KERN", "\n=== [D1.10][PF_STACK_ADJ] ===\n[D1.10][REGS] rip=%llx rsp=%llx rbp=%llx rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx", (unsigned long long)(rip), (unsigned long long)(rsp), (unsigned long long)(rbp), (unsigned long long)(rax), (unsigned long long)(rbx), (unsigned long long)(rcx), (unsigned long long)(rdx), (unsigned long long)(rsi), (unsigned long long)(rdi));

	klog_debug_fmt("KERN", "[D1.10][PF] addr=%llx write=%llx pid=%x comm=%s", (unsigned long long)(fault_addr), (unsigned long long)(write_fault ? 1 : 0), (unsigned)((uint32_t)p->task.pid), p->comm[0] ? p->comm : "(none)");

	if (rip >= 0x4422b0ULL && rip <= 0x442320ULL)
	{
		klog_debug_fmt("KERN", "[D1.10][REP_MOVSQ] len_qwords=%llx len_bytes=%llx src=%llx dst=%llx span_end=%llx", (unsigned long long)(rcx), (unsigned long long)(rcx * 8ULL), (unsigned long long)(rsi), (unsigned long long)(rdi), (unsigned long long)(movsq_end));
	}

	klog_debug_fmt("KERN",
		       "[D1.10][TOUCH] addr_match=%s src_page=%llx dst_page=%llx",
		       write_fault
			   ? (dst_touch ? "destination"
					: (src_touch ? "source_read_unlikely" : "unknown"))
			   : (src_touch ? "source"
					: (dst_touch ? "dest_write_unlikely" : "unknown")),
		       (unsigned long long)(rsi & ~0xFFFULL),
		       (unsigned long long)(rdi & ~0xFFFULL));

	klog_debug_fmt("KERN", "[D1.10][STACK] base=%llx top=%llx guard_below=%llx pages=%llx rsp_free_to_base=%llx heap_end=%llx mmap_base=%llx", (unsigned long long)((uint64_t)stack_lo), (unsigned long long)((uint64_t)stack_hi), (unsigned long long)((uint64_t)guard_lo), (unsigned long long)((uint64_t)(process_stack_size(p) / PAGE_SIZE_4KB)), (unsigned long long)(rsp > stack_lo ? rsp - stack_lo : 0), (unsigned long long)(process_heap_end(p)), (unsigned long long)(process_mmap_base(p)));

	klog_debug_fmt("KERN", "[D1.10][VMA] stack=[%llx,%llx)\n", (unsigned long long)((uint64_t)stack_lo), (unsigned long long)((uint64_t)stack_hi));

	prev_mmap = NULL;
	next_mmap = NULL;
	prev_end = 0;
	next_start = ~0ULL;
	for (r = process_mmap_list(p); r != NULL; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;

		if (end <= fault_addr && end > prev_end)
		{
			prev_end = end;
			prev_mmap = r;
		}
		if (start > fault_addr && start < next_start)
		{
			next_start = start;
			next_mmap = r;
		}
		klog_debug_fmt("KERN", "[D1.10][VMA] mmap=[%llx,%llx) prot=%llx", (unsigned long long)(start), (unsigned long long)(end), (unsigned long long)((uint64_t)r->prot));
	}

	if (process_heap_end(p) > process_heap_start(p))
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] heap=[%llx,%llx)\n", (unsigned long long)(process_heap_start(p)), (unsigned long long)(process_heap_end(p)));
	}

	if (prev_mmap)
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] prev_mmap_end=%llx", (unsigned long long)(prev_end));
	}
	else
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] prev_mmap_end=none gap_from_prev=%llx", (unsigned long long)(fault_addr - USER_MMAP_END));
	}

	if (next_mmap)
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] next_mmap_start=%llx", (unsigned long long)(next_start));
	}
	else
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] next_mmap_start=none gap_to_stack=%llx", (unsigned long long)(stack_lo - fault_addr));
	}

	klog_debug_fmt("KERN", "[D1.10][VMA] guard_gap=[%llx,%llx) unmapped\n=== [D1.10][PF_STACK_ADJ] end ===\n\n", (unsigned long long)((uint64_t)guard_lo), (unsigned long long)((uint64_t)stack_lo));
}

static void pf_d114_memmove_fault_diag(uint64_t *frame, uint64_t fault_addr,
				       const struct arch_page_fault_info *info,
				       process_t *p)
{
	uint64_t rip;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;

	if (!frame || !p || !info || !info->user)
		return;

	rip = frame[2];
	if (rip < 0x4422B0ULL || rip > 0x442320ULL)
		return;

	rcx = frame[-2];
	rdx = frame[-3];
	rsi = frame[-6];
	rdi = frame[-7];
	d1_13_malloc_pf_diag(p, fault_addr, rip, rdi, rsi, rdx, rcx);
}
#else
static void pf_d110_stack_adjacent_diag(uint64_t *frame, uint64_t fault_addr,
					const struct arch_page_fault_info *info,
					process_t *p)
{
	(void)frame;
	(void)fault_addr;
	(void)info;
	(void)p;
}

static void pf_d114_memmove_fault_diag(uint64_t *frame, uint64_t fault_addr,
				       const struct arch_page_fault_info *info,
				       process_t *p)
{
	(void)frame;
	(void)fault_addr;
	(void)info;
	(void)p;
}
#endif

static void pf_audit_classify(uint64_t *stack,
			      const struct arch_page_fault_info *info)
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
			 const struct arch_page_fault_info *info)
{
	if (signals_deliver_from_irq_frame(p, SIGSEGV, stack, fault_addr))
		return;

	ktm_probe_diag_pf(p, fault_addr, info ? info->ip : 0);

	if (!p)
		panic("[PF] userspace fault without process");

	/*
	 * NOTICE (not DEBUG): smoke-mm-cow-lazy greps this on serial; default
	 * klog level drops DEBUG.
	 */
	klog_notice_fmt("PF",
			"[PF] userspace segv pid=%x addr=%llx write=%llx user=%llx handler=%llx proc_mask=%x ignored=%x (no handler)\n",
			(unsigned)((uint32_t)p->task.pid),
			(unsigned long long)fault_addr,
			(unsigned long long)(info && info->write ? 1 : 0),
			(unsigned long long)(info && info->user ? 1 : 0),
			(unsigned long long)((uint64_t)(uintptr_t)p->signal_handlers[SIGSEGV]),
			(unsigned)(p->signal_mask),
			(unsigned)(p->signal_ignored));

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

void mm_page_fault_handle(const struct arch_page_fault_info *info, void *irq_frame)
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
	pf_d110_stack_adjacent_diag(stack, (uint64_t)info->address, info,
				    process_get_current());
	pf_d114_memmove_fault_diag(stack, (uint64_t)info->address, info,
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

		current = process_get_current();
		if (!current || !process_pgd(current))
			return;

		vaddr_aligned = fault_addr & ~0xFFFUL;
		pte = paging_get_pte(process_pgd(current), vaddr_aligned);
		if (!pte || !(*pte & PAGE_PRESENT) || !(*pte & PAGE_USER) ||
		    !(*pte & PAGE_COW) || (*pte & PAGE_RW))
		{
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		entry = *pte;
		old_phys = (uintptr_t)(entry & PAGE_PTE_PFN_MASK);

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

		memcpy((void *)new_phys, (void *)old_phys, 0x1000);

		map_flags = (entry & 0xFFF) | PAGE_USER | PAGE_RW;
		map_flags &= ~(PAGE_COW | PAGE_GLOBAL);
		if (!(entry & PAGE_NX))
			map_flags |= PAGE_EXEC;

		if (map_page_in_directory(process_pgd(current), vaddr_aligned,
					  new_phys, map_flags) != 0)
		{
			pmm_free_frame(new_phys);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}

		pmm_frame_put(old_phys);
		tlb_invalidate_page((uintptr_t)vaddr_aligned);
		return;
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

		klog_debug_fmt("PF",
			       "[PF] kernel_uaccess_fault addr=%llx write=%llx pid=%x",
			       (unsigned long long)fault_addr,
			       (unsigned long long)(write ? 1 : 0),
			       (unsigned)(current ? (uint32_t)current->task.pid : 0));
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
		uint64_t fault_rsp = stack ? stack[5] : (uint64_t)info->sp;
		process_t *cur = process_get_current();

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
	}

	panic("Unhandled kernel page fault");
}
