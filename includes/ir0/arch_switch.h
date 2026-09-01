/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/switch.h>

/* ISA-private; only sched/switch dispatcher and arch_switch.c. */
void arch_switch_to(task_t *prev, task_t *next);
