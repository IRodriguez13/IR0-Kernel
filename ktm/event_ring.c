/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: event_ring.c
 * Description: Typed KTM event ring + legacy string wrappers + consumer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ktm.h>
#include <config.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <ir0/ktm/deferred.h>
#include <string.h>

/*
 * 64 KiB of static ring (64 B/event). Sized against the observed worst case:
 * an idle shell ping-pongs BLOCK/WAKE once per tick, which flushed a
 * 256-entry ring in well under a second and left a failure dump showing only
 * scheduler churn — the pipe history that explains the failure was already
 * overwritten.
 */
#define KTM_EVENT_RING_CAP 1024

/* Widest event type + slack; the last slot absorbs anything beyond it. */
#define KTM_DUMP_TYPE_MAX 64

static ktm_event_t g_ring[KTM_EVENT_RING_CAP];
/* Static, not on the stack: dumps run from crash paths with little headroom. */
static uint8_t g_dump_selected[KTM_EVENT_RING_CAP / 8];
static uint16_t g_dump_type_count[KTM_DUMP_TYPE_MAX];
static uint32_t g_head; /* next write index (monotonic) */
static uint32_t g_tail; /* next read index for consumers */
static uint64_t g_seq;
static ktm_context_t *g_ctx;

ktm_context_t *ktm_current_context(void)
{
	return g_ctx;
}

void ktm_set_current_context(ktm_context_t *ctx)
{
	g_ctx = ctx;
}

uint64_t ktm_now_ticks(void)
{
	return g_seq; /* monotonic enough for v1 ordering */
}

int32_t ktm_current_pid(void)
{
	extern process_t *current_process;

	return current_process ? (int32_t)current_process->task.pid : 0;
}

void ktm_event_emit4(uint16_t type, uint16_t subsystem,
		     uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
	ktm_event_t *slot;
	uint32_t idx;

#if !(defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS)
	(void)type;
	(void)subsystem;
	(void)arg0;
	(void)arg1;
	(void)arg2;
	(void)arg3;
	return;
#else
	g_seq++;
	idx = g_head % KTM_EVENT_RING_CAP;
	slot = &g_ring[idx];
	g_head++;

	/* Drop oldest unread if ring full. */
	if ((uint32_t)(g_head - g_tail) > KTM_EVENT_RING_CAP)
		g_tail = g_head - KTM_EVENT_RING_CAP;

	slot->sequence = g_seq;
	slot->timestamp = g_seq;
	slot->cpu = 0;
	slot->pid = ktm_current_pid();
	slot->type = type;
	slot->subsystem = subsystem;
	slot->arg0 = arg0;
	slot->arg1 = arg1;
	slot->arg2 = arg2;
	slot->arg3 = arg3;

	KTM_FLIGHT((uint16_t)type, (uint32_t)arg0, (uint32_t)arg1,
		   (uint32_t)arg2, (uint32_t)arg3);

	(void)slot;
#endif
}

int ktm_event_pending(void)
{
#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS
	return (g_head != g_tail) ? 1 : 0;
#else
	return 0;
#endif
}

void ktm_event_ring_reset_cursor(void)
{
	g_tail = g_head;
}

void ktm_event_ring_dump(size_t max_events, uint32_t subsys_mask)
{
#if !(defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS)
	(void)max_events;
	(void)subsys_mask;
#else
	uint32_t stored;
	uint32_t first;
	uint32_t i;
	uint32_t matched = 0;
	uint32_t selected = 0;
	uint32_t quota;

	/*
	 * Turn rows recorded from paths too hot to emit into real events
	 * before walking the ring, so a dump shows both in one timeline.
	 */
	ktm_deferred_flush();

	/*
	 * Walks backwards from the write head instead of the consumer cursor:
	 * the point of a dump is the history leading up to a failure, which is
	 * usually already consumed (or never was, on a path with no consumer).
	 * Ordering bugs across two tasks — a wake published before the sleeper
	 * blocks, a pipe end released while a reader waits — are invisible in
	 * interleaved printf logs but obvious in sequence order here.
	 */
	stored = (g_head < KTM_EVENT_RING_CAP) ? g_head : KTM_EVENT_RING_CAP;
	first = g_head - stored;

	for (i = 0; i < stored; i++)
	{
		const ktm_event_t *e = &g_ring[(first + i) % KTM_EVENT_RING_CAP];

		if (!subsys_mask || (subsys_mask & (1u << e->subsystem)))
			matched++;
	}

	/*
	 * Select newest-first under a per-type quota.
	 *
	 * Taking the newest N outright makes the dump useless whenever one
	 * type dominates: the resume gate fires on every return to ring 3, and
	 * ktm_deferred_flush() above replays its backlog at the head, so a
	 * segfault dump came out as 48 identical CTX_USER_IRET rows with the
	 * page faults and pipe transitions that explain the crash pushed out.
	 * Capping each type keeps room for the rare events, which are the
	 * informative ones. A second pass backfills from whatever is left so a
	 * quiet ring still fills the budget.
	 */
	if (max_events == 0 || max_events > KTM_EVENT_RING_CAP)
		max_events = KTM_EVENT_RING_CAP;
	quota = (uint32_t)max_events / 4;
	if (quota < 2)
		quota = 2;

	memset(g_dump_selected, 0, sizeof(g_dump_selected));
	memset(g_dump_type_count, 0, sizeof(g_dump_type_count));

	for (i = stored; i-- > 0 && selected < max_events;)
	{
		const ktm_event_t *e = &g_ring[(first + i) % KTM_EVENT_RING_CAP];
		uint16_t t = e->type;

		if (subsys_mask && !(subsys_mask & (1u << e->subsystem)))
			continue;
		if (t >= KTM_DUMP_TYPE_MAX)
			t = KTM_DUMP_TYPE_MAX - 1;
		if (g_dump_type_count[t] >= quota)
			continue;
		g_dump_type_count[t]++;
		g_dump_selected[i >> 3] |= (uint8_t)(1u << (i & 7));
		selected++;
	}

	for (i = stored; i-- > 0 && selected < max_events;)
	{
		const ktm_event_t *e = &g_ring[(first + i) % KTM_EVENT_RING_CAP];

		if (subsys_mask && !(subsys_mask & (1u << e->subsystem)))
			continue;
		if (g_dump_selected[i >> 3] & (1u << (i & 7)))
			continue;
		g_dump_selected[i >> 3] |= (uint8_t)(1u << (i & 7));
		selected++;
	}

	klog_notice_fmt("KTM",
			"KTM_RING_BEGIN stored=%x match=%x shown=%x mask=%x\n",
			(unsigned)stored, (unsigned)matched,
			(unsigned)selected, (unsigned)subsys_mask);
	for (i = 0; i < stored; i++)
	{
		const ktm_event_t *e = &g_ring[(first + i) % KTM_EVENT_RING_CAP];

		if (!(g_dump_selected[i >> 3] & (1u << (i & 7))))
			continue;

		klog_notice_fmt("KTM",
			       "KTM_EV seq=%x pid=%x t=%x sub=%x a0=%llx a1=%llx a2=%llx a3=%llx\n",
			       (unsigned)e->sequence, (unsigned)e->pid,
			       (unsigned)e->type, (unsigned)e->subsystem,
			       (unsigned long long)e->arg0,
			       (unsigned long long)e->arg1,
			       (unsigned long long)e->arg2,
			       (unsigned long long)e->arg3);
	}
	klog_notice_fmt("KTM", "KTM_RING_END\n");
#endif
}

int ktm_event_copy_out(ktm_event_t *dst, size_t max_events)
{
	size_t n = 0;

	if (!dst || max_events == 0)
		return 0;
#if !(defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS)
	(void)dst;
	(void)max_events;
	return 0;
#else
	while (n < max_events && g_tail != g_head)
	{
		dst[n++] = g_ring[g_tail % KTM_EVENT_RING_CAP];
		g_tail++;
	}
	return (int)n;
#endif
}

#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS

void ktm_event_emit(const char *tag)
{
	klog_debug_fmt("KTM", "[KTM][EV] %s", tag ? tag : "(null)");
	ktm_event_emit4(KTM_EVENT_INFO, KTM_SUBSYS_CORE, 0, 0, 0, 0);
	ktm_transport_emit("EV", tag ? tag : "(null)", NULL);
}

void ktm_event_emit_pid(const char *tag, uint32_t pid)
{
	klog_debug_fmt("KTM", "[KTM][EV] %s pid=%x", tag ? tag : "(null)",
		       (unsigned)pid);
	ktm_event_emit4(KTM_EVENT_INFO, KTM_SUBSYS_CORE, pid, 0, 0, 0);
	ktm_transport_emit("EV", tag ? tag : "(null)", NULL);
}

#endif /* CONFIG_KTM_EVENTS */
