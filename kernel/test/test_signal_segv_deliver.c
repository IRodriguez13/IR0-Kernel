/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_signal_segv_deliver.c
 * Description: ktest — SIGSEGV delivery from synthetic IRQ frame
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include <config.h>
#include <ir0/signals.h>
#include <ir0/signal_irq.h>
#include <ir0/process.h>
#include <ir0/kmem.h>
#include <ir0/task_ops.h>
#include <stdint.h>

void ktest_signal_segv_deliver_irq_frame(void)
{
	uint64_t stub[32];
	uint64_t *frame;
	void *handler = (void *)(uintptr_t)0x00450000UL;
	uint64_t fault_addr = 0x08004000UL;
	int delivered;

	KTEST_BEGIN("signal_segv_deliver_irq_frame");

	KASSERT(current_process != NULL);

	frame = stub + 15;

	current_process->mode = USER_MODE;
	current_process->signal_handlers[SIGSEGV] = (void (*)(int))handler;
	current_process->signal_sa_flags[SIGSEGV] = 0;
	current_process->signal_mask = 0;
	current_process->signal_ignored = 0;
	process_saved_context_init(current_process);

	frame[0] = 14;
	frame[1] = 4;
	frame[2] = 0x004422E3UL;
	frame[3] = (uint64_t)USER_CODE_SEL;
	frame[4] = task_initial_user_status();
	frame[5] = 0x7FFFEB38UL;
	frame[6] = (uint64_t)USER_DATA_SEL;
	frame[-7] = 0;
	frame[-6] = 0;
	frame[-3] = 0;
	frame[-1] = 0x11111111UL;

	KASSERT(signals_has_user_handler(current_process, SIGSEGV));

	delivered = signals_deliver_from_irq_frame(current_process, SIGSEGV,
						   frame, fault_addr);
	KASSERT_EQ(delivered, 1);
	KASSERT(frame[2] == (uint64_t)(uintptr_t)handler);
	KASSERT(frame[-7] == (uint64_t)SIGSEGV);
	KASSERT(process_saved_context_present(current_process));
	KASSERT(sigcontext_ip(process_saved_context_peek(current_process)) ==
		0x004422E3UL);

	process_saved_context_clear(current_process);

	KTEST_END();
}
