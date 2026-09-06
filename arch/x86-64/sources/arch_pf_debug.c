/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_pf_debug.c
 * Description: x86-64 #PF register-frame diagnostics (DEBUG_D1_DIAG).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <config.h>
#include <stdint.h>
#include <ir0/arch_pf_debug.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <ir0/abi/mmap_contract.h>
#include <d1_13_malloc_pf_diag.h>

#if DEBUG_D1_DIAG

void pf_debug_stack_adjacent(uint64_t *frame, uint64_t fault_addr,
				 const struct page_fault_info *info,
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

void pf_debug_memmove_fault(uint64_t *frame, uint64_t fault_addr,
				const struct page_fault_info *info,
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

void pf_debug_stack_adjacent(uint64_t *frame, uint64_t fault_addr,
				 const struct page_fault_info *info,
				 process_t *p)
{
	(void)frame;
	(void)fault_addr;
	(void)info;
	(void)p;
}

void pf_debug_memmove_fault(uint64_t *frame, uint64_t fault_addr,
				const struct page_fault_info *info,
				process_t *p)
{
	(void)frame;
	(void)fault_addr;
	(void)info;
	(void)p;
}

#endif
