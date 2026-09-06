/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_clock.h
 * Description: Pre-calibration monotonic counter facade (klog / boot timing).
 *
 * Portable callers use semantic names; x86 reads TSC, ARM64 reads CNTVCT,
 * without embedding ISA in the API.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef enum
{
	EARLY_CLOCK_UNAVAILABLE = 0,
	EARLY_CLOCK_RAW = 1,
	EARLY_CLOCK_CALIBRATED = 2,
	EARLY_CLOCK_MONOTONIC = 3
} early_clock_quality_t;

/*
 * Earliest ordered counter before PIT/HPET calibration.
 * EARLY_CLOCK_RAW ticks are not wall-clock until clock_is_ready().
 */
int early_clock_available(void);
uint64_t early_clock_read(void);
early_clock_quality_t early_clock_quality(void);
