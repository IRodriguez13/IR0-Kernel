/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: deferred.c
 * Description: Record-now / emit-later observation for hot kernel paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/deferred.h>
#include <ir0/ktm/event.h>
#include <ir0/klog.h>

struct ktm_deferred_row
{
	uint32_t kind;
	uint32_t pid;
	uint64_t a0;
	uint64_t a1;
	uint64_t a2;
};

static struct ktm_deferred_row ktm_deferred_rows[KTM_DEFERRED_CAP];
static uint32_t ktm_deferred_head;    /* next slot to write */
static uint32_t ktm_deferred_stored;  /* rows held, saturates at CAP */

void ktm_deferred_record(uint32_t kind, uint32_t pid, uint64_t a0, uint64_t a1,
			 uint64_t a2)
{
	struct ktm_deferred_row *row = &ktm_deferred_rows[ktm_deferred_head];

	ktm_deferred_head = (ktm_deferred_head + 1) % KTM_DEFERRED_CAP;
	if (ktm_deferred_stored < KTM_DEFERRED_CAP)
		ktm_deferred_stored++;

	row->kind = kind;
	row->pid = pid;
	row->a0 = a0;
	row->a1 = a1;
	row->a2 = a2;
}

void ktm_deferred_flush(void)
{
	uint32_t n = ktm_deferred_stored;
	uint32_t first = (ktm_deferred_head + KTM_DEFERRED_CAP - n) %
			 KTM_DEFERRED_CAP;
	uint32_t i;

	/* Reset first: emitting can itself reschedule. */
	ktm_deferred_stored = 0;

	for (i = 0; i < n; i++)
	{
		const struct ktm_deferred_row *row =
			&ktm_deferred_rows[(first + i) % KTM_DEFERRED_CAP];

		switch (row->kind)
		{
		case KTM_DEFERRED_RESUME_GATE:
			ktm_event_emit4(KTM_EVENT_CTX_USER_IRET,
					KTM_SUBSYS_SCHED, (uint64_t)row->pid,
					row->a0, row->a1, row->a2);
			break;
		case KTM_DEFERRED_PAGE_FAULT:
			ktm_event_emit4(KTM_EVENT_PAGE_FAULT, KTM_SUBSYS_MM,
					(uint64_t)row->pid, row->a0, row->a1,
					row->a2);
			break;
		case KTM_DEFERRED_STACK_LOW:
			ktm_event_emit4(KTM_EVENT_STACK_LOW, KTM_SUBSYS_MM,
					(uint64_t)row->pid, row->a0, row->a1,
					row->a2);
			break;
		default:
			break;
		}
	}

}
