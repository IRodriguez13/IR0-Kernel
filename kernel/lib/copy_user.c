// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: copy_user.c
 * Description: Safe copy functions with conditional validation
 *              Allows dbgshell (KERNEL_MODE) to use syscalls without validation
 *              while real userspace (USER_MODE) gets proper address checking
 */

#include <ir0/copy_user.h>
#include "process.h"
#include <mm/paging.h>
#include <string.h>
#include <config.h>
#include <ir0/ktm/klog.h>

/* User space address range (simplified) */
#define USER_SPACE_START 0x00400000UL  /* 4MB */
#define USER_SPACE_END   0x00007FFFFFFFFFFFUL  /* Canonical userspace limit (128TB) */


/**
 * is_user_address - Check if address range is valid userspace address
 * @addr: Address to check
 * @size: Size of region
 * @check_mapped: If 1, verify pages are actually mapped (slower but safer)
 * Returns: 1 if valid user address, 0 otherwise
 */
int is_user_address_checked(const void *addr, size_t size, int check_mapped)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;
    
    /* Check NULL */
    if (addr == NULL)
    {
        return 0;
    }
    
    /* Check overflow */
    if (end < start)
    {
        return 0;
    }
    
    /* Check range (canonical userspace: 0x0000000000000000 - 0x00007FFFFFFFFFFF) */
    if (start < USER_SPACE_START || end > USER_SPACE_END)
    {
        return 0;
    }
    
    /* If check_mapped is set, verify pages are actually mapped */
    if (check_mapped)
    {
        process_t *current = process_get_current();
        if (!current || !process_pgd(current))
            return 0;  /* No process context or page directory */
        
        /* Check each page in the range */
        for (uintptr_t addr_check = start & ~0xFFF; addr_check < end; addr_check += 0x1000)
        {
            int mapped = is_page_mapped_in_directory(process_pgd(current), addr_check, NULL);
            if (mapped != 1)
            {
                /* Page not mapped */
#if DEBUG_SYSCALLS
                klog_info("COPY_USER", "Address range contains unmapped pages");
#endif
                return 0;
            }
        }
    }
    
    return 1;
}

int is_user_address(const void *addr, size_t size)
{
    /*
     * Fast range check only; unmapped pages fault on copy. Callers that must
     * reject unmapped user pointers before touching page tables (e.g. exec
     * argv/envp walks) use is_user_address_checked(addr, size, 1).
     */
    return is_user_address_checked(addr, size, 0);
}


/**
 * copy_to_user - Copy to user space with mode-aware validation
 */
int copy_to_user(void *dst, const void *src, size_t n)
{
    process_t *current = process_get_current();

    if (n == 0)
        return 0;

    /* KERNEL_MODE bypass (dbgshell, embedded init) */
    if (current && current->mode == KERNEL_MODE)
    {
        memcpy(dst, src, n);
        return 0;
    }

    if (!is_user_address(dst, n))
        return -EFAULT;

    /*
     * Walk target process page tables via MM facade — safe under any active
     * CR3 and fails cleanly on guard/unmapped pages (no kernel #PF panic).
     */
    if (current && process_pgd(current))
    {
        if (copy_to_user_region_in_directory(process_pgd(current),
                                             (uintptr_t)dst, src, n) != 0)
            return -EFAULT;
        return 0;
    }

    memcpy(dst, src, n);
    return 0;
}

/*
 *
 * copy_from_user - Copy from user space with mode-aware validation
 */
int copy_from_user(void *dst, const void *src, size_t n)
{
    process_t *current = process_get_current();

    if (n == 0)
        return 0;

    if (current && current->mode == KERNEL_MODE)
    {
        memcpy(dst, src, n);
        return 0;
    }

    if (!is_user_address(src, n))
        return -EFAULT;

    if (current && process_pgd(current))
    {
        if (copy_from_user_region_in_directory(process_pgd(current),
                                               (uintptr_t)src, dst, n) != 0)
            return -EFAULT;
        return 0;
    }

    memcpy(dst, src, n);
    return 0;
}

int copy_from_user_cstring(char *dst, size_t dst_sz, const char *src)
{
    process_t *current = process_get_current();
    size_t i;

    if (!dst || dst_sz == 0 || !src)
        return -EFAULT;

    if (current && current->mode == KERNEL_MODE)
    {
        size_t n = 0;

        while (n + 1 < dst_sz && ((const char *)src)[n])
        {
            dst[n] = ((const char *)src)[n];
            n++;
        }
        dst[n] = '\0';
        return 0;
    }

    if (!is_user_address(src, 1))
        return -EFAULT;

    if (!current || !process_pgd(current))
        return -EFAULT;

    for (i = 0; i + 1 < dst_sz; i++)
    {
        char c;

        if (copy_from_user_region_in_directory(process_pgd(current),
                                               (uintptr_t)src + i, &c, 1) != 0)
            return -EFAULT;
        dst[i] = c;
        if (c == '\0')
            return 0;
    }

    dst[dst_sz - 1] = '\0';
    return 0;
}
