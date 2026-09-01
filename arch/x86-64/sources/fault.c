/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fault.c
 * Description: x86-64 CPU exception entry; #PF decode dispatches to mm/page_fault.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <config.h>
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/cpu.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <ir0/arch_page_fault.h>
#include <ktm.h>

void page_fault_handler_x64(uint64_t *stack)
{
	struct page_fault_info info;
	int ret;

	ret = page_fault_decode(&info, stack ? stack[1] : 0, stack);
	if (ret < 0)
		panic("page_fault_decode failed");

	mm_page_fault_handle(&info, stack);
}

void gpf_audit_from_isr(uint64_t *stack)
{
#if !DEBUG_PAGE_FAULTS
	(void)stack;
	return;
#else
	process_t *current = process_get_current();
	uint64_t errcode = stack ? stack[1] : 0;
	uint64_t fault_rip = stack ? stack[2] : 0;
	uint64_t fault_cs = stack ? stack[3] : 0;
	uint64_t fault_rflags = stack ? stack[4] : 0;
	uint64_t fault_rsp = stack ? stack[5] : 0;
	uint64_t fault_ss = stack ? stack[6] : 0;
	int user = (fault_cs & 3U) == 3U;
	extern uint64_t iretq_checkpoint_buf[40];
	uint64_t ckpt_rip = iretq_checkpoint_buf[2];
	uint64_t ckpt_cs = iretq_checkpoint_buf[3];
	uint64_t ckpt_rsp = iretq_checkpoint_buf[5];

	if (ir0_panic_in_progress())
		return;

	klog_debug_fmt("GPF", "err=%llx rip=%llx cs=%llx rsp=%llx ss=%llx rflags=%llx mode=%s pid=%x comm=%s cr3=%llx", (unsigned long long)(errcode), (unsigned long long)(fault_rip), (unsigned long long)(fault_cs), (unsigned long long)(fault_rsp), (unsigned long long)(fault_ss), (unsigned long long)(fault_rflags), user ? "user" : "kernel", (unsigned)(current ? (uint32_t)current->task.pid : 0), current ? current->comm : "(none)", (unsigned long long)(get_current_page_directory()));

	klog_debug_fmt("GPF", "iretq_ckpt rip=%llx cs=%llx rsp=%llx", (unsigned long long)(ckpt_rip), (unsigned long long)(ckpt_cs), (unsigned long long)(ckpt_rsp));

	if (!user)
	{
		klog_debug("GPF", "CLASSIFY GPF_IN_KERNEL_BEFORE_IRET");
		if (fault_rip == ckpt_rip ||
		    (fault_rip >= 0x160000ULL && fault_rip <= 0x170000ULL))
		{
			klog_debug("GPF",
				   "CLASSIFY GPF_DURING_IRETQ note=rip_near_switch_to_user");
		}
	}
	else
	{
		klog_debug("GPF", "CLASSIFY GPF_IN_USERSPACE");
	}
#endif /* DEBUG_PAGE_FAULTS */
}