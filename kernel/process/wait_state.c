/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: wait_state.c
 * Description: wait4 blocked-syscall contract fields (process_t encapsulation).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

int process_wait_blocked(const process_t *p)
{
	if (!p)
		return 0;
	return p->wait_blocked != 0;
}

void process_wait_blocked_set(process_t *p)
{
	if (p)
		p->wait_blocked = 1;
}

void process_wait_blocked_clear(process_t *p)
{
	if (p)
		p->wait_blocked = 0;
}

pid_t process_wait_target_pid(const process_t *p)
{
	if (!p)
		return 0;
	return p->wait_target_pid;
}

void process_wait_target_pid_set(process_t *p, pid_t pid)
{
	if (p)
		p->wait_target_pid = pid;
}

int process_wait_options(const process_t *p)
{
	if (!p)
		return 0;
	return p->wait_options;
}

void process_wait_options_set(process_t *p, int options)
{
	if (p)
		p->wait_options = options;
}

int *process_wait_status_ptr_peek(process_t *p)
{
	if (!p)
		return NULL;
	return p->wait_status_ptr;
}

void process_wait_status_ptr_set(process_t *p, int *status_ptr)
{
	if (p)
		p->wait_status_ptr = status_ptr;
}

pid_t process_wait_resume_child_pid(const process_t *p)
{
	if (!p)
		return 0;
	return p->wait_resume_child_pid;
}

void process_wait_resume_child_pid_set(process_t *p, pid_t pid)
{
	if (p)
		p->wait_resume_child_pid = pid;
}

void process_wait_state_init(process_t *p)
{
	if (!p)
		return;
	p->wait_status_ptr = NULL;
	p->wait_blocked = 0;
	p->wait_target_pid = 0;
	p->wait_options = 0;
	p->wait_resume_child_pid = 0;
}

void process_wait_state_arm(process_t *p, pid_t pid, int options, int *status_ptr)
{
	if (!p)
		return;
	p->wait_status_ptr = status_ptr;
	p->wait_blocked = 1;
	p->wait_target_pid = pid;
	p->wait_options = options;
	p->wait_resume_child_pid = 0;
}

void process_wait_state_clear(process_t *p)
{
	process_wait_state_init(p);
}
