/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: domains.c
 * Description: Explicit clone/init for process credential/signal/MM domains.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/process_domains.h>
#include <ir0/errno.h>
#include <string.h>

int process_cred_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	dst->uid = src->uid;
	dst->gid = src->gid;
	dst->euid = src->euid;
	dst->egid = src->egid;
	dst->suid = src->suid;
	dst->sgid = src->sgid;
	dst->umask = src->umask;
	dst->ngroups = src->ngroups;
	memcpy(dst->groups, src->groups, sizeof(dst->groups));
	dst->no_new_privs = src->no_new_privs;
	dst->at_secure = src->at_secure;
	return 0;
}

int process_signals_clone(process_t *dst, const process_t *src)
{
	int i;

	if (!dst || !src)
		return -EINVAL;

	dst->signal_pending = 0;
	dst->signal_mask = src->signal_mask;
	dst->signal_ignored = src->signal_ignored;
	/* Linux: interval timers are not inherited by the child. */
	dst->it_real_expire_ms = 0;
	dst->it_real_interval_ms = 0;
	for (i = 0; i < _NSIG; i++)
	{
		dst->signal_handlers[i] = src->signal_handlers[i];
		dst->signal_sa_flags[i] = src->signal_sa_flags[i];
		dst->signal_sa_mask[i] = src->signal_sa_mask[i];
		dst->signal_restorer[i] = src->signal_restorer[i];
	}
	dst->saved_context = NULL;
	dst->signal_enter_pending = 0;
	return 0;
}

int process_rel_init_child(process_t *child, const process_t *parent,
			   pid_t child_pid)
{
	if (!child || !parent)
		return -EINVAL;

	child->task.pid = child_pid;
	child->tgid = child_pid;
	child->ppid = parent->task.pid;
	child->sid = parent->sid;
	child->pgid = parent->pgid;
	process_set_sched_state(child, PROCESS_READY);
	child->mode = parent->mode;
	child->sched_prio = parent->sched_prio;
	return 0;
}

int process_mm_cursor_clone(process_t *dst, const process_t *src)
{
	/*
	 * Cursors live only in mm_struct. Child gets a fresh mm in
	 * fork_child_mm_create() which copies heap/stack/mmap_base from parent.
	 */
	if (!dst || !src)
		return -EINVAL;
	dst->mm = NULL;
	return 0;
}

int process_session_attrs_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	memcpy(dst->cwd, src->cwd, sizeof(dst->cwd));
	memcpy(dst->root, src->root, sizeof(dst->root));
	memcpy(dst->comm, src->comm, sizeof(dst->comm));
	memcpy(dst->rlimits, src->rlimits, sizeof(dst->rlimits));
	dst->robust_list = src->robust_list;
	return 0;
}
