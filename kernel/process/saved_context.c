/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: saved_context.c
 * Description: sigreturn saved-context lifecycle (attach/clear/peek).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/kmem.h>

struct sigcontext *process_saved_context_peek(const process_t *p)
{
	if (!p)
		return NULL;
	return p->saved_context;
}

int process_saved_context_present(const process_t *p)
{
	return p && p->saved_context != NULL;
}

void process_saved_context_init(process_t *p)
{
	if (p)
		p->saved_context = NULL;
}

int process_saved_context_attach(process_t *p, struct sigcontext *ctx)
{
	if (!p || !ctx)
	{
		if (ctx)
			kfree(ctx);
		return -1;
	}
	/*
	 * Never silently replace an armed outer context: that dropped the
	 * real interrupt site and left rdi=signum for the next sigreturn
	 * (BusyBox ash setjmp → #PF at addr 2).
	 */
	if (p->saved_context)
	{
		kfree(ctx);
		return -1;
	}
	p->saved_context = ctx;
	return 0;
}

void process_saved_context_clear(process_t *p)
{
	if (!p || !p->saved_context)
		return;
	kfree(p->saved_context);
	p->saved_context = NULL;
}
