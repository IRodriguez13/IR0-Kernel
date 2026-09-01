/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: memfd.c
 * Description: Anonymous in-memory file for MAP_SHARED + SCM_RIGHTS.
 * And dont forget this is MDF!!
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/memfd.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/fcntl.h>
#include <ir0/arch_cpu.h>
#include <string.h>

extern fd_entry_t *get_process_fd_table(void);
extern void fd_slot_note_created(void);

#define MEMFD_MAX 16
#define MEMFD_MAX_PAGES 1024
#define PAGE_SZ 4096u
#define MEMFD_MAGIC 0x4D464421u /* MFD! */

struct ir0_memfd
{
	uint32_t magic;
	int in_use;
	int refs;
	size_t size;
	size_t npages;
	uintptr_t *frames;
	char name[64];
};

static struct ir0_memfd g_memfd[MEMFD_MAX];

int ir0_memfd_is(const void *ptr)
{
	const struct ir0_memfd *m = ptr;
	uintptr_t base = (uintptr_t)&g_memfd[0];
	uintptr_t end = (uintptr_t)&g_memfd[MEMFD_MAX];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_memfd[0])) != 0)
		return 0;
	return m && m->magic == MEMFD_MAGIC && m->in_use;
}

void ir0_memfd_acquire(struct ir0_memfd *m)
{
	unsigned long irq_flags;

	if (!m || !ir0_memfd_is(m))
		return;

	irq_flags = irq_save();
	m->refs++;
	irq_restore(irq_flags);
}

static void memfd_free_frames(struct ir0_memfd *m)
{
	size_t i;

	if (!m || !m->frames)
		return;
	for (i = 0; i < m->npages; i++)
	{
		if (m->frames[i])
			pmm_free_frame(m->frames[i]);
	}
	kfree(m->frames);
	m->frames = NULL;
	m->npages = 0;
	m->size = 0;
}

void ir0_memfd_release(struct ir0_memfd *m)
{
	unsigned long irq_flags;
	int last;

	if (!m || !ir0_memfd_is(m))
		return;

	irq_flags = irq_save();
	if (m->refs > 0)
		m->refs--;
	last = (m->refs == 0);
	irq_restore(irq_flags);

	if (!last)
		return;
	memfd_free_frames(m);
	memset(m, 0, sizeof(*m));
}

size_t ir0_memfd_size(const struct ir0_memfd *m)
{
	return (m && ir0_memfd_is(m)) ? m->size : 0;
}

int ir0_memfd_ftruncate(struct ir0_memfd *m, size_t length)
{
	size_t need;
	size_t i;
	uintptr_t *nf;

	if (!m || !ir0_memfd_is(m))
		return -EINVAL;
	if (length > (size_t)MEMFD_MAX_PAGES * PAGE_SZ)
		return -EINVAL;
	need = (length + PAGE_SZ - 1) / PAGE_SZ;
	if (need == m->npages)
	{
		m->size = length;
		return 0;
	}
	if (need < m->npages)
	{
		for (i = need; i < m->npages; i++)
		{
			if (m->frames[i])
				pmm_free_frame(m->frames[i]);
			m->frames[i] = 0;
		}
		m->npages = need;
		m->size = length;
		return 0;
	}
	nf = kmalloc(need * sizeof(uintptr_t));
	if (!nf)
		return -ENOMEM;
	memset(nf, 0, need * sizeof(uintptr_t));
	if (m->frames && m->npages)
		memcpy(nf, m->frames, m->npages * sizeof(uintptr_t));
	for (i = m->npages; i < need; i++)
	{
		uintptr_t phys = pmm_alloc_frame();

		if (!phys)
		{
			while (i > m->npages)
			{
				i--;
				pmm_free_frame(nf[i]);
			}
			kfree(nf);
			return -ENOMEM;
		}
		nf[i] = phys;
		memset((void *)(uintptr_t)phys, 0, PAGE_SZ);
	}
	if (m->frames)
		kfree(m->frames);
	m->frames = nf;
	m->npages = need;
	m->size = length;
	return 0;
}

int ir0_memfd_mmap(struct ir0_memfd *m, uint64_t *pml4, uintptr_t va,
		   size_t length, off_t offset, uint64_t page_flags)
{
	size_t off_u;
	size_t map_len;
	size_t p;
	size_t start_page;

	if (!m || !ir0_memfd_is(m) || !pml4)
		return -EINVAL;
	if (offset < 0 || (size_t)offset >= m->size)
		return -EINVAL;
	off_u = (size_t)offset;
	if (off_u % PAGE_SZ)
		return -EINVAL;
	map_len = length;
	if (off_u + map_len > m->size)
		map_len = m->size - off_u;
	map_len = (map_len + PAGE_SZ - 1) & ~(size_t)(PAGE_SZ - 1);
	if (map_len == 0)
		return -EINVAL;
	start_page = off_u / PAGE_SZ;
	for (p = 0; p < map_len / PAGE_SZ; p++)
	{
		size_t idx = start_page + p;

		if (idx >= m->npages || !m->frames[idx])
			return -EINVAL;
		if (map_page_in_directory(pml4, va + p * PAGE_SZ, m->frames[idx],
					  page_flags) != 0)
		{
			while (p > 0)
			{
				p--;
				unmap_page_in_directory(pml4, va + p * PAGE_SZ);
			}
			return -ENOMEM;
		}
	}
	return (int)map_len;
}

struct ir0_memfd *ir0_memfd_alloc(void)
{
	int i;

	for (i = 0; i < MEMFD_MAX; i++)
	{
		if (!g_memfd[i].in_use)
		{
			memset(&g_memfd[i], 0, sizeof(g_memfd[i]));
			g_memfd[i].in_use = 1;
			g_memfd[i].magic = MEMFD_MAGIC;
			g_memfd[i].refs = 1;
			return &g_memfd[i];
		}
	}
	return NULL;
}

int ir0_memfd_install_fd(struct ir0_memfd *m, int open_flags, int cloexec)
{
	fd_entry_t *tab;
	int fd;

	if (!m || !ir0_memfd_is(m) || !current_process)
		return -EINVAL;
	tab = get_process_fd_table();
	if (!tab)
		return -ESRCH;
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!tab[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
		return -EMFILE;
	memset(&tab[fd], 0, sizeof(tab[fd]));
	tab[fd].in_use = true;
	tab[fd].is_memfd = true;
	tab[fd].vfs_file = m;
	tab[fd].flags = open_flags;
	if (cloexec)
		tab[fd].fd_flags = FD_CLOEXEC;
	fd_slot_note_created();
	return fd;
}

int64_t sys_memfd_create(const char *uname, unsigned int flags)
{
	char name[64];
	struct ir0_memfd *m;
	int fd;

	if (!current_process)
		return -ESRCH;
	if (flags & ~IR0_MFD_CLOEXEC)
		return -EINVAL;
	memset(name, 0, sizeof(name));
	if (uname)
	{
		if (copy_from_user(name, uname, sizeof(name) - 1) != 0)
			return -EFAULT;
	}
	m = ir0_memfd_alloc();
	if (!m)
		return -ENOMEM;
	memcpy(m->name, name, sizeof(m->name) - 1);
	fd = ir0_memfd_install_fd(m, O_RDWR, (flags & IR0_MFD_CLOEXEC) ? 1 : 0);
	if (fd < 0)
	{
		ir0_memfd_release(m);
		return fd;
	}
	return fd;
}
