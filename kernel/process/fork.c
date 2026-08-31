/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fork.c
 * Description: Portable POSIX fork / clone_thread — transactional, ISA-agnostic.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/clone.h>
#include <ir0/arch_fork.h>
#include <ir0/arch_task.h>
#include <ir0/process_domains.h>
#include <ir0/mm_struct.h>
#include <ir0/files_struct.h>
#include <ir0/ktm/checkpoint.h>
#include <ir0/ktm/fault.h>
#include <string.h>

/*
 * Transactional child construction — no memcpy(process_t). Each domain is
 * cloned explicitly; resource handles start NULL until their phase succeeds.
 */
static process_t *fork_process_create(process_t *parent, pid_t *child_pid_out)
{
	process_t *child;
	pid_t child_pid;

	if (!parent || !child_pid_out)
		return NULL;

	if (KTM_FAULT_HIT("process.fork_alloc"))
		return NULL;

	child = kmalloc_try(sizeof(process_t));
	if (!child)
		return NULL;

	memset(child, 0, sizeof(*child));

	child_pid = process_get_next_pid();
	child->task.priority = parent->task.priority;
	child->task.state = TASK_READY;
	arch_task_context_clone(&child->task.arch, &parent->task.arch);
	child->start_ticks = clock_get_tick_count();
	process_tls_set(child, process_tls_get(parent));

	if (process_rel_init_child(child, parent, child_pid) < 0 ||
	    process_cred_clone(child, parent) < 0 ||
	    process_session_attrs_clone(child, parent) < 0 ||
	    process_signals_clone(child, parent) < 0 ||
	    process_mm_cursor_clone(child, parent) < 0)
	{
		kfree(child);
		return NULL;
	}

	child->files = NULL;
	child->set_tid_ptr = NULL;
	child->next = NULL;
	child->poll_waiter = NULL;
	child->fork_pending_child = NULL;
	child->kstack_base = NULL;
	child->kstack_top = 0;
	child->saved_user_rsp = 0;

	/* Kstack mapped after fork_child_mm_create (needs child PML4). */

	*child_pid_out = child_pid;
	return child;
}

static int fork_child_mm_create(process_t *child, process_t *parent)
{
	mm_struct_t *mm;

	if (!child || !parent)
		return -ENOMEM;

	if (KTM_FAULT_HIT("process.fork_mm"))
		return -ENOMEM;

	mm = mm_create();
	if (!mm)
		return -ENOMEM;

	mm->page_directory = (uint64_t *)create_process_page_directory();
	if (!mm->page_directory)
	{
		mm_put(mm);
		return -ENOMEM;
	}

	mm->owns_tables = 1;
	if (parent->mm)
	{
		mm->mmap_base = parent->mm->mmap_base;
		mm->heap_start = parent->mm->heap_start;
		mm->heap_end = parent->mm->heap_end;
		mm->stack_start = parent->mm->stack_start;
		mm->stack_size = parent->mm->stack_size;
	}
	process_mm_bind(child, mm);
	process_set_mm_root(child, (uint64_t)(uintptr_t)process_pgd(child));

	if (KTM_FAULT_HIT("process.fork_page_directory"))
	{
		process_fork_destroy_child_mm(child);
		return -ENOMEM;
	}
	return 0;
}

static int fork_attach_pending_child(process_t *child, process_t *parent)
{
	uint64_t irq_flags;

	if (!child || !parent)
		return -EINVAL;

	if (KTM_FAULT_HIT("process.fork_enqueue"))
		return -ENOMEM;

	irq_flags = process_irq_save();
	child->next = process_list;
	process_list = child;
	process_irq_restore(irq_flags);

	process_set_sched_state(child, PROCESS_BLOCKED);
	parent->fork_pending_child = child;
	return 0;
}

void process_fork_wake_pending(process_t *parent)
{
	process_t *child;

	if (!parent)
		return;

	child = parent->fork_pending_child;
	if (!child)
		return;

	parent->fork_pending_child = NULL;
	process_set_sched_state(child, PROCESS_READY);
	sched_add_process(child);
}

static void fork_rollback(process_t *child, pid_t child_pid, int enqueued)
{
	(void)child_pid;

	if (!child)
		return;

	if (enqueued)
	{
		process_t *parent = current_process;

		if (parent && parent->fork_pending_child == child)
			parent->fork_pending_child = NULL;

		if (child->state == PROCESS_READY || child->state == PROCESS_RUNNING)
			sched_remove_process(child);
		(void)process_remove_from_list(child);
	}

	process_release_fds(child, "FORK_ROLLBACK");
	if (child->files)
	{
		files_put(child->files);
		child->files = NULL;
	}
	process_fork_destroy_child_mm(child);
	process_fork_free_mmap_list(child);
	process_kernel_stack_free(child);
	kfree(child);
}

/*
 * fork() — clone address space (COW), files, and arrange parent/child returns
 * via arch_fork_* hooks. Child is not runnable until process_fork_wake_pending.
 */
pid_t fork(void)
{
	process_t *parent = current_process;
	process_t *child;
	pid_t child_pid;
	int ret;

	if (!parent)
		return -1;

	child = fork_process_create(parent, &child_pid);
	if (!child)
		return -ENOMEM;

	if (fork_child_mm_create(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (process_kernel_stack_alloc(child) != 0 ||
	    KTM_FAULT_HIT("process.fork_kstack"))
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (KTM_FAULT_HIT("process.fork_cow") || copy_process_memory(parent, child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	{
		struct mmap_region *mmap_list;

		if (KTM_FAULT_HIT("process.fork_mmap_clone"))
		{
			fork_rollback(child, child_pid, 0);
			return -ENOMEM;
		}

		mmap_list = process_clone_mmap_list(process_mmap_list(parent));
		if (process_mmap_list(parent) && !mmap_list)
		{
			fork_rollback(child, child_pid, 0);
			return -ENOMEM;
		}
		process_mm_set_mmap_list(child, mmap_list);
		{
			const struct mmap_region *r;
			unsigned n = 0;

			for (r = mmap_list; r; r = r->next)
				n++;
			klog_debug_fmt("FORK", "vma clone child=%x parent=%x n=%x\n",
				       (unsigned)((uint32_t)child_pid),
				       (unsigned)((uint32_t)parent->task.pid), n);
		}
	}

	if (KTM_FAULT_HIT("process.fork_files") ||
	    process_files_clone(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	process_set_mm_root(child, (uint64_t)(uintptr_t)process_pgd(child));
	child->task.pid = child_pid;
	process_tls_set(child, process_tls_get(parent));

	/*
	 * Child ISA setup before visibility. Parent return must NOT run until
	 * attach succeeds — otherwise enqueue fault leaves parent irreversibly
	 * mutated (§5).
	 */
	if (parent->mode == USER_MODE)
	{
		if (KTM_FAULT_HIT("process.fork_arch_child"))
		{
			fork_rollback(child, child_pid, 0);
			return -ENOMEM;
		}
		ret = arch_fork_prepare_child_return(child, parent);
		if (ret < 0)
		{
			fork_rollback(child, child_pid, 0);
			return ret;
		}
	}

	if (fork_attach_pending_child(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (parent->mode == USER_MODE)
	{
		ret = arch_fork_prepare_parent_return(parent, child_pid);
		if (ret < 0)
		{
			fork_rollback(child, child_pid, 1);
			return ret;
		}
	}

	KTM_CHECKPOINT(KTM_CP_PROCESS_FORK);
	return child_pid;
}

/*
 * clone_thread — CLONE_VM|CLONE_THREAD (+ optional CLONE_FILES / SETTLS / tid).
 */
pid_t clone_thread(unsigned long flags, void *stack, int *parent_tid,
		   int *child_tid, unsigned long tls)
{
	process_t *parent = current_process;
	process_t *child;
	pid_t child_pid;
	uintptr_t child_sp;

	if (!parent)
		return -ESRCH;
	if (!(flags & CLONE_THREAD) || !(flags & CLONE_VM))
		return -EINVAL;
	if (!stack)
		return -EINVAL;

	child = fork_process_create(parent, &child_pid);
	if (!child)
		return -ENOMEM;

	if (process_mm_share(child, parent) < 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}
	process_set_mm_root(child, process_mm_root(parent));
	child->tgid = parent->tgid;
	child->ppid = parent->task.pid;

	if (process_kernel_stack_alloc(child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (flags & CLONE_SETTLS)
	{
		int tls_ret = arch_process_set_tls(child, tls);

		if (tls_ret < 0 && tls_ret != -EOPNOTSUPP)
		{
			fork_rollback(child, child_pid, 0);
			return tls_ret;
		}
		if (tls_ret == -EOPNOTSUPP)
			process_tls_set(child, tls);
	}
	else
		process_tls_set(child, process_tls_get(parent));

	if (flags & CLONE_FILES)
	{
		if (process_files_share(child, parent) != 0)
		{
			fork_rollback(child, child_pid, 0);
			return -ENOMEM;
		}
	}
	else if (process_files_clone(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	child_sp = (uintptr_t)stack;
	child->task.pid = child_pid;
	task_set_sp(&child->task, (uint64_t)child_sp);
	arch_task_clear_frame_pointer(&child->task);

	if (parent->mode == USER_MODE)
	{
		int cret;

		if (KTM_FAULT_HIT("process.fork_arch_child"))
		{
			fork_rollback(child, child_pid, 0);
			return -ENOMEM;
		}
		cret = arch_fork_prepare_child_return(child, parent);
		if (cret < 0)
		{
			fork_rollback(child, child_pid, 0);
			return cret;
		}
	}

	if (flags & CLONE_PARENT_SETTID && parent_tid)
	{
		int tid = (int)child_pid;

		if (process_validate_userspace_buffer(parent_tid, sizeof(tid)) == 0)
			(void)copy_to_user(parent_tid, &tid, sizeof(tid));
	}
	if (flags & CLONE_CHILD_SETTID && child_tid)
	{
		int tid = (int)child_pid;

		if (process_validate_userspace_buffer(child_tid, sizeof(tid)) == 0)
			(void)copy_to_user(child_tid, &tid, sizeof(tid));
	}
	if (flags & CLONE_CHILD_CLEARTID && child_tid)
		child->set_tid_ptr = child_tid;

	if (fork_attach_pending_child(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (parent->mode == USER_MODE)
	{
		int pret = arch_fork_prepare_parent_return(parent, child_pid);

		if (pret < 0)
		{
			fork_rollback(child, child_pid, 1);
			return pret;
		}
	}

	return child_pid;
}
