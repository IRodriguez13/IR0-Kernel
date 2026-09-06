/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: create.c
 * Description: spawn/spawn_user/spawn_kernel and kernel stack allocation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <mm/pmm.h>
#include <ir0/mm.h>
#include <ir0/paging.h>
#include <ir0/tlb.h>

static uint32_t ir0_kstack_slot_next;

/*
 * spawn() creates a new process with an explicit mode (kernel vs user).
 * POSIX fork()/clone() live in fork.c: they build the child with
 * fork_process_create() and domain clones (mm, files, ...). They do not
 * call spawn() internally.
 */
int process_kernel_stack_alloc(process_t *p)
{
	uint64_t *pml4;
	uint32_t slot;
	uintptr_t va;
	size_t off;
	size_t mapped;

	if (!p)
		return -EINVAL;
	if (p->kstack_base)
		return 0;

#if defined(__x86_64__) || defined(__amd64__)
	/*
	 * Linux thread stack model: dedicated supervisor VA outside the low
	 * identity / user-brk window so huge-PDE breaks can use pte_none
	 * without #DF on TSS.RSP0.
	 *
	 * Map into the pinned boot/kernel PML4 only. mm_copy_kernel_half
	 * shares those high PML4 slots by reference, so every process CR3 sees
	 * every kstack. Required: switch_context_x64 loads next CR3 while still
	 * on prev's RSP (same as Linux — kernel stacks live in shared kernel VA).
	 */
	{
		uint64_t kcr3 = paging_get_kernel_cr3();

		if (!kcr3)
			kcr3 = get_current_page_directory();
		pml4 = kcr3 ? (uint64_t *)(uintptr_t)kcr3 : NULL;
	}
	if (!pml4)
		return -ENOMEM;

	slot = __sync_fetch_and_add(&ir0_kstack_slot_next, 1u);
	if (slot >= IR0_KSTACK_MAX_SLOTS)
		return -ENOMEM;

	/* Bottom page of the slot is an unmapped guard. */
	va = (uintptr_t)IR0_KSTACK_VA_BASE +
	     (uintptr_t)slot * (uintptr_t)IR0_KSTACK_SLOT_SIZE +
	     (uintptr_t)PAGE_SIZE_4KB;

	mapped = 0;
	/*
	 * Map stack + one page above kstack_top (boundary touch at empty RSP).
	 */
	for (off = 0; off < (size_t)IR0_PROC_KSTACK_SIZE + PAGE_SIZE_4KB;
	     off += PAGE_SIZE_4KB)
	{
		uintptr_t phys = pmm_alloc_frame();

		if (!phys)
			goto rollback;
		/* Poison so ktm_stack_peak_used can measure real usage. */
		paging_poison_phys_page(phys, IR0_KSTACK_POISON);
		if (map_page_in_directory(pml4, va + off, phys, PAGE_RW) != 0)
		{
			pmm_free_frame(phys);
			goto rollback;
		}
		mapped = off + PAGE_SIZE_4KB;
	}
	/*
	 * Flush via CR3 reload on the boot root — never invlpg each new
	 * kstack VA while RSP may sit in the same 4K page (post-ash #DF).
	 */
	{
		uint64_t saved = get_current_page_directory();

		if (saved != (uint64_t)(uintptr_t)pml4)
			load_page_directory((uint64_t)(uintptr_t)pml4);
		tlb_invalidate_all();
		if (saved != (uint64_t)(uintptr_t)pml4)
			load_page_directory(saved);
	}

	/*
	 * Process mm may have been created before PML4[kstack] existed on the
	 * boot root. Re-share present kernel-half slots so switch_context_x64
	 * (CR3 swap while still on prev RSP) and TSS.RSP0 keep working.
	 * Broadcast to every process mm: new PTEs live in the shared PDPT, but
	 * mms that never linked PML4[kstack] still need the root slot.
	 */
	{
		process_t *it;
		uint64_t irqf = process_irq_save();

		for (it = process_list; it; it = it->next)
		{
			uint64_t *proc_pml4 = process_pgd(it);

			if (proc_pml4 && proc_pml4 != pml4)
				mm_copy_kernel_half(proc_pml4, pml4);
		}
		process_irq_restore(irqf);
	}

	p->kstack_base = (void *)va;
	p->kstack_top = va + (uint64_t)IR0_PROC_KSTACK_SIZE;
	p->saved_user_rsp = 0;
	return 0;

rollback:
	for (off = 0; off < mapped; off += PAGE_SIZE_4KB)
		(void)unmap_page_in_directory(pml4, va + off);
	return -ENOMEM;
#else
	{
		void *base = kmalloc_aligned_try(IR0_PROC_KSTACK_SIZE, 16);

		if (!base)
			return -ENOMEM;
		memset(base, 0, IR0_PROC_KSTACK_SIZE);
		p->kstack_base = base;
		p->kstack_top = (uint64_t)(uintptr_t)base + IR0_PROC_KSTACK_SIZE;
		p->saved_user_rsp = 0;
		return 0;
	}
#endif
}

void process_kernel_stack_free(process_t *p)
{
	uint64_t *pml4;
	uintptr_t va;
	size_t off;

	if (!p || !p->kstack_base)
		return;

#if defined(__x86_64__) || defined(__amd64__)
	{
		uint64_t kcr3 = paging_get_kernel_cr3();

		if (!kcr3)
			kcr3 = get_current_page_directory();
		pml4 = kcr3 ? (uint64_t *)(uintptr_t)kcr3 : NULL;
	}
	va = (uintptr_t)p->kstack_base;
	if (pml4)
	{
		size_t span = (size_t)IR0_PROC_KSTACK_SIZE + PAGE_SIZE_4KB;

		for (off = 0; off < span; off += PAGE_SIZE_4KB)
		{
			/* unmap_page_in_directory already invlpgs; skip live page. */
			(void)unmap_page_in_directory(pml4, va + off);
		}
	}
#else
	kfree_aligned(p->kstack_base);
#endif
	p->kstack_base = NULL;
	p->kstack_top = 0;
	p->saved_user_rsp = 0;
}

pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode)
{
	process_t *proc;
	
	if (!entry || !name)
		return -1;

	klog_debug_fmt("KERN", "SERIAL: spawn: begin %s", name);
	
	proc = kmalloc_try(sizeof(process_t));
	if (!proc) {
		klog_debug("KERN", "[ERROR] Failed to allocate process structure\n");
		return -ENOMEM;
	}

	memset(proc, 0, sizeof(process_t));

	/* Default unlimited resource limits (Linux RLIM_INFINITY = ~0ULL). */
	for (int ri = 0; ri < IR0_RLIM_NLIMITS; ri++)
	{
		proc->rlimits[ri].rlim_cur = (uint64_t)-1;
		proc->rlimits[ri].rlim_max = (uint64_t)-1;
	}
	/* Soft NOFILE matches MAX_FDS_PER_PROCESS. */
	proc->rlimits[7].rlim_cur = MAX_FDS_PER_PROCESS; /* RLIMIT_NOFILE */
	proc->rlimits[7].rlim_max = MAX_FDS_PER_PROCESS;
	/* RLIMIT_STACK soft 8 MiB (Linux-like default). */
	proc->rlimits[3].rlim_cur = 8ULL * 1024ULL * 1024ULL;
	proc->rlimits[3].rlim_max = (uint64_t)-1;
	/* RLIMIT_CORE soft 0 (no dump by default). */
	proc->rlimits[4].rlim_cur = 0;
	proc->rlimits[4].rlim_max = (uint64_t)-1;
	/* RLIMIT_AS remains unlimited soft/hard. */

	/* Basic process setup */
	proc->task.pid = process_get_next_pid();
	proc->tgid = proc->task.pid;
	proc->sid = proc->task.pid;
	proc->pgid = proc->task.pid;
	proc->ppid = current_process ? current_process->task.pid : 0;
	proc->start_ticks = clock_get_tick_count();
	process_set_sched_state(proc, PROCESS_READY);
	proc->sched_prio = IR0_SCHED_PRIO_DEFAULT;
	proc->sched_nice = 0;
	proc->personality = 0; /* PER_LINUX */

	/* Explicit mode specification - no magic address detection */
	proc->mode = mode;

	/*
	 * Kernel idle reuses active kernel CR3 (boot/kmain tables).  Avoids
	 * remapping ~48MB of supervisor pages on every idle spawn (slow + PMM).
	 * Lowest sched band so priority pick never starves userspace (IRQ
	 * preempt is ring-3-only today).
	 */
	{
		mm_struct_t *mm = mm_create();

		if (!mm)
		{
			kfree(proc);
			return -ENOMEM;
		}

		if (mode == KERNEL_MODE && name && strcmp(name, "idle") == 0)
		{
			uint64_t kcr3 = get_current_page_directory();

			proc->sched_prio = 0;
			mm->page_directory = (uint64_t *)kcr3;
			mm->owns_tables = 0;
			process_mm_bind(proc, mm);
			process_set_mm_root(proc, kcr3);
			klog_debug("KERN", "SERIAL: spawn: kernel CR3 shared (idle)\n");
		}
		else
		{
			mm->page_directory = (uint64_t *)create_process_page_directory();
			if (!mm->page_directory)
			{
				mm_put(mm);
				klog_debug("KERN", "[ERROR] Failed to create page directory for process\n");
				kfree(proc);
				return -ENOMEM;
			}
			mm->owns_tables = 1;
			process_mm_bind(proc, mm);
			klog_debug("KERN", "SERIAL: spawn: page directory OK\n");
			process_set_mm_root(proc, (uint64_t)(uintptr_t)process_pgd(proc));
		}
	}

	/* Inherit permissions from current process or default to root */
	if (current_process) {
		proc->uid = current_process->uid;
		proc->gid = current_process->gid;
		proc->euid = current_process->euid;
		proc->egid = current_process->egid;
		proc->suid = current_process->suid;
		proc->sgid = current_process->sgid;
		proc->no_new_privs = current_process->no_new_privs;
		proc->at_secure = current_process->at_secure;
		proc->umask = current_process->umask;
		strncpy(proc->cwd, current_process->cwd, sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
		strncpy(proc->root, current_process->root, sizeof(proc->root) - 1);
		proc->root[sizeof(proc->root) - 1] = '\0';
	} else {
		proc->uid = ROOT_UID;
		proc->gid = ROOT_GID;
		proc->euid = ROOT_UID;
		proc->egid = ROOT_GID;
		proc->suid = ROOT_UID;
		proc->sgid = ROOT_GID;
		proc->no_new_privs = 0;
		proc->at_secure = 0;
		proc->umask = DEFAULT_UMASK;
		strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
		strncpy(proc->root, "/", sizeof(proc->root) - 1);
		proc->root[sizeof(proc->root) - 1] = '\0';
	}
	process_cred_init_groups(proc);
	if (proc->cwd[0] != '/')
	{
		strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
	}
	if (proc->root[0] != '/')
	{
		strncpy(proc->root, "/", sizeof(proc->root) - 1);
		proc->root[sizeof(proc->root) - 1] = '\0';
	}
	
	/* Set command name */
	strncpy(proc->comm, name, sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';

	/* Create user stack in userspace (only for USER_MODE processes) */
	if (proc->mode == USER_MODE)
	{
		/* User stack: [USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP) */
		process_set_stack_layout(proc, USER_STACK_TOP - USER_STACK_SIZE,
					 USER_STACK_SIZE);

		/*
		 * Map under kernel CR3: map_user_region_in_directory() allocates page
		 * tables from the kernel heap and must not run with child CR3 active.
		 */
		if (map_user_region_in_directory(process_pgd(proc), process_stack_start(proc), process_stack_size(proc), PAGE_RW) != 0)
		{
			klog_debug("KERN", "SERIAL: spawn: stack map failed\n");
			process_unmap_user_pages_all(process_pgd(proc), NULL);
			goto fail_proc;
		}
		klog_debug("KERN", "SERIAL: spawn: stack mapped\n");

		/* Setup stack pointer just below USER_STACK_TOP (stack grows down) */
		task_set_sp(&proc->task, USER_STACK_TOP - 16);
		task_set_frame_pointer(&proc->task, task_get_sp(&proc->task));
	}
	else
	{
		/* Kernel mode: allocate from kernel heap (existing behavior) */
		void *kstack = kmalloc_try(0x2000);

		if (!kstack)
		{
			if (process_mm_owns_tables(proc))
				process_unmap_user_pages_all(process_pgd(proc), NULL);
			goto fail_proc;
		}
		process_set_stack_layout(proc, (uint64_t)(uintptr_t)kstack, 0x2000);
		memset(kstack, 0, 0x2000);
		task_set_sp(&proc->task, process_stack_start(proc) + process_stack_size(proc) - 16);
		task_set_frame_pointer(&proc->task, task_get_sp(&proc->task));
	}

	/* Setup task registers for clean start */
	task_set_ip(&proc->task, (uint64_t)entry);
	if (proc->mode == USER_MODE)
		task_set_flags(&proc->task, ir0_rflags_sanitize_user(RFLAGS_IF));
	else
		task_set_flags(&proc->task, RFLAGS_IF);
	if (proc->mode == KERNEL_MODE)
		task_set_kernel_segments(&proc->task);
	else
		task_set_user_segments(&proc->task);

	/* Initialize file descriptor table */
	process_init_fd_table(proc);

	/* Initialize signal handlers to default */
	proc->signal_pending = 0;
	proc->signal_mask = 0;
	proc->signal_ignored = 0;
	process_saved_context_init(proc);
	process_signal_enter_pending_init(proc);
	process_signal_defer_catchable_clear(proc);
	process_signal_last_delivered_clear(proc);
	process_kernel_sleep_interrupted_clear(proc);
	for (int i = 0; i < _NSIG; i++)
	{
		proc->signal_handlers[i] = SIG_DFL;
		proc->signal_sa_flags[i] = 0;
		proc->signal_sa_mask[i] = 0;
		proc->signal_restorer[i] = NULL;
	}
	proc->robust_list = NULL;

	/* Private kernel stack for syscall/IRQ entry (see IR0_PROC_KSTACK_SIZE). */
	if (process_kernel_stack_alloc(proc) != 0)
	{
		klog_debug("KERN", "SERIAL: spawn: kernel stack alloc failed\n");
		if (proc->mode != USER_MODE && process_stack_start(proc))
		{
			kfree((void *)(uintptr_t)process_stack_start(proc));
			process_set_stack_layout(proc, 0, 0);
		}
		else if (process_mm_owns_tables(proc))
		{
			process_unmap_user_pages_all(process_pgd(proc), NULL);
		}
		goto fail_proc;
	}

	/* Add to process list */
	{
		uint64_t irq_flags = process_irq_save();
		proc->next = process_list;
		process_list = proc;
		process_irq_restore(irq_flags);
	}

	fase_audit_note_proc_created();
#if IR0_DEBUG_PROC
	process_fase43_proc_audit("spawn-after");
	fase_audit_spawn_init(proc);
	fase_audit_trace_pid(proc->task.pid, "CREATED");
	fase_audit_ref_emit(proc, "spawn");
	process_fase44_list_checkpoint("spawn-after");
#endif

	/* Add to scheduler */
	sched_add_process(proc);

	return proc->task.pid;

fail_proc:
	process_kernel_stack_free(proc);
	if (process_stack_start(proc) && proc->mode == KERNEL_MODE)
	{
		kfree((void *)(uintptr_t)process_stack_start(proc));
		process_set_stack_layout(proc, 0, 0);
	}
	if (proc->mm)
	{
		mm_put(proc->mm);
		proc->mm = NULL;
	}
	kfree(proc);
	return -ENOMEM;
}

/* Convenience wrapper for user-mode processes */
pid_t spawn_user(void (*entry)(void), const char *name)
{
	return spawn(entry, name, USER_MODE);
}

/* Convenience wrapper for kernel-mode processes */
pid_t spawn_kernel(void (*entry)(void), const char *name)
{
	return spawn(entry, name, KERNEL_MODE);
}

