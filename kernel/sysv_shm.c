/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sysv_shm.c
 * Description: SysV shmget/shmat/shmdt/shmctl MVP for MIT-SHM prep.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sysv_shm.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/arch_port.h>
#include <string.h>

#define SHM_MAX 8
#define SHM_MAX_PAGES 1024
#define SHM_ATTACH_MAX 16
#define PAGE_SZ 4096u

struct shm_attach
{
	int used;
	process_t *proc;
	uintptr_t va;
	size_t len;
};

struct shm_seg
{
	int in_use;
	int key;
	int id;
	int marked_rmid;
	size_t npages;
	uintptr_t *frames;
	int nattch;
	struct shm_attach attaches[SHM_ATTACH_MAX];
};

static struct shm_seg g_shm[SHM_MAX];
static int g_next_id = 1;

static struct shm_seg *shm_by_id(int shmid)
{
	int i;

	for (i = 0; i < SHM_MAX; i++)
	{
		if (g_shm[i].in_use && g_shm[i].id == shmid)
			return &g_shm[i];
	}
	return NULL;
}

static void shm_free_frames(struct shm_seg *s)
{
	size_t i;

	if (!s || !s->frames)
		return;
	for (i = 0; i < s->npages; i++)
	{
		if (s->frames[i])
			pmm_free_frame(s->frames[i]);
	}
	kfree(s->frames);
	s->frames = NULL;
	s->npages = 0;
}

static void shm_try_destroy(struct shm_seg *s)
{
	if (!s || !s->marked_rmid || s->nattch > 0)
		return;
	shm_free_frames(s);
	memset(s, 0, sizeof(*s));
}

int64_t sys_shmget(int key, size_t size, int shmflg)
{
	int i;
	struct shm_seg *free_slot = NULL;
	size_t npages;
	size_t p;

	if (size == 0 || size > (size_t)SHM_MAX_PAGES * PAGE_SZ)
		return -EINVAL;
	npages = (size + PAGE_SZ - 1) / PAGE_SZ;

	if (key != IR0_IPC_PRIVATE)
	{
		for (i = 0; i < SHM_MAX; i++)
		{
			if (g_shm[i].in_use && g_shm[i].key == key)
			{
				if ((shmflg & IR0_IPC_CREAT) && (shmflg & IR0_IPC_EXCL))
					return -EEXIST;
				return g_shm[i].id;
			}
		}
		if (!(shmflg & IR0_IPC_CREAT))
			return -ENOENT;
	}

	for (i = 0; i < SHM_MAX; i++)
	{
		if (!g_shm[i].in_use)
		{
			free_slot = &g_shm[i];
			break;
		}
	}
	if (!free_slot)
		return -ENOSPC;

	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->frames = kmalloc(npages * sizeof(uintptr_t));
	if (!free_slot->frames)
		return -ENOMEM;
	memset(free_slot->frames, 0, npages * sizeof(uintptr_t));
	for (p = 0; p < npages; p++)
	{
		uintptr_t phys = pmm_alloc_frame();
		uint64_t old_cr3;

		if (!phys)
		{
			shm_free_frames(free_slot);
			return -ENOMEM;
		}
		free_slot->frames[p] = phys;
		old_cr3 = paging_current_address_space();
		/* Zero via temporary identity-style map in kernel CR3 if needed:
		 * frames are low phys; clear through direct map when available. */
		memset((void *)(uintptr_t)phys, 0, PAGE_SZ);
		(void)old_cr3;
	}
	free_slot->in_use = 1;
	free_slot->key = key;
	free_slot->id = g_next_id++;
	free_slot->npages = npages;
	return free_slot->id;
}

int64_t sys_shmat(int shmid, const void *shmaddr, int shmflg)
{
	struct shm_seg *s;
	uintptr_t va;
	size_t p;
	int ai;
	uint64_t *pml4;
	size_t len;

	(void)shmflg;
	if (!current_process)
		return -ESRCH;
	if (shmaddr != NULL)
		return -EINVAL;
	s = shm_by_id(shmid);
	if (!s || !s->frames)
		return -EINVAL;

	pml4 = process_pgd(current_process);
	len = s->npages * PAGE_SZ;
	va = 0x70000000UL + (uintptr_t)((unsigned)shmid * 0x01000000UL) +
	     (uintptr_t)((unsigned)s->nattch * 0x00100000UL);
	for (p = 0; p < s->npages; p++)
	{
		if (map_page_in_directory(pml4, va + p * PAGE_SZ, s->frames[p],
					  PAGE_USER | PAGE_RW) != 0)
		{
			while (p > 0)
			{
				p--;
				unmap_page_in_directory(pml4, va + p * PAGE_SZ);
			}
			return -ENOMEM;
		}
	}
	for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
	{
		if (!s->attaches[ai].used)
		{
			s->attaches[ai].used = 1;
			s->attaches[ai].proc = current_process;
			s->attaches[ai].va = va;
			s->attaches[ai].len = len;
			s->nattch++;
			return (int64_t)va;
		}
	}
	for (p = 0; p < s->npages; p++)
		unmap_page_in_directory(pml4, va + p * PAGE_SZ);
	return -ENOSPC;
}

int64_t sys_shmdt(const void *shmaddr)
{
	uintptr_t va = (uintptr_t)shmaddr;
	int i;
	int ai;
	size_t off;

	if (!current_process || !shmaddr)
		return -EINVAL;
	for (i = 0; i < SHM_MAX; i++)
	{
		struct shm_seg *s = &g_shm[i];

		if (!s->in_use)
			continue;
		for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
		{
			if (!s->attaches[ai].used ||
			    s->attaches[ai].proc != current_process ||
			    s->attaches[ai].va != va)
				continue;
			for (off = 0; off < s->attaches[ai].len; off += PAGE_SZ)
				unmap_page_in_directory(process_pgd(current_process),
							va + off);
			s->attaches[ai].used = 0;
			s->nattch--;
			shm_try_destroy(s);
			return 0;
		}
	}
	return -EINVAL;
}

int64_t sys_shmctl(int shmid, int cmd, void *buf)
{
	struct shm_seg *s = shm_by_id(shmid);
	struct ir0_shmid_ds st;

	if (!s)
		return -EINVAL;
	if (cmd == IR0_IPC_RMID)
	{
		s->marked_rmid = 1;
		shm_try_destroy(s);
		return 0;
	}
	if (cmd == IR0_IPC_STAT)
	{
		if (!buf)
			return -EFAULT;
		memset(&st, 0, sizeof(st));
		st.shm_segsz = s->npages * PAGE_SZ;
		st.shm_nattch = s->nattch;
		st.shm_perm_mode = 0600;
		if (copy_to_user(buf, &st, sizeof(st)) != 0)
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}
