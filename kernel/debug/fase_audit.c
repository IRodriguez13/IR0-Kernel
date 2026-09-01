/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fase_audit.c
 * Description: FASE43–48 bring-up audit module (IR0_DEBUG_PROC)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "debug/fase_audit.h"
#include "process.h"
#include <config.h>
#include <ir0/arch_port.h>
#include <ir0/debug_runtime.h>
#include <ir0/kmem.h>
#include <ir0/ktm/event.h>
#include <ir0/paging.h>
#include <ir0/pipe.h>
#include <ir0/pmm.h>
#include <ir0/serial_io.h>
#include <string.h>

extern void fd_slot_stats_get(uint64_t *created, uint64_t *destroyed,
			      uint64_t *blocked_readers, uint64_t *blocked_writers);

#if IR0_DEBUG_PROC


#define FASE_AUDIT_SLOTS 128

static int fase_audit_slot_count;

fase_proc_audit_t *fase_audit_get(process_t *p, int create)
{
	int i;

	if (!p)
		return NULL;

	for (i = 0; i < fase_audit_slot_count; i++)
	{
		if (fase_audit_slots[i].proc == p)
			return &fase_audit_slots[i];
	}

	if (!create || fase_audit_slot_count >= FASE_AUDIT_SLOTS)
		return NULL;

	i = fase_audit_slot_count++;
	fase_audit_slots[i].proc = p;
	fase_audit_slots[i].fase44_audit_state = FASE44_PROC_ALIVE;
	fase_audit_slots[i].fase46_fork_generation = 0;
	fase_audit_slots[i].fase46_fork_parent_pid = 0;
	fase_audit_slots[i].fase46_entered_userspace = 0;
	fase_audit_slots[i].fase46_entered_exit = 0;
	fase_audit_slots[i].fase46_entered_wait = 0;
	return &fase_audit_slots[i];
}

void fase_audit_unbind(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < fase_audit_slot_count; i++)
	{
		if (fase_audit_slots[i].proc != p)
			continue;
		fase_audit_slot_count--;
		if (i < fase_audit_slot_count)
			fase_audit_slots[i] = fase_audit_slots[fase_audit_slot_count];
		return;
	}
}

void fase_audit_spawn_init(process_t *p)
{
	fase_proc_audit_t *a = fase_audit_get(p, 1);

	if (a)
		a->fase44_audit_state = FASE44_PROC_ALIVE;
}

void fase_audit_fork_init(process_t *child, process_t *parent)
{
	fase_proc_audit_t *ca = fase_audit_get(child, 1);
	fase_proc_audit_t *pa = parent ? fase_audit_get(parent, 0) : NULL;

	if (!ca)
		return;

	ca->fase44_audit_state = FASE44_PROC_ALIVE;
	ca->fase46_fork_generation = pa ? pa->fase46_fork_generation + 1U : 0U;
	ca->fase46_fork_parent_pid = parent ? parent->task.pid : 0;
	ca->fase46_entered_userspace = 0;
	ca->fase46_entered_exit = 0;
	ca->fase46_entered_wait = 0;
}


static uint64_t fase43_proc_created;
static uint64_t fase43_proc_exited;
static uint64_t fase43_proc_zombie;
static uint64_t fase43_proc_reaped;
static uint64_t fase43_proc_destroyed;
static uint64_t fase43_mm_created;
static uint64_t fase43_mm_destroyed;
static uint64_t fase43_reparent_events;
static uint64_t fase43_reap_events;


const char *fase_audit_state_name(int state)
{
	switch ((process_state_t)state)
	{
	case PROCESS_READY:
		return "READY";
	case PROCESS_RUNNING:
		return "RUNNING";
	case PROCESS_BLOCKED:
		return "BLOCKED";
	case PROCESS_ZOMBIE:
		return "ZOMBIE";
	default:
		return "UNKNOWN";
	}
}

static uint64_t fase43_count_vmas(process_t *p)
{
	uint64_t n = 0;
	struct mmap_region *r;

	if (!p)
		return 0;
	for (r = process_mmap_list(p); r; r = r->next)
		n++;
	return n;
}

static uint64_t fase43_count_user_frames(process_t *p)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!p || !process_pgd(p))
		return 0;

	pml4 = process_pgd(p);
	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];

					if ((ent & PAGE_PRESENT) && (ent & PAGE_USER))
						count++;
				}
			}
		}
	}
	return count;
}

static uint64_t process_fase47_count_leaves_in_range(process_t *p, uint64_t start,
						     uint64_t end)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!p || !process_pgd(p) || end <= start)
		return 0;

	pml4 = process_pgd(p);
	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];
					uint64_t virt;

					if (!(ent & PAGE_PRESENT) || !(ent & PAGE_USER))
						continue;
					virt = ((uint64_t)i4 << 39) | ((uint64_t)i3 << 30) |
					       ((uint64_t)i2 << 21) | ((uint64_t)i1 << 12);
					if (virt >= start && virt < end)
						count++;
				}
			}
		}
	}
	return count;
}

static uint64_t process_fase47_count_page_table_frames(process_t *p)
{
	size_t i4;
	size_t i3;
	size_t i2;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!p || !process_pgd(p) || !process_mm_owns_tables(p))
		return 0;

	pml4 = process_pgd(p);
	count++;
	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		count++;
		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			count++;
			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				count++;
			}
		}
	}
	return count;
}

static uint64_t process_fase47_count_mmap_pages(process_t *p)
{
	uint64_t pages = 0;
	struct mmap_region *r;

	if (!p)
		return 0;
	for (r = process_mmap_list(p); r; r = r->next)
		pages += (r->length + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
	return pages;
}

void process_fase47_mm_owner_audit(const char *tag)
{
	(void)tag;
	/* Serial FASE47 dumps retired; counters remain via note_* APIs. */
}

void process_fase43_note_mm_created(void)
{
	fase43_mm_created++;
}

void process_fase43_note_mm_destroyed(void)
{
	fase43_mm_destroyed++;
}

void process_fase43_proc_audit(const char *tag)
{
	(void)tag;
}

void process_fase43_live_proc_dump(void)
{
}

void fase_audit_ref_emit(process_t *p, const char *tag)
{
	(void)p;
	(void)tag;
}

void fase_audit_destroy_audit(process_t *p, pid_t parent, uint8_t state_before,
				 uint8_t state_after, int removed_from_list,
				 const char *tag)
{
#if !IR0_DEBUG_PROC
	(void)p;
	(void)parent;
	(void)state_before;
	(void)state_after;
	(void)removed_from_list;
	(void)tag;
	return;
#else
	if (!p)
		return;

#endif
}

void fase_audit_trace_pid(pid_t pid, const char *event)
{
#if !IR0_DEBUG_PROC
	(void)pid;
	(void)event;
	return;
#else
#endif
}

void fase_audit_reap_zombie(process_t *child, pid_t parent_pid, const char *tag)
{
	uint8_t before;
	int removed;

	if (!child)
		return;

	{
		fase_proc_audit_t *fa = fase_audit_get(child, 0);

		before = fa ? fa->fase44_audit_state : 0;
	}
	removed = process_remove_from_list(child);
	process_fase50_trace_proc("reap_zombie-removed", child);
	fase_audit_destroy_audit(child, parent_pid, before, FASE44_PROC_REAPED,
			     removed == 0, tag);
	if (removed != 0)
	{
		return;
	}

	fase_audit_trace_pid(child->task.pid, "REAP");
	{
		fase_proc_audit_t *fa = fase_audit_get(child, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_REAPED;
	}
	fase_audit_ref_emit(child, tag);
	fase43_proc_reaped++;
	fase_audit_trace_pid(child->task.pid, "DESTROY");
	process_fase50_trace_proc("reap_zombie-before-destroy", child);
	process_destroy(child);
	fase_audit_trace_pid(child->task.pid, "FREE");
	kfree(child);
}

void process_fase48_capture_fd_baseline(process_t *p)
{
	if (!fase48_fd_baseline && p)
		fase48_fd_baseline = process_count_open_fds(p);
}

void process_fase48_ipc_summary(const char *tag)
{
	uint64_t pipe_created = 0;
	uint64_t pipe_destroyed = 0;
	uint64_t fd_created = 0;
	uint64_t fd_destroyed = 0;
	uint64_t blocked_readers = 0;
	uint64_t blocked_writers = 0;
	uint64_t live_user = process_list_count_user();
	uint64_t fd_after = 0;
	process_t *init;
	int inv_pipe;
	int inv_fd;
	int inv_proc;
	const char *ipc_class;

	if (fase48_ipc_summary_done)
		return;
	fase48_ipc_summary_done = 1;

	init = process_find_by_pid(1);
	if (init)
		fd_after = process_count_open_fds(init);

	pipe_stats_get(&pipe_created, &pipe_destroyed);
	fd_slot_stats_get(&fd_created, &fd_destroyed, &blocked_readers,
			    &blocked_writers);

	if (!fase48_fd_baseline && init)
		fase48_fd_baseline = process_count_open_fds(init);

	inv_pipe = (pipe_created == pipe_destroyed);
	inv_fd = (fd_created == fd_destroyed);
	inv_proc = (fase43_proc_created == fase43_proc_destroyed + live_user);


	if (!inv_pipe || !inv_fd)
		ipc_class = "IPC_LEAK";
	else if (!inv_proc)
		ipc_class = "FD_LIFECYCLE_BROKEN";
	else
		ipc_class = "IPC_READY";

}
static uint64_t fase46_scheduled;
static uint64_t fase46_entered_userspace;
static uint64_t fase46_child_user_enter;
static uint64_t fase46_exited;
static uint64_t fase46_child_exited;
static uint64_t fase46_entered_wait;
static uint64_t fase46_child_destroyed;
static uint64_t fase46_frames_baseline;
static int fase46_frames_baseline_set;

static void process_fase47_closure_audit(const char *tag)
{
	process_fase47_mm_owner_audit(tag);
	paging_fase47_steady_state_audit(tag, fase46_frames_baseline,
					 fase43_mm_created, fase43_mm_destroyed);
}

void fase_audit_fork_state(pid_t pid, const char *stage)
{
	if (!DEBUG_FORK)
		return;
}

void fase_audit_assert_child_not_visible(pid_t pid)
{
	if (!DEBUG_FORK)
		return;
	if (process_find_by_pid(pid) != NULL)
	{
	}
}

void process_fase45_fork_audit(const char *tag)
{
	if (!DEBUG_FORK)
		return;
}

void process_fase45_summary(const char *tag)
{
	uint64_t live = process_list_count();
	uint64_t live_user = process_list_count_user();
	uint64_t baseline = fase44_baseline_count;
	int lifecycle_ok;

	if (!DEBUG_FORK)
		return;

	lifecycle_ok = (fase43_proc_created == fase43_proc_destroyed + live_user &&
			live <= baseline + 2);

	process_fase45_fork_audit(tag);
}

void process_fase46_proc_log(process_t *p, int64_t fork_ret, const char *phase)
{
	fase_proc_audit_t *fa;

	if (!p || !phase)
		return;

	if (!IR0_DEBUG_PROC)
		return;

	fa = fase_audit_get(p, 0);
	if (!fa)
		return;


	if (strcmp(phase, "USER_ENTER") == 0)
	{
		if (fa->fase46_entered_userspace)
		{
		}
		fa->fase46_entered_userspace = 1;
		fase46_entered_userspace++;
		if (fa->fase46_fork_generation > 0)
			fase46_child_user_enter++;
	}
	else if (strcmp(phase, "EXIT") == 0)
	{
		fa->fase46_entered_exit = 1;
		fase46_exited++;
		if (fa->fase46_fork_generation > 0)
			fase46_child_exited++;
	}
	else if (strcmp(phase, "DESTROY") == 0)
	{
		if (fa->fase46_fork_generation > 0)
			fase46_child_destroyed++;
	}
}

void process_fase46_note_wait(process_t *p)
{
#if !IR0_DEBUG_PROC
	(void)p;
	return;
#else
	if (!p)
		return;

	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		if (!fa || fa->fase46_entered_wait)
			return;
		fa->fase46_entered_wait = 1;
	}
	fase46_entered_wait++;
	process_fase46_proc_log(p, -1, "WAIT");
#endif
}

void process_fase46_convergence_summary(const char *tag)
{
	size_t total_frames = 0;
	size_t used_frames = 0;
	uint64_t live = process_list_count();
	uint64_t live_user = process_list_count_user();
	uint64_t baseline = fase44_baseline_count;
	int inv_scheduled;
	int inv_user_enter;
	int inv_destroyed;
	int inv_visible;
	int inv_frames;
	int convergence_ok;

	if (!IR0_DEBUG_PROC)
		return;

	if (!fase46_frames_baseline_set)
	{
		pmm_stats(&total_frames, &used_frames, NULL);
		fase46_frames_baseline = (uint64_t)used_frames;
		fase46_frames_baseline_set = 1;
	}

	pmm_stats(&total_frames, &used_frames, NULL);

	inv_scheduled = (fase46_scheduled <= fase43_proc_created);
	inv_user_enter = (fase46_entered_userspace <= fase46_scheduled);
	inv_destroyed = (fase43_proc_destroyed + live_user == fase43_proc_created);
	inv_visible = (live <= baseline + 2);
	inv_frames = (fase46_frames_baseline == 0 ||
		      (uint64_t)used_frames <= (fase46_frames_baseline * 105ULL / 100ULL));

	convergence_ok = inv_scheduled && inv_user_enter && inv_destroyed &&
			 inv_visible && inv_frames;



	if (!fase47_closure_done)
	{
		fase47_closure_done = 1;
		process_fase47_closure_audit(tag);
	}
}

static void fase44_maybe_steady_summary(const char *tag)
{
	process_t *p;
	uint64_t zombies = 0;

	if (fase44_steady_summary_done || !fase44_baseline_set)
		return;
	if (process_list_count() != fase44_baseline_count)
		return;

	for (p = process_list; p; p = p->next)
	{
		if (p->state == PROCESS_ZOMBIE)
			zombies++;
	}
	if (zombies != 0)
		return;

	fase44_steady_summary_done = 1;
	process_fase44_live_summary(tag ? tag : "steady-state");
	process_fase45_summary(tag ? tag : "steady-state");
	process_fase46_convergence_summary(tag ? tag : "steady-state");
}

void process_fase44_list_checkpoint(const char *tag)
{
#if !IR0_DEBUG_PROC
	(void)tag;
	return;
#else
	uint64_t cnt = process_list_count();

	if (!fase44_baseline_set && current_process &&
	    current_process->task.pid == 1)
	{
		fase44_baseline_count = cnt;
		fase44_baseline_set = 1;
	}

#if IR0_DEBUG_PROC
#endif

	if (tag && (strncmp(tag, "wait-after", 10) == 0 ||
		    strncmp(tag, "drain-zombie-after", 18) == 0))
	{
		fase44_maybe_steady_summary(tag);
		if (!fase47_closure_done && fase44_baseline_set &&
		    process_list_count() == fase44_baseline_count &&
		    process_list_count_user() <= 1)
		{
			fase47_closure_done = 1;
			process_fase47_closure_audit(tag);
		}
	}
#endif
}

void process_fase44_drain_zombie_children(pid_t ppid)
{
#if !IR0_DEBUG_PROC
	(void)ppid;
	return;
#else
	process_t *child;

	for (;;)
	{
		process_t *found = NULL;

		for (child = process_list; child; child = child->next)
		{
			if (child->ppid == ppid && child->state == PROCESS_ZOMBIE &&
			    child->task.pid != ppid)
			{
				found = child;
				break;
			}
		}
		if (!found)
			return;

		fase43_reap_events++;
		process_fase43_proc_audit("drain-zombie");
		fase_audit_reap_zombie(found, ppid, "drain-zombie");
		process_fase44_list_checkpoint("drain-zombie-after");
	}
#endif
}

void process_fase44_live_summary(const char *tag)
{
	(void)tag;
}

void fase_audit_note_proc_created(void)
{
	process_t *cur = process_get_current();

	fase43_proc_created++;
	ktm_event_emit4(KTM_EVENT_PROCESS_CREATE, KTM_SUBSYS_PROC,
			cur ? (uint64_t)(uint32_t)cur->task.pid : 0ULL,
			fase43_proc_created, 0ULL, 0ULL);
}
void fase_audit_note_proc_exited(void)
{
	fase43_proc_exited++;
	ktm_event_emit4(KTM_EVENT_PROCESS_EXIT, KTM_SUBSYS_PROC,
			(uint64_t)fase43_proc_exited, 0ULL, 0ULL, 0ULL);
}
void fase_audit_note_proc_zombie(void)
{
	fase43_proc_zombie++;
	ktm_event_emit4(KTM_EVENT_PROCESS_EXIT, KTM_SUBSYS_PROC,
			(uint64_t)fase43_proc_zombie, 1ULL, 0ULL, 0ULL);
}
void fase_audit_note_proc_destroyed(void)
{
	fase43_proc_destroyed++;
	ktm_event_emit4(KTM_EVENT_PROCESS_REAP, KTM_SUBSYS_PROC,
			(uint64_t)fase43_proc_destroyed, 0ULL, 0ULL, 0ULL);
}
void fase_audit_note_reparent(void)
{
	fase43_reparent_events++;
}
void fase_audit_note_reap_event(void)
{
	fase43_reap_events++;
	ktm_event_emit4(KTM_EVENT_PROCESS_REAP, KTM_SUBSYS_PROC,
			(uint64_t)fase43_reap_events, 1ULL, 0ULL, 0ULL);
}
void fase_audit_note_fork_rollback(void) { fase45_fork_rollback++; }
void fase_audit_note_scheduled(void) { fase46_scheduled++; }

static uint64_t fase_audit_count_open_fds(process_t *p)
{
	uint64_t n = 0;
	fd_entry_t *fdt;
	int i;

	if (!p)
		return 0;
	fdt = process_fd_table(p);
	if (!fdt)
		return 0;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		if (fdt[i].in_use)
			n++;
	}
	return n;
}

uint64_t process_count_open_fds(process_t *p)
{
	return fase_audit_count_open_fds(p);
}

#endif /* IR0_DEBUG_PROC */
