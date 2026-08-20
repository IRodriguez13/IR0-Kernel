/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rr_sched.c
 * Description: IR0 Kernel - Round-Robin Scheduler
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process.h"
#include "rr_sched.h"
#include "sched_ops.h"
#include "sched_switch.h"
#include <config.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <ir0/arch_port.h>
#include <stdint.h>

/*
 * Ready list is singly linked (head -> ... -> tail, next=NULL). Advance wraps
 * with `next ? next : rr_head`; it is not a circular queue.
 */
static rr_task_t *rr_head = NULL;
static rr_task_t *rr_tail = NULL;
static rr_task_t *rr_current = NULL;

/*
 * Bound a pick walk if the list is dirty with zombies/blocked tasks.
 * Prevents livelock; not a statement about maximum process count.
 */
#define RR_MAX_PICK_ATTEMPTS 100

/*
 * UP scheduler critical sections:
 * serialize scheduler queue mutations against timer IRQ scheduling.
 */
static inline uint64_t rr_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void rr_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

/**
 * rr_add_process - Add a process to the round-robin scheduler
 * @proc: Process to add (must be valid and not already in scheduler)
 *
 * Inserts a new process into the round-robin queue at the tail,
 * maintaining FIFO order. The process is marked as READY and will
 * be scheduled on the next scheduling pass.
 *
 * Algorithm:
 * 1. Validate process pointer (prevent NULL pointer dereference)
 * 2. Allocate scheduler node (panic on failure - critical path)
 * 3. Link process to scheduler node
 * 4. Append to tail of the ready list (maintains FIFO order)
 * 5. Mark process as READY (eligible for scheduling)
 *
 * Complexity: O(1) - constant time insertion
 *
 * Thread safety: NOT thread-safe - caller must ensure mutual exclusion
 */
void rr_add_process(process_t *proc)
{
	rr_task_t *scan;
	rr_task_t *node;
	uint64_t irq_flags;

	/* Sanity check - prevent adding NULL processes.
	 * This is a defensive check for API misuse.
	 */
	if (!proc)
		return;

	/* Allocate scheduler node - must succeed.
	 * Out of memory in the scheduler is a critical failure since
	 * we can't schedule new processes. Panic if allocation fails.
	 */
	node = kmalloc(sizeof(rr_task_t));
	if (!node)
	{
		BUG_ON(1); /* Out of memory in scheduler - unrecoverable */
		return;
	}

	BUG_ON(!proc); /* Invalid process parameter - should never happen */
	
	/* Initialize scheduler node with process pointer.
	 * The node will be linked into the ready list.
	 */
	node->process = proc;
	node->next = NULL;

	irq_flags = rr_irq_save();

	/* Insert at tail of the singly-linked ready list.
	 * If queue is empty, initialize head and tail to point to new node.
	 * Otherwise, append to tail and update tail pointer.
	 * This maintains FIFO order for fair scheduling.
	 */
	for (scan = rr_head; scan; scan = scan->next)
	{
		if (scan->process == proc)
		{
			/* Process already queued: only ensure READY state. */
			process_set_sched_state(proc, PROCESS_READY);
			rr_irq_restore(irq_flags);
			kfree(node);
			return;
		}
	}

	if (!rr_head)
	{
		/* First process - initialize queue */
		rr_head = rr_tail = node;
	}
	else
	{
		/* Append to tail - maintains order */
		rr_tail->next = node;
		rr_tail = node;
	}

	/* Mark process as ready for scheduling.
	 * State machine: NEW -> READY -> RUNNING -> READY -> ...
	 */
	process_set_sched_state(proc, PROCESS_READY);
	rr_irq_restore(irq_flags);
}

/**
 * rr_remove_process - Remove a process from the round-robin scheduler
 * @proc: Process to remove (must be valid and in scheduler)
 *
 * Removes a process from the scheduler queue. This is used when a process
 * exits or becomes a zombie. The process structure remains in memory until
 * reaped by the parent, but it is no longer scheduled for execution.
 *
 * Algorithm:
 * 1. Find the scheduler node containing this process
 * 2. Remove node from the ready list (handle head, middle, tail cases)
 * 3. Free scheduler node
 * 4. Update rr_current if it pointed to removed node
 *
 * Complexity: O(n) worst case where n = number of processes
 * Could be optimized to O(1) with a doubly-linked list or hash table
 *
 * Thread safety: NOT thread-safe - caller must ensure mutual exclusion
 */
void rr_remove_process(process_t *proc)
{
	rr_task_t *current;
	rr_task_t *prev;
	uint64_t irq_flags;
	
	if (!proc)
		return;

	irq_flags = rr_irq_save();
	
	/* Find the scheduler node containing this process */
	current = rr_head;
	prev = NULL;
	
	while (current)
	{
		if (current->process == proc)
		{
			/* Found it - remove from queue */
			
			/* Update queue pointers */
			if (prev)
			{
				/* Node is in middle or at tail */
				prev->next = current->next;
				if (current == rr_tail)
					rr_tail = prev;
			}
			else
			{
				/* Node is at head */
				rr_head = current->next;
				if (!rr_head)
					rr_tail = NULL;  /* Queue is now empty */
			}
			
			/* If rr_current points to this node, advance it */
			if (rr_current == current)
			{
				rr_current = current->next;
				if (!rr_current && rr_head)
					rr_current = rr_head;  /* Wrap to head if needed */
			}
			
			/* Free scheduler node */
			kfree(current);
			rr_irq_restore(irq_flags);
			return;
		}
		
		prev = current;
		current = current->next;
		
		/* Prevent infinite loop if queue is corrupted */
		if (current == rr_head)
			break;
	}

	rr_irq_restore(irq_flags);
}

/**
 * rr_schedule_next - Select and switch to next process in round-robin order
 */
void rr_schedule_next(void)
{
	process_t *next = NULL;
	uint64_t irq_flags = rr_irq_save();
	int attempts = 0;

	if (!rr_head)
	{
		rr_irq_restore(irq_flags);
		return;
	}

	if (!rr_current)
		rr_current = rr_head;
	else
		rr_current = rr_current->next ? rr_current->next : rr_head;

	while (rr_current && attempts < RR_MAX_PICK_ATTEMPTS)
	{
		next = rr_current->process;

		if (next && next->state != PROCESS_ZOMBIE && next->state != PROCESS_BLOCKED)
			break;

		rr_current = rr_current->next ? rr_current->next : rr_head;
		attempts++;
	}

	if (!rr_current || !next || next->state == PROCESS_ZOMBIE ||
	    next->state == PROCESS_BLOCKED)
	{
		rr_irq_restore(irq_flags);
		return;
	}

	BUG_ON(!next);
	rr_irq_restore(irq_flags);
	sched_context_switch_to(next);
}

int rr_count_runnable(void)
{
	rr_task_t *walk;
	int count = 0;
	uint64_t irq_flags;

	if (!rr_head)
		return 0;

	irq_flags = rr_irq_save();
	walk = rr_head;
	do
	{
		if (walk->process &&
		    (walk->process->state == PROCESS_READY ||
		     walk->process->state == PROCESS_RUNNING))
			count++;
		walk = walk->next;
	} while (walk && walk != rr_head);

	rr_irq_restore(irq_flags);
	return count;
}

void rr_promote_process(process_t *proc)
{
	rr_task_t *walk;
	uint64_t irq_flags;

	if (!proc || !rr_head)
		return;

	irq_flags = rr_irq_save();
	for (walk = rr_head; walk; walk = walk->next)
	{
		if (walk->process == proc)
		{
			rr_current = walk;
			process_set_sched_state(proc, PROCESS_READY);
			break;
		}
		if (walk->next == rr_head)
			break;
	}
	rr_irq_restore(irq_flags);
}

const struct ir0_sched_ops ir0_rr_sched_ops = {
	.add = rr_add_process,
	.remove = rr_remove_process,
	.schedule_next = rr_schedule_next,
	.count_runnable = rr_count_runnable,
	.promote = rr_promote_process,
};
