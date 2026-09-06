/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: spinlock.h
 * Description: UP spinlock facade (irq-save); SMP backend later.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/cpu.h>
#include <config.h>

/*
 * UP (default): mask IRQs for the critical section — same contract as the
 * legacy ipc_irq_save / pipe refcount paths.
 *
 * SMP (future): per-lock atomic owner + arch_cpu_relax; not wired yet.
 */
#if defined(CONFIG_SMP) && CONFIG_SMP
#error "ir0_spinlock: SMP backend not implemented — use UP build for now"
#endif

typedef struct
{
	unsigned long flags;
} ir0_spinlock_t;

static inline void ir0_spin_lock(ir0_spinlock_t *lock)
{
	lock->flags = irq_save();
}

static inline void ir0_spin_unlock(ir0_spinlock_t *lock)
{
	irq_restore(lock->flags);
}
