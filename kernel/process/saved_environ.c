/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: saved_environ.c
 * Description: Exec-time environ cache for /proc/<pid>/environ.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <string.h>

#define PROCESS_ENVIRON_MAX (128U * 1024U)
#define PROCESS_ENVIRON_VARS_MAX 256

void process_saved_environ_clear(process_t *p)
{
	if (!p)
		return;

	if (p->saved_environ)
	{
		kfree(p->saved_environ);
		p->saved_environ = NULL;
	}
	p->saved_environ_len = 0;
}

int process_saved_environ_set(process_t *p, char *const envp[])
{
	size_t total = 0;
	int envc = 0;

	if (!p)
		return -EINVAL;

	process_saved_environ_clear(p);
	if (!envp)
		return 0;

	while (envp[envc])
	{
		size_t len = strlen(envp[envc]) + 1;

		if (envc >= PROCESS_ENVIRON_VARS_MAX ||
		    total + len > PROCESS_ENVIRON_MAX)
			return -E2BIG;
		total += len;
		envc++;
	}

	if (total == 0)
		return 0;

	p->saved_environ = kmalloc_try(total);
	if (!p->saved_environ)
		return -ENOMEM;

	total = 0;
	for (int i = 0; i < envc; i++)
	{
		size_t len = strlen(envp[i]) + 1;

		memcpy(p->saved_environ + total, envp[i], len);
		total += len;
	}
	p->saved_environ_len = total;
	return 0;
}

int process_saved_environ_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	process_saved_environ_clear(dst);
	if (!src->saved_environ || src->saved_environ_len == 0)
		return 0;

	dst->saved_environ = kmalloc_try(src->saved_environ_len);
	if (!dst->saved_environ)
		return -ENOMEM;

	memcpy(dst->saved_environ, src->saved_environ, src->saved_environ_len);
	dst->saved_environ_len = src->saved_environ_len;
	return 0;
}
