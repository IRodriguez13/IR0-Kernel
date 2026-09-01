/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pseudo_fd_bind.c
 * Description: Refcount helpers for process-local pseudo-fs fd bindings.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process.h"
#include <ir0/arch_cpu.h>

void pseudo_fd_bind_acquire(pseudo_fd_bind_t *bind)
{
	unsigned long irq_flags;

	if (!bind)
		return;

	irq_flags = irq_save();
	bind->refs++;
	irq_restore(irq_flags);
}

int pseudo_fd_bind_release(pseudo_fd_bind_t *bind)
{
	unsigned long irq_flags;
	int last;

	if (!bind)
		return 0;

	irq_flags = irq_save();
	if (bind->refs > 0)
		bind->refs--;
	last = (bind->refs == 0);
	irq_restore(irq_flags);
	return last;
}
