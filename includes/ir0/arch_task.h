/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_task.h
 * Description: Portable per-task arch context accessors and init helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#if defined(ARCH_ARM64) || defined(__aarch64__)
#include <ir0/arch_task_context_arm64.h>
#else
#include <ir0/arch_task_context_x86_64.h>
#endif

/* Defined in task.h; accessors accept incomplete type via pointer. */
typedef struct task task_t;

static inline void task_context_init(arch_task_context_t *ctx)
{
	if (!ctx)
		return;

	*ctx = (arch_task_context_t){0};
}

static inline void task_context_clone(arch_task_context_t *dst,
					   const arch_task_context_t *src)
{
	if (!dst || !src)
		return;

	*dst = *src;
}

#if defined(ARCH_ARM64) || defined(__aarch64__)

static inline uint64_t task_mm_root(const task_t *t)
{
	return t ? t->arch.ttbr0_el1 : 0;
}

static inline void task_set_mm_root(task_t *t, uint64_t root)
{
	if (t)
		t->arch.ttbr0_el1 = root;
}

static inline uint64_t task_get_ip(const task_t *t)
{
	return t ? t->arch.elr_el1 : 0;
}

static inline void task_set_ip(task_t *t, uint64_t ip)
{
	if (t)
		t->arch.elr_el1 = ip;
}

static inline uint64_t task_get_sp(const task_t *t)
{
	return t ? t->arch.sp_el0 : 0;
}

static inline void task_set_sp(task_t *t, uint64_t sp)
{
	if (t)
		t->arch.sp_el0 = sp;
}

static inline uint64_t task_get_retval(const task_t *t)
{
	return t ? t->arch.x0 : 0;
}

static inline void task_set_retval(task_t *t, uint64_t val)
{
	if (t)
		t->arch.x0 = val;
}

static inline uint64_t task_get_flags(const task_t *t)
{
	return t ? t->arch.spsr_el1 : 0;
}

static inline void task_set_flags(task_t *t, uint64_t flags)
{
	if (t)
		t->arch.spsr_el1 = flags;
}

static inline void task_set_user_return(task_t *task, uint64_t ip,
					     uint64_t sp, uint64_t flags)
{
	if (!task)
		return;

	task_set_ip(task, ip);
	task_set_sp(task, sp);
	task_set_flags(task, flags);
}

static inline void task_prepare_fork_child(arch_task_context_t *child,
						const arch_task_context_t *parent)
{
	if (!child || !parent)
		return;

	task_context_clone(child, parent);
	child->x0 = 0;
}

static inline uint16_t task_get_cs(const task_t *t)
{
	(void)t;
	return 0;
}

static inline void task_set_cs(task_t *t, uint16_t cs)
{
	(void)t;
	(void)cs;
}

static inline uint16_t task_get_ss(const task_t *t)
{
	(void)t;
	return 0;
}

static inline void task_set_ss(task_t *t, uint16_t ss)
{
	(void)t;
	(void)ss;
}

static inline void task_clear_frame_pointer(task_t *t)
{
	if (t)
		t->arch.x29 = 0;
}

static inline void task_set_frame_pointer(task_t *t, uint64_t fp)
{
	if (t)
		t->arch.x29 = fp;
}

static inline uint64_t task_get_rdi(const task_t *t)
{
	return t ? t->arch.x0 : 0;
}

static inline void task_set_rdi(task_t *t, uint64_t val)
{
	if (t)
		t->arch.x0 = val;
}

static inline uint64_t task_get_rsi(const task_t *t)
{
	return t ? t->arch.x1 : 0;
}

static inline void task_set_rsi(task_t *t, uint64_t val)
{
	if (t)
		t->arch.x1 = val;
}

static inline uint64_t task_get_rdx(const task_t *t)
{
	return t ? t->arch.x2 : 0;
}

static inline void task_set_rdx(task_t *t, uint64_t val)
{
	if (t)
		t->arch.x2 = val;
}

/* Portable syscall/arg accessors (§3) — prefer these over rdi/rsi/rdx names. */
static inline uint64_t task_get_arg0(const task_t *t)
{
	return task_get_rdi(t);
}

static inline void task_set_arg0(task_t *t, uint64_t val)
{
	task_set_rdi(t, val);
}

static inline uint64_t task_get_arg1(const task_t *t)
{
	return task_get_rsi(t);
}

static inline void task_set_arg1(task_t *t, uint64_t val)
{
	task_set_rsi(t, val);
}

static inline uint64_t task_get_arg2(const task_t *t)
{
	return task_get_rdx(t);
}

static inline void task_set_arg2(task_t *t, uint64_t val)
{
	task_set_rdx(t, val);
}

static inline void task_set_user_segments(task_t *t)
{
	(void)t;
}

static inline int task_cs_is_user(const task_t *t)
{
	(void)t;
	return 1;
}

static inline void task_set_kernel_segments(task_t *t)
{
	(void)t;
}

#else

static inline uint64_t task_mm_root(const task_t *t)
{
	return t ? t->arch.cr3 : 0;
}

static inline void task_set_mm_root(task_t *t, uint64_t root)
{
	if (t)
		t->arch.cr3 = root;
}

static inline uint64_t task_get_ip(const task_t *t)
{
	return t ? t->arch.rip : 0;
}

static inline void task_set_ip(task_t *t, uint64_t ip)
{
	if (t)
		t->arch.rip = ip;
}

static inline uint64_t task_get_sp(const task_t *t)
{
	return t ? t->arch.rsp : 0;
}

static inline void task_set_sp(task_t *t, uint64_t sp)
{
	if (t)
		t->arch.rsp = sp;
}

static inline uint64_t task_get_retval(const task_t *t)
{
	return t ? t->arch.rax : 0;
}

static inline void task_set_retval(task_t *t, uint64_t val)
{
	if (t)
		t->arch.rax = val;
}

static inline uint64_t task_get_flags(const task_t *t)
{
	return t ? t->arch.rflags : 0;
}

static inline void task_set_flags(task_t *t, uint64_t flags)
{
	if (t)
		t->arch.rflags = flags;
}

static inline uint16_t task_get_cs(const task_t *t)
{
	return t ? t->arch.cs : 0;
}

static inline void task_set_cs(task_t *t, uint16_t cs)
{
	if (t)
		t->arch.cs = cs;
}

static inline uint16_t task_get_ss(const task_t *t)
{
	return t ? t->arch.ss : 0;
}

static inline void task_set_ss(task_t *t, uint16_t ss)
{
	if (t)
		t->arch.ss = ss;
}

static inline void task_set_user_return(task_t *task, uint64_t ip,
					     uint64_t sp, uint64_t flags)
{
	if (!task)
		return;

	task_set_ip(task, ip);
	task_set_sp(task, sp);
	task_set_flags(task, flags);

#if defined(USER_CODE_SEL) && defined(USER_DATA_SEL)
	task_set_cs(task, (uint16_t)USER_CODE_SEL);
	task_set_ss(task, (uint16_t)USER_DATA_SEL);
	task->arch.ds = (uint16_t)USER_DATA_SEL;
	task->arch.es = (uint16_t)USER_DATA_SEL;
#endif
}

static inline void task_prepare_fork_child(arch_task_context_t *child,
						const arch_task_context_t *parent)
{
	if (!child || !parent)
		return;

	task_context_clone(child, parent);
	child->rax = 0;
}

static inline void task_clear_frame_pointer(task_t *t)
{
	if (t)
		t->arch.rbp = 0;
}

static inline void task_set_frame_pointer(task_t *t, uint64_t fp)
{
	if (t)
		t->arch.rbp = fp;
}

static inline uint64_t task_get_rdi(const task_t *t)
{
	return t ? t->arch.rdi : 0;
}

static inline void task_set_rdi(task_t *t, uint64_t val)
{
	if (t)
		t->arch.rdi = val;
}

static inline uint64_t task_get_rsi(const task_t *t)
{
	return t ? t->arch.rsi : 0;
}

static inline void task_set_rsi(task_t *t, uint64_t val)
{
	if (t)
		t->arch.rsi = val;
}

static inline uint64_t task_get_rdx(const task_t *t)
{
	return t ? t->arch.rdx : 0;
}

static inline void task_set_rdx(task_t *t, uint64_t val)
{
	if (t)
		t->arch.rdx = val;
}

/* Portable syscall/arg accessors (§3) — prefer these over rdi/rsi/rdx names. */
static inline uint64_t task_get_arg0(const task_t *t)
{
	return task_get_rdi(t);
}

static inline void task_set_arg0(task_t *t, uint64_t val)
{
	task_set_rdi(t, val);
}

static inline uint64_t task_get_arg1(const task_t *t)
{
	return task_get_rsi(t);
}

static inline void task_set_arg1(task_t *t, uint64_t val)
{
	task_set_rsi(t, val);
}

static inline uint64_t task_get_arg2(const task_t *t)
{
	return task_get_rdx(t);
}

static inline void task_set_arg2(task_t *t, uint64_t val)
{
	task_set_rdx(t, val);
}

static inline void task_set_user_segments(task_t *t)
{
	if (!t)
		return;

#if defined(USER_CODE_SEL) && defined(USER_DATA_SEL)
	task_set_cs(t, (uint16_t)USER_CODE_SEL);
	task_set_ss(t, (uint16_t)USER_DATA_SEL);
	t->arch.ds = (uint16_t)USER_DATA_SEL;
	t->arch.es = (uint16_t)USER_DATA_SEL;
	t->arch.fs = (uint16_t)USER_DATA_SEL;
	t->arch.gs = (uint16_t)USER_DATA_SEL;
#else
	task_set_cs(t, 0x1B);
	task_set_ss(t, 0x23);
	t->arch.ds = 0x23;
	t->arch.es = 0x23;
	t->arch.fs = 0x23;
	t->arch.gs = 0x23;
#endif
}

static inline int task_cs_is_user(const task_t *t)
{
	return (task_get_cs(t) & 3u) != 0u;
}

static inline void task_set_kernel_segments(task_t *t)
{
	if (!t)
		return;

#if defined(KERNEL_CODE_SEL) && defined(KERNEL_DATA_SEL)
	task_set_cs(t, (uint16_t)KERNEL_CODE_SEL);
	task_set_ss(t, (uint16_t)KERNEL_DATA_SEL);
	t->arch.ds = (uint16_t)KERNEL_DATA_SEL;
	t->arch.es = (uint16_t)KERNEL_DATA_SEL;
	t->arch.fs = (uint16_t)KERNEL_DATA_SEL;
	t->arch.gs = (uint16_t)KERNEL_DATA_SEL;
#endif
}

#endif
