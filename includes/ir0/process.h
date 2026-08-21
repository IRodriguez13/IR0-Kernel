/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.h
 * Description: Process/task public include. Layout is not fully encapsulated yet.
 *
 * Include as <ir0/process.h>. kernel/process.h still publishes process_t
 * (signals, wait, blocked-syscall resume, creds, timers, stacks, ...).
 * Domain extraction is in progress (mm_struct, files_struct); new code
 * should use accessors rather than growing the struct further.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <kernel/process.h>

