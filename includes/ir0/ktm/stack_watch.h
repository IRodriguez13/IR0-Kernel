/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stack_watch.h
 * Description: Kernel stack headroom high-water mark
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>

/* Sample @rsp; ignores addresses outside the kernel stack window. */
void ktm_stack_watch(uint64_t rsp);

/* Smallest headroom seen since boot, in bytes (0 if never sampled). */
uint64_t ktm_stack_min_headroom_get(void);

/* Interrupt nesting depth, tracked around the ISR dispatch. */
void ktm_irq_nest_enter(void);
void ktm_irq_nest_exit(void);
unsigned ktm_irq_nest_max_get(void);

/* Peak kernel stack bytes ever used by the calling task (poison scan). */
uint64_t ktm_stack_peak_used(void);
