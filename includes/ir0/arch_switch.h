/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/switch.h>

/*
 * ISA-private context switch body. Only sched/switch/arch_context_switch.c and
 * per-ISA arch_switch.c under arch/ may include this header (arch-guard).
 */
void arch_switch_to(task_t *prev, task_t *next);
