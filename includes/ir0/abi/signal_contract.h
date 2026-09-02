/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal_contract.h
 * Description: Portable signal-delivery stack layout contract (poly-ISA).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Bytes kept free below USER_STACK_TOP for alternate signal frames when the
 * interrupted SP is missing or too deep. Must exceed sigframe + musl redzone
 * + libc work in handlers (clock_gettime, printf). Too small (e.g. 2 KiB)
 * places handlers against the KTM canary and causes STACK_TOP_OVERRUN at
 * 7ffff000 under interactive ash.
 */
#define SIGNAL_HANDLER_TOP_MARGIN 0x4000UL
