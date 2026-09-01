/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context.h
 * Description: Context-switch facade (no <sched/...> includes).
 *
 * Public ISA-polymorphic entry: switch_to(). ISA asm
 * (switch_context_x64 / switch_context_arm64) stays private to the dispatcher.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>
#include <ir0/switch.h>

struct process;

void switch_to(task_t *prev, task_t *next);
