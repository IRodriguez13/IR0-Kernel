/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_panic_class.c
 * Description: IR0 kernel source — ktm panic class
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#define USER_RIP_LO    0x00400000ULL
#define USER_RIP_HI    0x00007FFFFFFFFFFFULL

/* Mirrors panic_level_t in includes/ir0/oops.h (no include — avoid coupling). */
#define KTM_PL_KERNEL_BUG       0
#define KTM_PL_HARDWARE_FAULT   1
#define KTM_PL_OUT_OF_MEMORY    2
#define KTM_PL_STACK_OVERFLOW   3
#define KTM_PL_ASSERT_FAILED    4
#define KTM_PL_MEM              5

static int rip_in_userspace(uint64_t rip)
{
	return rip >= USER_RIP_LO && rip <= USER_RIP_HI;
}

void ktm_panic_site_emit(const char *file, unsigned int line, const char *caller,
			 const char *message)
{
#if !defined(CONFIG_KTM) || !CONFIG_KTM
	(void)file;
	(void)line;
	(void)caller;
	(void)message;
	return;
#else
	klog_error_fmt("KTM",
		       "[KTM][PANIC_SITE] file=%s line=%x caller=%s msg=%s",
		       file ? file : "?", (unsigned)line, caller ? caller : "?",
		       message ? message : "(null)");
#endif
}

void ktm_classify_user_fault(process_t *proc, uint64_t fault_addr,
			     uint64_t fault_rip, uint64_t fault_cs)
{
	const char *klass = NULL;

#if !defined(CONFIG_KTM) || !CONFIG_KTM
	(void)proc;
	(void)fault_addr;
	(void)fault_rip;
	(void)fault_cs;
	return;
#else
	ktm_ctx_snapshot(proc, "user_page_fault");
	KTM_EVENT(KTM_EV_CTX_SNAPSHOT);

	if ((fault_cs & 3U) == 0U && rip_in_userspace(fault_rip))
		klass = "KERNEL_JUMP_BAD_RIP";
	else if (fault_addr < 0x1000ULL)
		klass = "USER_NULL_DEREF";
	else if (proc && proc->irq_frame_saved && process_wait_status_ptr_peek(proc))
		klass = "FAULT_DURING_WAIT4";
	else if (proc && proc->irq_frame_saved)
		klass = "FAULT_DURING_SYSCALL_BLOCK";

	if (klass)
		ktm_panic_class_emit(klass);
#endif
}

static const char *classify_by_level(int panic_level)
{
	switch (panic_level)
	{
	case KTM_PL_HARDWARE_FAULT:
		return "KERNEL_HW_FAULT";
	case KTM_PL_OUT_OF_MEMORY:
		return "KERNEL_OOM";
	case KTM_PL_STACK_OVERFLOW:
		return "KERNEL_STACK_OVERFLOW";
	case KTM_PL_ASSERT_FAILED:
		return "KERNEL_ASSERT";
	case KTM_PL_MEM:
		return "KERNEL_MEM_FAULT";
	default:
		return NULL;
	}
}

static const char *classify_by_message(const char *message)
{
	const char *klass = "KERNEL_PANIC_UNCLASSIFIED";

	if (!message)
		return klass;

	if (strstr(message, "rip") || strstr(message, "RIP") ||
	    strstr(message, "context") || strstr(message, "iretq"))
		klass = "KERNEL_CONTEXT_CORRUPT";
	else if (strstr(message, "oom") || strstr(message, "OOM") ||
		 strstr(message, "memory"))
		klass = "KERNEL_OOM";
	else if (strstr(message, "assert") || strstr(message, "ASSERT"))
		klass = "KERNEL_ASSERT";
	else if (strstr(message, "Double fault") || strstr(message, "Triple fault") ||
		 strstr(message, "page fault") || strstr(message, "GPF") ||
		 strstr(message, "Divide by zero"))
		klass = "KERNEL_HW_FAULT";

	return klass;
}

void ktm_classify_kernel_panic_ex(const char *message, int panic_level,
				  const char *file, unsigned int line,
				  const char *caller)
{
	const char *klass;

#if !defined(CONFIG_KTM) || !CONFIG_KTM
	(void)message;
	(void)panic_level;
	(void)file;
	(void)line;
	(void)caller;
	return;
#else
	(void)file;
	(void)line;
	(void)caller;

	klass = classify_by_level(panic_level);
	if (!klass)
		klass = classify_by_message(message);

	ktm_panic_class_emit(klass);
#endif
}

void ktm_classify_kernel_panic(const char *message)
{
	ktm_classify_kernel_panic_ex(message, KTM_PL_KERNEL_BUG, NULL, 0, NULL);
}

void ktm_panic_class_emit(const char *klass)
{
	klog_debug_fmt("KTM", "[KTM][PANIC_CLASS] %s", klass ? klass : "UNKNOWN");
}
