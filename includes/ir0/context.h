/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context.h
 * Description: Context-switch facade (portable sched policy API).
 *
 * Callers model "change to the next runnable task", not "invoke the ISA switch
 * backend". Implementation is selected at link time (switch_x64.asm /
 * switch_context_arm64.S) via switch_to() → arch_switch_to() in the
 * sched/switch dispatcher only.
 *
 * Include this (or sched.h), not arch_switch.h, from portable code.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>
#include <ir0/switch.h>

struct process;

void switch_to(task_t *prev, task_t *next);

/*
 * Enter userspace with full task register state (fork/signal/syscall-block resume).
 * ISA backend performs iretq / EL drop; portable code names the contract only.
 */
void switch_to_user_task(const struct task *task);

void switch_to_user(uintptr_t entry, uintptr_t stack);
