/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm.h
 * Description: KTM — Kernel Trace Module (IR0 public facade)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>
#include <ir0/ktm/klog.h>
#include <stdint.h>

struct process;


#ifndef KTM_FILE
#define KTM_FILE \
	(__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

static inline void ktm_fail_at(const char *kind, const char *expr,
			       const char *file, unsigned int line)
{
	klog_error_fmt("KTM", "[FAIL] %s at %s:%u — %s",
		       kind ? kind : "assert",
		       file ? file : "?",
		       line,
		       expr ? expr : "(null)");
}

#define KTM_MARK_FAIL() do { \
	extern int _ktest_failed; \
	_ktest_failed = 1; \
} while (0)

#define KTM_ASSERT(cond) do { \
	if (!(cond)) { \
		ktm_fail_at("ASSERT", #cond, KTM_FILE, __LINE__); \
		KTM_MARK_FAIL(); \
	} \
} while (0)

#define KTM_ASSERT_EQ(a, b) do { \
	if ((a) != (b)) { \
		ktm_fail_at("ASSERT_EQ", #a " == " #b, KTM_FILE, __LINE__); \
		KTM_MARK_FAIL(); \
	} \
} while (0)

#define KTM_ASSERT_GE(a, b) do { \
	if ((a) < (b)) { \
		ktm_fail_at("ASSERT_GE", #a " >= " #b, KTM_FILE, __LINE__); \
		KTM_MARK_FAIL(); \
	} \
} while (0)

#define KTM_ASSERT_GT(a, b) do { \
	if ((a) <= (b)) { \
		ktm_fail_at("ASSERT_GT", #a " > " #b, KTM_FILE, __LINE__); \
		KTM_MARK_FAIL(); \
	} \
} while (0)

/* ------------------------------------------------------------------------- */
/* Context snapshot + panic class (CONFIG_KTM / always declared)             */
/* ------------------------------------------------------------------------- */

void ktm_panic_class_emit(const char *klass);

void ktm_panic_site_emit(const char *file, unsigned int line, const char *caller,
			 const char *message);
void ktm_classify_kernel_panic(const char *message);
void ktm_classify_kernel_panic_ex(const char *message, int panic_level,
				  const char *file, unsigned int line,
				  const char *caller);

void ktm_classify_user_fault(struct process *proc, uint64_t fault_addr,
			     uint64_t fault_rip, uint64_t fault_cs);

void ktm_ctx_snapshot(const struct process *p, const char *reason);

/* ------------------------------------------------------------------------- */
/* Flight recorder (CONFIG_KTM_FLIGHT) — ring buffer dumped on panic         */
/* ------------------------------------------------------------------------- */

#define KTM_FL_SYSCALL_ENTER  1U
#define KTM_FL_SYSCALL_RET    2U
#define KTM_FL_SCHED_SWITCH   3U
#define KTM_FL_PANIC          4U
#define KTM_FL_INVARIANT      5U
#define KTM_FL_EV             6U
#define KTM_FL_PF_USER        7U
#define KTM_FL_SIGNAL_DELIVER 8U

void ktm_flight_record(uint16_t type, uint32_t a0, uint32_t a1,
		       uint32_t a2, uint32_t a3);
void ktm_flight_dump(void);
void ktm_flight_dump_last(uint32_t max_events);

#if defined(CONFIG_KTM_FLIGHT) && CONFIG_KTM_FLIGHT

#define KTM_FLIGHT(type, a0, a1, a2, a3) \
	ktm_flight_record((type), (uint32_t)(a0), (uint32_t)(a1), \
			  (uint32_t)(a2), (uint32_t)(a3))

#define KTM_TRACE_SYSCALL_ENTER(nr) \
	KTM_FLIGHT(KTM_FL_SYSCALL_ENTER, (nr), 0, 0, 0)

#define KTM_TRACE_SYSCALL_RET(nr, ret) \
	KTM_FLIGHT(KTM_FL_SYSCALL_RET, (nr), (uint32_t)(ret), 0, 0)

#define KTM_TRACE_SCHED_SWITCH(old_pid, new_pid) \
	KTM_FLIGHT(KTM_FL_SCHED_SWITCH, (old_pid), (new_pid), 0, 0)

#else

#define KTM_FLIGHT(type, a0, a1, a2, a3)        ((void)0)
#define KTM_TRACE_SYSCALL_ENTER(nr)               ((void)0)
#define KTM_TRACE_SYSCALL_RET(nr, ret)            ((void)0)
#define KTM_TRACE_SCHED_SWITCH(old_pid, new_pid)  ((void)0)

#endif /* CONFIG_KTM_FLIGHT */

/* ------------------------------------------------------------------------- */

#define KTM_EV_SCHED_IRQ_TTY_PREEMPT    "SCHED_IRQ_TTY_PREEMPT"
#define KTM_EV_SCHED_IRQ_TIMER_PREEMPT  "SCHED_IRQ_TIMER_PREEMPT"
#define KTM_EV_WAIT_PROMOTE_CHILD       "WAIT_PROMOTE_CHILD"
#define KTM_EV_TTY_CANON_WAKE           "TTY_CANON_WAKE"
#define KTM_EV_CTX_SNAPSHOT             "CTX_SNAPSHOT"

#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS

void ktm_event_emit(const char *tag);
void ktm_event_emit_pid(const char *tag, uint32_t pid);

#define KTM_EVENT(tag)           ktm_event_emit(tag)
#define KTM_EVENT_PID(tag, pid)  ktm_event_emit_pid((tag), (uint32_t)(pid))

#else

#define KTM_EVENT(tag)           ((void)0)
#define KTM_EVENT_PID(tag, pid)  ((void)0)

#endif /* CONFIG_KTM_EVENTS */

/* ------------------------------------------------------------------------- */
/* Deep diagnostics (IR0_KERNEL_TESTS only — zero cost otherwise)            */
/* ------------------------------------------------------------------------- */

#ifdef IR0_KERNEL_TESTS

void ktm_invariant_process(const struct process *p, const char *tag);

#define KTM_INVARIANT(proc, cond) do { \
	if (!(cond)) { \
		ktm_fail_at("INVARIANT", #cond, KTM_FILE, __LINE__); \
		ktm_invariant_process((proc), #cond); \
		KTM_MARK_FAIL(); \
	} \
} while (0)

#define KTM_INVARIANT_PROC(proc) ktm_invariant_process((proc), "snapshot")

#define KTM_PROBE(cond, tag) do { \
	if (!(cond)) { \
		ktm_fail_at("PROBE", tag, KTM_FILE, __LINE__); \
	} \
} while (0)

#define KTM_PROBE_CTX(proc, cond, tag) do { \
	if (!(cond)) { \
		ktm_fail_at("PROBE", tag, KTM_FILE, __LINE__); \
		ktm_invariant_process((proc), tag); \
	} \
} while (0)

#define KTM_TRACE(tag) do { \
	klog_debug("KTM", tag); \
} while (0)

void ktm_sched_gate_enter_irq(void);
void ktm_sched_gate_leave_irq(void);
void ktm_sched_gate_check_before_sched(const char *caller);

#else /* !IR0_KERNEL_TESTS */

#define KTM_INVARIANT(proc, cond)        ((void)0)
#define KTM_INVARIANT_PROC(proc)         ((void)0)
#define KTM_PROBE(cond, tag)             ((void)0)
#define KTM_PROBE_CTX(proc, cond, tag)   ((void)0)
#define KTM_TRACE(tag)                   ((void)0)

static inline void ktm_sched_gate_enter_irq(void) { }
static inline void ktm_sched_gate_leave_irq(void) { }
static inline void ktm_sched_gate_check_before_sched(const char *caller)
{
	(void)caller;
}

#endif /* IR0_KERNEL_TESTS */

/* ------------------------------------------------------------------------- */
/* In-kernel test harness (kernel/test)                                      */
/* ------------------------------------------------------------------------- */

#define KTM_BEGIN(name) do { \
	extern int _ktest_failed; \
	extern int _ktest_count; \
	_ktest_failed = 0; \
	_ktest_count++; \
	klog_info_fmt("KTM", "%s ...", name); \
} while (0)

#define KTM_END() do { \
	extern int _ktest_failed; \
	extern int _ktest_pass; \
	if (_ktest_failed) { \
		klog_error("KTM", "FAIL"); \
		_ktest_pass = 0; \
	} else { \
		klog_info("KTM", "PASS"); \
	} \
} while (0)

#define KTEST_BEGIN  KTM_BEGIN
#define KTEST_END    KTM_END
#define KASSERT      KTM_ASSERT
#define KASSERT_EQ   KTM_ASSERT_EQ
#define KASSERT_GE   KTM_ASSERT_GE
#define KASSERT_GT   KTM_ASSERT_GT

void kernel_test_run_all(void);
