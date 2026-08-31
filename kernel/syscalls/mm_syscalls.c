/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_syscalls.c
 * Description: memory-management syscall helpers (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/syscalls_kernel.h>
#include "mm_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/abi/mmap_contract.h>
#include <config.h>
#include <ir0/vfs.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <ir0/stat.h>
#include <ir0/process.h>
#include <ir0/files_struct.h>
#include <ir0/mm_struct.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/mm_port.h>
#include <ir0/clock.h>
#include <mm/allocator.h>
#include <ir0/arch_port.h>
#include <ir0/ktm/checkpoint.h>
#include <stdbool.h>
#include <string.h>

#include <ir0/fb.h>
#include <ir0/memfd.h>
#include <ir0/validation.h>
#include <stdint.h>

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0
#define SYSCALL_PTR_ERR(err) ((void *)(intptr_t)(-(err)))
#define MMAP_AUDIT_FAILED ((void *)(intptr_t)-1)

static void fase39_dump_current_vmas(const char *tag);

static void mm_prepare_map_fixed(uintptr_t start, size_t length)
{
	struct mmap_region **link;
	uintptr_t end;

	if (!current_process || length == 0)
		return;

	end = start + length;
	link = process_mmap_list_p(current_process);
	if (!link)
		return;

	while (*link)
	{
		struct mmap_region *region = *link;
		uintptr_t region_start = (uintptr_t)region->addr & ~(PAGE_SIZE_4KB - 1);
		uintptr_t region_end = region_start +
			((region->length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1));

		if (region_end <= start || region_start >= end)
		{
			link = &region->next;
			continue;
		}

		*link = region->next;
		kfree(region);
	}

	for (uintptr_t page = start; page < end; page += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(process_pgd(current_process), page,
						NULL) == 1)
			unmap_page_in_directory(process_pgd(current_process), page);
	}
}

static int mm_mmap_verify_ptes(uint64_t *pml4, uintptr_t virt_addr, size_t len)
{
	size_t i;

	if (!pml4 || len == 0)
		return -EINVAL;

	for (i = 0; i < len; i += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(pml4, virt_addr + i, NULL) != 1)
			return -ENOMEM;
	}
	return 0;
}

static bool mm_va_range_all_unmapped(uint64_t *pml4, uintptr_t start,
				     size_t length)
{
	uintptr_t check;

	if (!pml4 || length == 0)
		return false;

	for (check = start; check < start + length; check += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(pml4, check, NULL) == 1)
			return false;
	}
	return true;
}

static uintptr_t mm_mmap_search_low(process_t *proc)
{
	uintptr_t search_low = USER_MMAP_START;

	if (!proc)
		return search_low;

	if (process_heap_end(proc) > process_heap_start(proc) &&
	    process_heap_end(proc) > search_low)
	{
		search_low = (uintptr_t)(process_heap_end(proc) + PAGE_SIZE_4KB - 1) &
			     ~(PAGE_SIZE_4KB - 1);
	}
	return search_low;
}

static uintptr_t mm_mmap_stack_ref(process_t *proc)
{
	if (!proc || proc->mode != USER_MODE || !process_stack_start(proc))
		return USER_STACK_BASE;

	return (uintptr_t)process_stack_start(proc);
}

static uintptr_t mm_mmap_search_end(process_t *proc)
{
	if (!proc)
		return USER_MMAP_END;

	return ir0_mmap_stack_search_end(mm_mmap_stack_ref(proc));
}

/*
 * Linux-like top-down placement for mmap(NULL) and non-fixed hints.
 * Updates process_mmap_base(proc) to the chosen start for the next call.
 */
static uintptr_t mm_pick_free_va_topdown(process_t *proc, uint64_t *pml4,
					 size_t length)
{
	uintptr_t search_end;
	uintptr_t search_low;
	uintptr_t top;
	uintptr_t start;

	if (!proc || !pml4 || length == 0)
		return 0;

	if (length > (size_t)(USER_MMAP_END - USER_MMAP_START))
		return 0;

	{
		uintptr_t stack_ref = mm_mmap_stack_ref(proc);

		search_low = mm_mmap_search_low(proc);
		search_end = mm_mmap_search_end(proc);
		if (search_low >= search_end || length > search_end - search_low)
			return 0;

		top = (uintptr_t)process_mmap_base(proc);
		if (top == 0 || top > search_end)
			top = search_end;
		for (start = top - length; start >= search_low; start -= PAGE_SIZE_4KB)
		{
			start &= ~(PAGE_SIZE_4KB - 1);
			if (start < search_low)
				break;
			if (!ir0_mmap_respects_stack_gap(start, length, stack_ref))
				continue;
			if (process_user_va_range_overlaps(proc, start, length))
				continue;
			if (!mm_va_range_all_unmapped(pml4, start, length))
				continue;

			process_set_mmap_base(proc, start);
			return start;
		}
	}
	return 0;
}

static uintptr_t mm_find_free_va(uint64_t *pml4, process_t *proc, uintptr_t hint,
				 size_t length)
{
	if (hint != 0)
	{
		if ((hint & (PAGE_SIZE_4KB - 1)) != 0)
			return 0;
		if (hint < USER_MMAP_START ||
		    hint + length > USER_MMAP_END || hint + length < hint)
			return 0;
		if (!is_user_address((void *)(uintptr_t)hint, length))
			return 0;
		if (proc && process_user_va_range_overlaps(proc, hint, length))
			return 0;
		if (is_page_mapped_in_directory(pml4, hint, NULL) == 1)
			return 0;
		return hint;
	}

	return mm_pick_free_va_topdown(proc, pml4, length);
}

void *mm_mmap_file_private(process_t *proc, void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset)
{
	fd_entry_t *fd_table;
	struct vfs_file *vfs_file;
	stat_t st;
	size_t map_len;
	size_t file_rem;
	uint64_t page_flags;
	uintptr_t virt_addr;
	struct mmap_region *region;
	char page_buf[PAGE_SIZE_4KB];
	size_t copied;
	off_t file_pos;

	if (!proc)
		return SYSCALL_PTR_ERR(ESRCH);

	if (flags & MAP_SHARED)
	{
		if (prot & PROT_WRITE)
			return SYSCALL_PTR_ERR(ENOSYS);
	}

	fd_table = process_fd_table(proc);
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
		return SYSCALL_PTR_ERR(EBADF);
	if (fd_table[fd].is_pipe || fd_table[fd].is_devfs)
		return SYSCALL_PTR_ERR(ENODEV);
	if (!fd_table[fd].vfs_file)
		return SYSCALL_PTR_ERR(EACCES);

	if (vfs_stat(fd_table[fd].path, &st) != 0)
		return SYSCALL_PTR_ERR(EACCES);
	if (!S_ISREG(st.st_mode))
		return SYSCALL_PTR_ERR(ENODEV);

	if (offset < 0 || (uint64_t)offset >= (uint64_t)st.st_size)
		return SYSCALL_PTR_ERR(EINVAL);

	file_rem = (size_t)((uint64_t)st.st_size - (uint64_t)offset);
	map_len = length;
	if (map_len > file_rem)
		map_len = file_rem;
	map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
	if (map_len == 0)
		return SYSCALL_PTR_ERR(EINVAL);

	virt_addr = mm_find_free_va(process_pgd(proc), proc, (uintptr_t)addr, map_len);
	if (virt_addr == 0)
		return SYSCALL_PTR_ERR(ENOMEM);

	page_flags = PAGE_USER;
	if (prot & PROT_WRITE)
		page_flags |= PAGE_RW;
	if (prot & PROT_EXEC)
		page_flags |= PAGE_EXEC;

	if (map_user_region_in_directory(process_pgd(proc), virt_addr, map_len,
					 page_flags) != 0)
		return SYSCALL_PTR_ERR(ENOMEM);

	vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
	copied = 0;
	file_pos = offset;

	while (copied < map_len)
	{
		size_t chunk = map_len - copied;
		int nread;

		if (chunk > sizeof(page_buf))
			chunk = sizeof(page_buf);
		nread = vfs_pread(vfs_file, page_buf, chunk, file_pos);
		if (nread < 0)
		{
			for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
				unmap_page_in_directory(process_pgd(proc),
							virt_addr + off);
			return SYSCALL_PTR_ERR(EIO);
		}
		if ((size_t)nread < chunk)
			memset(page_buf + nread, 0, chunk - (size_t)nread);
		if (copy_to_user_region_in_directory(process_pgd(proc),
						     virt_addr + copied,
						     page_buf, chunk) != 0)
		{
			for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
				unmap_page_in_directory(process_pgd(proc),
							virt_addr + off);
			return SYSCALL_PTR_ERR(EFAULT);
		}
		copied += chunk;
		file_pos += (off_t)chunk;
	}

	region = kmalloc_try(sizeof(struct mmap_region));
	if (!region)
	{
		for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
			unmap_page_in_directory(process_pgd(proc), virt_addr + off);
		return SYSCALL_PTR_ERR(ENOMEM);
	}

	region->addr = (void *)virt_addr;
	region->hint_addr = addr;
	region->length = map_len;
	region->prot = prot;
	region->flags = flags;
	region->next = process_mmap_list(proc);
	process_mm_set_mmap_list(proc, region);
	KTM_CHECKPOINT(KTM_CP_MM_MAP);

	if (DEBUG_MMAP_AUDIT)
		klog_debug("MMAP", "CLASSIFY FILE_MMAP_PRIVATE_OK");
	return (void *)virt_addr;
}

/*
 * Does [lo, hi) overlap any registered mmap region of @proc?
 * Linux find_vma_intersection equivalent for the brk growth check.
 */
static int brk_range_hits_mmap(process_t *proc, uintptr_t lo, uintptr_t hi)
{
	struct mmap_region *r;

	for (r = process_mmap_list(proc); r != NULL; r = r->next)
	{
		uintptr_t r_lo = (uintptr_t)r->addr;
		uintptr_t r_hi = r_lo + (r->length ? r->length : 1);

		if (lo < r_hi && r_lo < hi)
			return 1;
	}
	return 0;
}

int64_t sys_brk(void *addr)
{
	uintptr_t heap_lo;
	uintptr_t new_brk;
	uintptr_t current_brk;

	if (!current_process)
		return -ESRCH;

	klog_debug_fmt("BRK", "req pid=%x addr=%llx hs=%llx he=%llx\n",
			(unsigned)((uint32_t)current_process->task.pid),
			(unsigned long long)(uintptr_t)addr,
			(unsigned long long)(uint64_t)process_heap_start(current_process),
			(unsigned long long)(uint64_t)process_heap_end(current_process));

	/* brk(NULL) / brk(0): return current program break (Linux ABI). */
	if (!addr)
	{
		if (process_heap_start(current_process) == 0 &&
		    process_heap_end(current_process) == 0)
		{
			process_set_heap_start(current_process, USER_HEAP_BASE);
			process_set_heap_end(current_process, USER_HEAP_BASE);
		}
		return (int64_t)process_heap_end(current_process);
	}

	new_brk = (uintptr_t)addr;
	current_brk = process_heap_end(current_process);
	heap_lo = process_heap_start(current_process);

	/*
	 * Processes without ELF exec init (legacy smokes): fall back to
	 * USER_HEAP_BASE. Post-exec images set heap_start/end from PT_LOAD
	 * (already snapped to USER_HEAP_BASE by elf_compute_initial_brk).
	 */
	if (heap_lo == 0 && current_brk == 0)
	{
		heap_lo = USER_HEAP_BASE;
		process_set_heap_start(current_process, heap_lo);
		process_set_heap_end(current_process, heap_lo);
		current_brk = heap_lo;
	}

	/*
	 * Linux brk(2) never returns -errno. A rejected request keeps the
	 * current program break so musl's `__syscall(SYS_brk) < end` probe
	 * sees a real VA instead of -EFAULT.
	 */
	if (!is_user_address(addr, 0) || new_brk < heap_lo ||
	    new_brk > heap_lo + USER_HEAP_MAX_SIZE)
		return (int64_t)current_brk;

	/*
	 * Linux do_brk_flags: the break never grows over an existing mapping.
	 * musl probes the page above the break with a PROT_NONE MAP_FIXED
	 * anonymous mmap; extending across it left the kernel treating the page
	 * as heap while the VMA still said PROT_NONE, so the first heap access
	 * took a SIGSEGV. Returning the unchanged break is how brk(2) reports
	 * failure, and musl then falls back to mmap.
	 */
	/*
	 * Linux SYSCALL_DEFINE1(brk) also reserves a guard page past the new
	 * break (find_vma_intersection(mm, oldbrk, newbrk + PAGE_SIZE)). IR0
	 * cannot: mallocng parks its PROT_NONE guard exactly one page above
	 * the break it just set, so the extra page rejects musl's own growth.
	 */
	if (new_brk > current_brk &&
	    brk_range_hits_mmap(current_process, current_brk, new_brk))
		return (int64_t)current_brk;

	/* If expanding heap, map only pages past the current break. */
	if (new_brk > current_brk)
	{
		uintptr_t start_page =
			(current_brk + PAGE_SIZE_4KB - 1) &
			(uintptr_t)PAGE_FRAME_MASK;
		uintptr_t end_page = (new_brk + PAGE_SIZE_4KB - 1) &
				     (uintptr_t)PAGE_FRAME_MASK;
		size_t size_to_map;

		if (end_page > start_page)
		{
			size_to_map = end_page - start_page;
			if (map_user_region_in_directory(process_pgd(current_process),
							 start_page, size_to_map,
							 PAGE_RW) != 0)
				return (int64_t)process_heap_end(current_process);
		}
	}
	else if (new_brk < current_brk)
	{
		uintptr_t old_end = current_brk;

		for (uintptr_t page = (new_brk + (PAGE_SIZE_4KB - 1)) &
				      (uintptr_t)PAGE_FRAME_MASK;
		     page < old_end;
		     page += PAGE_SIZE_4KB)
			unmap_page_in_directory(process_pgd(current_process), page);
	}

	process_set_heap_end(current_process, new_brk);
	fase39_dump_current_vmas("brk");
	return (int64_t)new_brk;
}

/* sbrk is typically implemented as a userspace library function using brk */
/* POSIX does not require sbrk as a syscall */


static int mmap_audit_ptr_err(void *ret)
{
  return ((intptr_t)ret < 0);
}

static int mmap_audit_errno_from_ret(void *ret)
{
  if (!mmap_audit_ptr_err(ret))
    return 0;
  return -(int)(intptr_t)ret;
}

static void mmap_audit_log_pte(const char *tag, uint64_t *pml4, uintptr_t va)
{
  uint64_t pte_flags = 0;
  uint64_t *pte;
  int mapped;

  if (!DEBUG_MMAP_AUDIT)
    return;
  if (!pml4)
    return;

  mapped = is_page_mapped_in_directory(pml4, va, &pte_flags);
  pte = paging_get_pte(pml4, va);

  if (pte && (*pte & PAGE_PRESENT))
  {
    klog_debug_fmt("MMAP", "PTE tag=%s va=%llx mapped=%llx present=%llx "
                   "user=%llx rw=%llx nx=%llx pfn=%llx",
                   tag ? tag : "(null)", (unsigned long long)((uint64_t)va),
                   (unsigned long long)((uint64_t)(mapped > 0 ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && (*pte & PAGE_PRESENT) ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_USER ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_RW ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && (*pte & PAGE_NX) ? 1 : 0)),
                   (unsigned long long)((uint64_t)(*pte & PAGE_PTE_PFN_MASK)));
  }
  else
  {
    klog_debug_fmt("MMAP", "PTE tag=%s va=%llx mapped=%llx present=%llx "
                   "user=%llx rw=%llx nx=%llx",
                   tag ? tag : "(null)", (unsigned long long)((uint64_t)va),
                   (unsigned long long)((uint64_t)(mapped > 0 ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && (*pte & PAGE_PRESENT) ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_USER ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_RW ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && (*pte & PAGE_NX) ? 1 : 0)));
  }
}

static void mmap_audit_log_args(void *addr, size_t length, int prot, int flags,
                                int fd, off_t offset)
{
  if (!DEBUG_MMAP_AUDIT)
    return;
  if (current_process)
  {
    klog_debug_fmt("MMAP", "ARGS classify=MMAP_ARGS_DECODED pid=%x comm=%s "
                   "addr=%llx length=%llx prot=%llx flags=%llx fd=%llx offset=%llx "
                   "caller_rip=%llx caller_rsp=%llx",
                   (unsigned)(current_process ? (uint32_t)current_process->task.pid : 0),
                   current_process ? current_process->comm : "(none)",
                   (unsigned long long)((uint64_t)(uintptr_t)addr),
                   (unsigned long long)((uint64_t)length),
                   (unsigned long long)((uint64_t)(unsigned int)prot),
                   (unsigned long long)((uint64_t)(unsigned int)flags),
                   (unsigned long long)((uint64_t)(unsigned int)fd),
                   (unsigned long long)((uint64_t)offset),
                   (unsigned long long)(process_syscall_ip(current_process)),
                   (unsigned long long)(process_syscall_sp(current_process)));
  }
  else
  {
    klog_debug_fmt("MMAP", "ARGS classify=MMAP_ARGS_DECODED pid=%x comm=%s "
                   "addr=%llx length=%llx prot=%llx flags=%llx fd=%llx offset=%llx",
                   (unsigned)(current_process ? (uint32_t)current_process->task.pid : 0),
                   current_process ? current_process->comm : "(none)",
                   (unsigned long long)((uint64_t)(uintptr_t)addr),
                   (unsigned long long)((uint64_t)length),
                   (unsigned long long)((uint64_t)(unsigned int)prot),
                   (unsigned long long)((uint64_t)(unsigned int)flags),
                   (unsigned long long)((uint64_t)(unsigned int)fd),
                   (unsigned long long)((uint64_t)offset));
  }

  if ((flags & MAP_SHARED) != 0 && (flags & MAP_PRIVATE) != 0)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_UNSUPPORTED_FLAGS reason=MAP_SHARED_and_MAP_PRIVATE");
  }
  if (!(flags & MAP_ANONYMOUS) && fd < 0)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_UNSUPPORTED_FLAGS reason=file_map_without_fd");
  }
}

static void mmap_audit_log_return(const char *stage, void *ret, uintptr_t virt_addr,
                                  size_t length, int vma_inserted, uint64_t *pml4)
{
  size_t pages = (length + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
  size_t mapped_pages = 0;
  size_t i;

  if (!DEBUG_MMAP_AUDIT)
    return;

  klog_debug_fmt("MMAP", "RET stage=%s ret=%llx", stage ? stage : "(null)", (unsigned long long)((uint64_t)(uintptr_t)ret));
  if (mmap_audit_ptr_err(ret))
  {
    klog_debug_fmt("KERN", " errno=%llx", (unsigned long long)((uint64_t)(unsigned int)mmap_audit_errno_from_ret(ret)));
  }
  else
  {
    klog_debug_fmt("KERN", " range=[%llx,%llx) pages=%llx", (unsigned long long)((uint64_t)virt_addr), (unsigned long long)((uint64_t)(virt_addr + length)), (unsigned long long)((uint64_t)pages));
  }
  klog_debug_fmt("KERN", " vma_inserted=%llx", (unsigned long long)((uint64_t)(unsigned int)vma_inserted));

  if (mmap_audit_ptr_err(ret) || !pml4 || virt_addr == 0 || length == 0)
    return;

  for (i = 0; i < pages; i++)
  {
    uintptr_t va = virt_addr + i * PAGE_SIZE_4KB;
    if (is_page_mapped_in_directory(pml4, va, NULL) == 1)
      mapped_pages++;
  }

  klog_debug_fmt("MMAP", "RET pte_mapped_pages=%llx pte_expected_pages=%llx", (unsigned long long)((uint64_t)mapped_pages), (unsigned long long)((uint64_t)pages));

  if (mapped_pages == 0)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_VMA_WITHOUT_PTES");
  }
  else if (mapped_pages < pages)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_RET_UNMAPPED_RANGE");
  }

  mmap_audit_log_pte("first", pml4, virt_addr);
  if (pages > 1)
    mmap_audit_log_pte("last", pml4, virt_addr + (pages - 1) * PAGE_SIZE_4KB);

  if (pml4)
  {
    uint64_t flags_low = 0;

    if (is_page_mapped_in_directory(pml4, virt_addr, &flags_low) == 1)
    {
      if (!(flags_low & PAGE_USER))
      {
        klog_debug("MMAP", "CLASSIFY MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_USER");
      }
      if (!(flags_low & PAGE_RW))
      {
        klog_debug("MMAP", "CLASSIFY MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_RW");
      }
    }
  }
}

/*
 * FASE39 diagnostics: dump current process VMAs after brk/mmap/munmap.
 * Pure observability helper (no policy changes).
 */
static void fase39_dump_current_vmas(const char *tag)
{
  struct mmap_region *r;

  if (!DEBUG_MMAP_AUDIT)
    return;
  if (!current_process)
    return;




  for (r = process_mmap_list(current_process); r; r = r->next)
  {
    if ((r->flags & MAP_ANONYMOUS) != 0)
      klog_debug("KERN", "anonymous");
    else
      klog_debug("KERN", "fd-backed-or-device");
  }
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *ret;
  uintptr_t virt_addr_out = 0;
  size_t aligned_len = 0;
  int vma_inserted = 0;

  mmap_audit_log_args(addr, length, prot, flags, fd, offset);

  if (!current_process)
  {
    klog_debug("KERN", "SERIAL: mmap: no current process\n");
    ret = (void *)(intptr_t)-ESRCH;
    mmap_audit_log_return("no-process", ret, 0, 0, 0, NULL);
    return ret;
  }

  if (length == 0)
  {
    klog_debug("KERN", "SERIAL: mmap: zero length\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("zero-length", ret, 0, 0, 0, process_pgd(current_process));
    return ret;
  }

  if (length > (USER_MMAP_END - USER_MMAP_START) || length > (1UL << 28))
  {
    klog_debug("KERN", "SERIAL: mmap: length exceeds user mmap arena\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("length-too-large", ret, 0, 0, 0,
                          process_pgd(current_process));
    return ret;
  }

  /* Validate protection flags */
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
  {
    klog_debug("KERN", "SERIAL: mmap: invalid protection flags\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("bad-prot", ret, 0, 0, 0, process_pgd(current_process));
    return ret;
  }

  /* Validate offset alignment for file mappings */
  if (!(flags & MAP_ANONYMOUS))
  {
    if (fd < 0)
    {
      klog_debug("KERN", "SERIAL: mmap: file mapping requires valid fd\n");
      return SYSCALL_PTR_ERR(EBADF);
    }
    
    /* Offset must be page-aligned for file mappings */
    if (offset % PAGE_SIZE_4KB != 0)
    {
      klog_debug("KERN", "SERIAL: mmap: offset must be page-aligned\n");
      return SYSCALL_PTR_ERR(EINVAL);
    }

    /*
     * Linux: reject invalid fds before ENOSYS for unimplemented file-backed
     * mmap. Only real fd_table slots are valid (devfs /dev/fb0 uses is_devfs).
     */
    {
      fd_entry_t *fd_table = get_process_fd_table();
      bool fd_valid_open = (fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
                            fd_table && fd_table[fd].in_use);

      if (!fd_valid_open)
      {
        klog_debug("KERN", "SERIAL: mmap: invalid fd for file-backed mmap\n");
        ret = SYSCALL_PTR_ERR(EBADF);
        mmap_audit_log_return("bad-fd", ret, 0, 0, 0,
                              process_pgd(current_process));
        return ret;
      }
    }

    /*
     * mmap of /dev/fb0 — real fd_table slot (is_devfs + device_id 15).
     * Maps framebuffer physical memory into userspace for efficient access.
     */
    {
      bool fb_mmap_devfs = false;
      uint32_t device_id = UINT32_MAX;
      fd_entry_t *fd_table = get_process_fd_table();

      if (fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
          fd_table && fd_table[fd].in_use && fd_table[fd].is_devfs)
      {
        device_id = fd_table[fd].dev_device_id;
        fb_mmap_devfs = true;
      }

#if CONFIG_ENABLE_VBE
      if (device_id == 15U)
      {
        struct ir0_fb_info fb_info;
        uint32_t fb_phys;
        uint32_t fb_size;

        if (!ir0_fb_get_info(&fb_info))
          return SYSCALL_PTR_ERR(ENODEV);

        fb_phys = fb_info.fb_phys;
        fb_size = fb_info.fb_size;
        if (fb_phys == 0 || fb_size == 0)
          return SYSCALL_PTR_ERR(ENODEV);

        if (offset < 0 || (uint64_t)offset >= (uint64_t)fb_size)
          return SYSCALL_PTR_ERR(EINVAL);

        uint64_t off_u = (uint64_t)offset;
        uint64_t rem = (uint64_t)fb_size - off_u;
        size_t map_len = length;

        if ((uint64_t)map_len > rem)
          map_len = (size_t)rem;
        map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
        if (map_len == 0)
          return SYSCALL_PTR_ERR(EINVAL);

        uintptr_t virt_addr = 0;
        if (addr != NULL)
        {
          uintptr_t hint_addr = (uintptr_t)addr;
          if ((hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
            return SYSCALL_PTR_ERR(EINVAL);
          if (!is_user_address(addr, map_len))
            return SYSCALL_PTR_ERR(EFAULT);
          int mapped = is_page_mapped_in_directory(process_pgd(current_process), hint_addr, NULL);
          if (mapped == 1)
            return SYSCALL_PTR_ERR(EINVAL);
          virt_addr = hint_addr;
        }
        else
        {
          uintptr_t search_start = USER_MMAP_START;
          uintptr_t search_end = USER_MMAP_END;
          uintptr_t candidate = search_start;
          bool found = false;
          while (candidate + map_len < search_end && !found)
          {
            bool all_unmapped = true;
            for (uintptr_t check = candidate; check < candidate + map_len; check += PAGE_SIZE_4KB)
            {
              int m = is_page_mapped_in_directory(process_pgd(current_process), check, NULL);
              if (m == 1)
              {
                all_unmapped = false;
                candidate = ((check + PAGE_SIZE_4KB) + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
                break;
              }
            }
            if (all_unmapped)
            {
              virt_addr = candidate;
              found = true;
            }
          }
          if (!found)
            return SYSCALL_PTR_ERR(ENOMEM);
        }
        
        uint64_t page_flags = PAGE_USER | PAGE_RW;
        for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
        {
          uintptr_t v = virt_addr + off;
          uintptr_t p = (uintptr_t)fb_phys + off_u + off;
          if (map_page_in_directory(process_pgd(current_process), v, p, page_flags) != 0)
          {
            /* Rollback: unmap already mapped pages */
            for (size_t r = 0; r < off; r += PAGE_SIZE_4KB)
              unmap_page_in_directory(process_pgd(current_process), virt_addr + r);
            return SYSCALL_PTR_ERR(ENOMEM);
          }
        }
        
        struct mmap_region *region = kmalloc_try(sizeof(struct mmap_region));
        if (!region)
        {
          for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
            unmap_page_in_directory(process_pgd(current_process), virt_addr + off);
          return SYSCALL_PTR_ERR(ENOMEM);
        }
        region->addr = (void *)virt_addr;
        region->hint_addr = addr;
        region->length = map_len;
        region->prot = prot;
        region->flags = flags;
        region->next = process_mmap_list(current_process);
        process_mm_set_mmap_list(current_process, region);
        KTM_CHECKPOINT(KTM_CP_MM_MAP);
        fase39_dump_current_vmas("mmap-fb");
        {
          static int s_fb_mmap_devfs_tag;
          static int s_fb_mmap_ok_tag;

          if (fb_mmap_devfs && !s_fb_mmap_devfs_tag)
          {
            s_fb_mmap_devfs_tag = 1;
            klog_smoke("FB_MMAP_DEVFS_FD_OK");
            klog_smoke("DEVFB0_MMAP_REAL_FD_OK");
          }
          if (!s_fb_mmap_ok_tag)
          {
            s_fb_mmap_ok_tag = 1;
            klog_smoke("FB_MMAP_OK");
          }
          if ((flags & MAP_SHARED) != 0)
          {
            static int s_fb_map_shared_tag;

            if (!s_fb_map_shared_tag)
            {
              s_fb_map_shared_tag = 1;
              klog_smoke("FB_MAP_SHARED_OK");
            }
          }
        }
        return (void *)virt_addr;
      }
#else
      (void)fb_mmap_devfs;
      (void)device_id;
#endif
    }

    /* memfd_create fd: MAP_SHARED (or MAP_PRIVATE copy-like via shared frames). */
    {
      fd_entry_t *fd_table = get_process_fd_table();

      if (fd >= 0 && fd < MAX_FDS_PER_PROCESS && fd_table &&
	  fd_table[fd].in_use && fd_table[fd].is_memfd && fd_table[fd].vfs_file)
      {
	struct ir0_memfd *m = (struct ir0_memfd *)fd_table[fd].vfs_file;
	size_t map_len;
	uintptr_t virt_addr = 0;
	uint64_t page_flags;
	int mapped_len;
	struct mmap_region *region;

	if ((flags & MAP_SHARED) == 0 && (flags & MAP_PRIVATE) == 0)
	  return SYSCALL_PTR_ERR(EINVAL);
	if (ir0_memfd_size(m) == 0)
	  return SYSCALL_PTR_ERR(EINVAL);

	map_len = length;
	map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
	if (map_len == 0)
	  return SYSCALL_PTR_ERR(EINVAL);

	if (addr != NULL)
	{
	  uintptr_t hint_addr = (uintptr_t)addr;

	  if ((hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
	    return SYSCALL_PTR_ERR(EINVAL);
	  if (!is_user_address(addr, map_len))
	    return SYSCALL_PTR_ERR(EFAULT);
	  if (is_page_mapped_in_directory(process_pgd(current_process),
					  hint_addr, NULL) == 1)
	    return SYSCALL_PTR_ERR(EINVAL);
	  virt_addr = hint_addr;
	}
	else
	{
	  virt_addr = mm_pick_free_va_topdown(current_process,
					      process_pgd(current_process),
					      map_len);
	  if (virt_addr == 0)
	    return SYSCALL_PTR_ERR(ENOMEM);
	}

	page_flags = PAGE_USER;
	if (prot & PROT_WRITE)
	  page_flags |= PAGE_RW;
	mapped_len = ir0_memfd_mmap(m, process_pgd(current_process), virt_addr,
				    map_len, offset, page_flags);
	if (mapped_len < 0)
	  return SYSCALL_PTR_ERR(-mapped_len);
	map_len = (size_t)mapped_len;

	region = kmalloc_try(sizeof(struct mmap_region));
	if (!region)
	{
	  for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
	    unmap_page_in_directory(process_pgd(current_process),
				    virt_addr + off);
	  return SYSCALL_PTR_ERR(ENOMEM);
	}
	region->addr = (void *)virt_addr;
	region->hint_addr = addr;
	region->length = map_len;
	region->prot = prot;
	region->flags = flags;
	region->next = process_mmap_list(current_process);
	process_mm_set_mmap_list(current_process, region);
	{
	  static int s_memfd_map_ok;

	  if (!s_memfd_map_ok)
	  {
	    s_memfd_map_ok = 1;
	    klog_smoke("MEMFD_MAP_SHARED_OK");
	  }
	}
	return (void *)virt_addr;
      }
    }

    /* File-based mapping not yet implemented for other files */
    klog_debug("KERN", "SERIAL: mmap: file-based mapping not yet implemented\n");
    return SYSCALL_PTR_ERR(ENOSYS);
  }

  /* Align length to page boundary */
  length = (length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
  aligned_len = length;

  /* Address hint support:
   * - If addr is NULL: Kernel chooses address
   * - If addr is provided: Try to use it if valid and page-aligned
   * - If MAP_FIXED is set (future): Must use exact address
   */
  uintptr_t virt_addr = 0;
  uintptr_t hint_addr = (uintptr_t)addr;
  bool use_hint = false;

  if ((flags & MAP_FIXED) != 0)
  {
    if (addr == NULL ||
        (hint_addr & (PAGE_SIZE_4KB - 1)) != 0 ||
        !is_user_address(addr, length))
    {
      ret = SYSCALL_PTR_ERR(EINVAL);
      mmap_audit_log_return("map-fixed-bad-addr", ret, 0, 0, 0,
                            process_pgd(current_process));
      return ret;
    }
    if (hint_addr < (uintptr_t)PMM_PHYS_BASE &&
	hint_addr + length > (uintptr_t)SIMPLE_HEAP_START)
    {
      ret = SYSCALL_PTR_ERR(EINVAL);
      mmap_audit_log_return("map-fixed-kernel-heap", ret, hint_addr, length, 0,
			    process_pgd(current_process));
      return ret;
    }

    /*
     * MAP_FIXED landing inside the live brk heap: the VMA then shadows
     * pages the break already owns, and a later heap access hits the VMA
     * protection instead of the heap. Narrow condition, logged to show
     * pid and ordering against the break.
     */
    if (hint_addr < (uintptr_t)process_heap_end(current_process) &&
        hint_addr + length > (uintptr_t)process_heap_start(current_process))
      klog_debug_fmt("MMAP",
		      "map-fixed inside brk pid=%x addr=%llx len=%llx prot=%llx heap_end=%llx rip=%llx\n",
		      (unsigned)((uint32_t)current_process->task.pid),
		      (unsigned long long)hint_addr,
		      (unsigned long long)(uint64_t)length,
		      (unsigned long long)(uint64_t)prot,
		      (unsigned long long)(uint64_t)process_heap_end(current_process),
		      (unsigned long long)process_syscall_ip(current_process));

    mm_prepare_map_fixed(hint_addr, length);
    virt_addr = hint_addr;
    use_hint = true;
  }
  /*
   * Non-MAP_FIXED: addr is only an advisory hint. If unusable (misaligned,
   * out of range, or already mapped) pick another address — do not EINVAL.
   */
  else if (addr != NULL &&
           (hint_addr & (PAGE_SIZE_4KB - 1)) == 0 &&
           hint_addr >= USER_MMAP_START &&
           hint_addr + length <= USER_MMAP_END &&
           hint_addr + length >= hint_addr &&
           is_user_address(addr, length))
  {
    bool collision = false;

    for (uintptr_t check = hint_addr; check < hint_addr + length;
         check += PAGE_SIZE_4KB)
    {
      if (is_page_mapped_in_directory(process_pgd(current_process), check,
                                      NULL) == 1)
      {
        collision = true;
        break;
      }
    }

    if (!collision)
    {
      virt_addr = hint_addr;
      use_hint = true;
    }
  }

  if (!use_hint)
  {
    virt_addr = mm_pick_free_va_topdown(current_process,
                                        process_pgd(current_process),
                                        length);
    if (virt_addr == 0)
      return SYSCALL_PTR_ERR(ENOMEM);
  }

  if ((flags & MAP_FIXED) == 0 &&
      (virt_addr < USER_MMAP_START ||
       virt_addr + aligned_len > USER_MMAP_END ||
       virt_addr + aligned_len < virt_addr))
  {
    klog_debug_fmt("MMAP",
		    "reject va=%llx len=%llx outside mmap arena\n",
		    (unsigned long long)virt_addr,
		    (unsigned long long)aligned_len);
    return SYSCALL_PTR_ERR(ENOMEM);
  }

  /* Determine page flags from protection flags */
  uint64_t page_flags = PAGE_USER;  /* Always user mode */
  if (prot & PROT_READ)
    page_flags |= 0;  /* Read is default */
  if (prot & PROT_WRITE)
    page_flags |= PAGE_RW;
  if (prot & PROT_EXEC)
    page_flags |= PAGE_EXEC;

  /* Map pages in process page directory (skip PTE install for anon PROT_NONE) */
  if (!((flags & MAP_ANONYMOUS) && prot == PROT_NONE))
  {
    if (map_user_region_in_directory(process_pgd(current_process), virt_addr, length, page_flags) != 0)
    {
      klog_debug("KERN", "SERIAL: mmap: failed to map pages\n");
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("map-failed", ret, virt_addr, aligned_len, 0,
                            process_pgd(current_process));
      return ret;
    }

    if (mm_mmap_verify_ptes(process_pgd(current_process), virt_addr,
                            aligned_len) != 0)
    {
      for (uintptr_t page = virt_addr; page < virt_addr + aligned_len;
           page += PAGE_SIZE_4KB)
        unmap_page_in_directory(process_pgd(current_process), page);
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("pte-verify-fail", ret, virt_addr, aligned_len, 0,
                            process_pgd(current_process));
      return ret;
    }

    virt_addr_out = virt_addr;
    mmap_audit_log_pte("post-map", process_pgd(current_process), virt_addr);
  }
  else if (DEBUG_MMAP_AUDIT)
  {
    klog_debug_fmt("MMAP", "RESERVE stage=vma-only prot_none anon len=%llx", (unsigned long long)((uint64_t)aligned_len));
  }

  /*
   * Anonymous zero-fill: map_user_region_in_directory() clears each
   * physical frame via the identity map before installing final PTEs.
   * Do not memset() through the user VA while PTEs lack PAGE_RW — that
   * faults in kernel mode on PROT_NONE / read-only mappings.
   */
  if (DEBUG_MMAP_AUDIT)
  {
    if (prot == PROT_NONE)
    {
      klog_debug("MMAP", "ZERO stage=skipped reason=prot_none");
    }
    else if (prot & PROT_WRITE)
    {
      klog_debug("MMAP", "ZERO stage=phys-prezeroed-in-map_user_region");
    }
    else
    {
      klog_debug("MMAP", "ZERO stage=skipped reason=no_prot_write");
    }
  }

  /* Create mapping entry */
  struct mmap_region *region = kmalloc_try(sizeof(struct mmap_region));
  if (!region)
  {
      /* Failed to allocate region entry - unmap pages */
      if (!((flags & MAP_ANONYMOUS) && prot == PROT_NONE))
      {
        for (uintptr_t page = virt_addr; page < virt_addr + length; page += PAGE_SIZE_4KB)
        {
          unmap_page_in_directory(process_pgd(current_process), page);
        }
      }
    return SYSCALL_PTR_ERR(ENOMEM);
  }

  region->addr = (void *)virt_addr;
  region->hint_addr = addr;  /* Store hint for future reference */
  region->length = length;
  region->prot = prot;  /* Store protection flags for mprotect */
  region->flags = flags;
  region->next = process_mmap_list(current_process);
  process_mm_set_mmap_list(current_process, region);
  vma_inserted = 1;
  virt_addr_out = virt_addr;
  KTM_CHECKPOINT(KTM_CP_MM_MAP);
  fase39_dump_current_vmas("mmap");

  ret = (void *)virt_addr;
  mmap_audit_log_return("ok", ret, virt_addr_out, aligned_len, vma_inserted,
                        process_pgd(current_process));
  if (DEBUG_MMAP_AUDIT)
    klog_debug("MMAP", "CLASSIFY BUSYBOX_NEXT_SYSCALL_REACHED stage=mmap-return-ok");
  return ret;
}

int sys_munmap(void *addr, size_t length)
{
  uint64_t unmapped_pages = 0;
  paging_ir0_mm_checkpoint("munmap-before", (int32_t)process_get_pid());
  if (!current_process)
    return -ESRCH;
  if (!addr || length == 0)
    return -EINVAL;

  /* Validate address is in userspace */
  if (!is_user_address(addr, length))
    return -EFAULT;

  /* Align to page boundaries */
  uintptr_t start_page = (uintptr_t)addr & ~0xFFF;
  size_t aligned_length = ((length + 0xFFF) & ~0xFFF);

  /* Find the mapping */
  struct mmap_region *current = process_mmap_list(current_process);
  struct mmap_region *prev = NULL;

  while (current)
  {
    uintptr_t mapping_start = (uintptr_t)current->addr & ~0xFFF;
    uintptr_t mapping_end = mapping_start + ((current->length + 0xFFF) & ~0xFFF);
    
    if (start_page >= mapping_start && (start_page + aligned_length) <= mapping_end)
    {
      /* Remove from list */
      if (prev)
        prev->next = current->next;
      else
        process_mm_set_mmap_list(current_process, current->next);

      /* Unmap pages in process page directory */
      for (uintptr_t page = start_page; page < start_page + aligned_length; page += PAGE_SIZE_4KB)
      {
        if (unmap_page_in_directory(process_pgd(current_process), page) == 0)
          unmapped_pages++;
      }

      /* Free the mapping structure */
      kfree(current);
      KTM_CHECKPOINT(KTM_CP_MM_UNMAP);
      paging_ir0_mm_checkpoint("munmap-after", (int32_t)current_process->task.pid);
      fase39_dump_current_vmas("munmap");
      return 0;
    }
    prev = current;
    current = current->next;
  }

  return -EINVAL; /* Not found */
}

int sys_mprotect(void *addr, size_t len, int prot)
{
  struct mmap_region *current;
  struct mmap_region *matched = NULL;
  uintptr_t range_start;
  uintptr_t range_end;
  uint64_t *pml4;
  uint64_t map_flags;
  int saw_present = 0;

  if (!current_process)
    return -ESRCH;
  if (!addr || len == 0)
    return -EINVAL;

  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    return -EINVAL;

  if (!is_user_address(addr, len))
    return -EFAULT;

  /*
   * Linux allows mprotect on any mapped VA (including ELF LOAD segments).
   * IR0 mmap_list only tracks sys_mmap regions — fall through to PTE walk so
   * glibc/musl RELRO (mprotect → PROT_READ after load) works for exec images.
   */
  current = process_mmap_list(current_process);
  while (current)
  {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length)
    {
      matched = current;
      break;
    }
    current = current->next;
  }

  range_start = (uintptr_t)addr & (uintptr_t)PAGE_FRAME_MASK;
  range_end = (((uintptr_t)addr + len) + PAGE_SIZE_4KB - 1) & (uintptr_t)PAGE_FRAME_MASK;
  pml4 = process_pgd(current_process);

  map_flags = PAGE_USER;
  if (prot & PROT_WRITE)
    map_flags |= PAGE_RW;
  if (prot & PROT_EXEC)
    map_flags |= PAGE_EXEC;

  for (uintptr_t page = range_start; page < range_end; page += PAGE_SIZE_4KB)
  {
    uint64_t *pte;
    uint64_t phys;

    pte = paging_get_pte(pml4, page);
    if (!pte || !(*pte & PAGE_PRESENT))
    {
      /* Gaps only OK when covering a known mmap region (lazy anon). */
      if (!matched)
        continue;
      phys = pmm_alloc_frame();
      if (phys == 0)
        return -ENOMEM;
      if (map_page_in_directory(pml4, page, phys, map_flags) != 0)
      {
        pmm_free_frame(phys);
        return -ENOMEM;
      }
      {
        uint64_t old_cr3 = get_current_page_directory();

        load_page_directory((uint64_t)pml4);
        memset((void *)page, 0, PAGE_SIZE_4KB);
        load_page_directory(old_cr3);
      }
      tlb_invalidate_page((uintptr_t)page);
      saw_present = 1;
      continue;
    }

    saw_present = 1;
    phys = *pte & PAGE_FRAME_MASK;
    if (map_page_in_directory(pml4, page, phys, map_flags) != 0)
      return -ENOMEM;
    tlb_invalidate_page((uintptr_t)page);
  }

  if (!saw_present)
    return -ENOMEM; /* Linux: no mapping in range → typically ENOMEM */

  if (matched)
    matched->prot = prot;

  return 0;
}

/*
 * sysinfo(2) — memory, load and uptime in one struct.
 *
 * BusyBox `free` and `uptime` read this syscall, not /proc. While it was
 * missing they got -ENOSYS and printed whatever their uninitialised struct
 * happened to hold: gigabytes of RAM on a machine with megabytes, and an
 * uptime of weeks. Layout and units follow sysinfo(2) (post Linux 2.3.48):
 * memory as multiples of mem_unit, loads as fixed point with a 16-bit
 * fraction.
 */
#define SYSINFO_LOAD_SHIFT 16

struct ir0_sysinfo
{
  int64_t uptime;
  uint64_t loads[3];
  uint64_t totalram;
  uint64_t freeram;
  uint64_t sharedram;
  uint64_t bufferram;
  uint64_t totalswap;
  uint64_t freeswap;
  uint16_t procs;
  uint16_t pad;
  uint64_t totalhigh;
  uint64_t freehigh;
  uint32_t mem_unit;
  char _f[20 - 2 * sizeof(uint64_t) - sizeof(uint32_t)];
};

/* Offsets checked against glibc's struct sysinfo on x86-64. */
_Static_assert(sizeof(struct ir0_sysinfo) == 112, "sysinfo ABI size");
_Static_assert(__builtin_offsetof(struct ir0_sysinfo, totalram) == 32,
               "sysinfo ABI totalram");
_Static_assert(__builtin_offsetof(struct ir0_sysinfo, procs) == 80,
               "sysinfo ABI procs");
_Static_assert(__builtin_offsetof(struct ir0_sysinfo, mem_unit) == 104,
               "sysinfo ABI mem_unit");

int64_t sys_sysinfo(void *user_info)
{
  struct ir0_sysinfo info;
  size_t total_frames = 0;
  size_t used_frames = 0;
  size_t free_frames = 0;
  uint32_t l1 = 0, l5 = 0, l15 = 0;
  unsigned runnable = 0, nprocs = 0;
  int last_pid = 0;

  if (!user_info)
    return -EFAULT;

  memset(&info, 0, sizeof(info));

  info.uptime = (int64_t)(clock_get_uptime_milliseconds() / 1000ULL);

  clock_get_loadavg(&l1, &l5, &l15, &runnable, &nprocs, &last_pid);
  /* Hundredths of a load unit into the 1/65536 fixed point Linux uses. */
  info.loads[0] = ((uint64_t)l1 << SYSINFO_LOAD_SHIFT) / 100ULL;
  info.loads[1] = ((uint64_t)l5 << SYSINFO_LOAD_SHIFT) / 100ULL;
  info.loads[2] = ((uint64_t)l15 << SYSINFO_LOAD_SHIFT) / 100ULL;
  info.procs = (uint16_t)nprocs;

  /* Same PMM figures /proc/meminfo reports, so the two cannot disagree. */
  ir0_mm_pmm_stats(&total_frames, &used_frames, &free_frames);
  info.mem_unit = (uint32_t)IR0_MM_PAGE_SIZE;
  info.totalram = (uint64_t)total_frames;
  info.freeram = (uint64_t)free_frames;

  /* No swap and no high memory on x86-64: report them as absent, not as
   * uninitialised. */

  if (copy_to_user(user_info, &info, sizeof(info)) != 0)
    return -EFAULT;

  return 0;
}
