/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: validate_user.c
 * Description: userspace pointer validation helpers (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "validate_user.h"
#include <kernel/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/process.h>

/**
 * validate_userspace_string - Validate that a string argument is in userspace
 * @str: String pointer to validate
 * @max_len: Maximum expected string length
 * Returns: 0 if valid, -EFAULT if invalid
 */
int validate_userspace_string(const char *str, size_t max_len)
{
  if (!current_process)
    return -ESRCH;

  /*
   * spawn_kernel / ktest harness: syscalls accept kernel pointers on the
   * process kstack without copy_from_user. USER_MODE stays strict below.
   */
  if (current_process->mode == KERNEL_MODE)
    return 0;

  if (!is_user_address(str, max_len))
    return -EFAULT;

  return 0;
}

/**
 * validate_userspace_buffer - Validate that a buffer argument is in userspace
 * @buf: Buffer pointer to validate
 * @size: Size of buffer
 * Returns: 0 if valid, -EFAULT if invalid
 */
int validate_userspace_buffer(const void *buf, size_t size)
{
  if (!current_process)
    return -ESRCH;

  /*
   * spawn_kernel / ktest harness: syscalls accept kernel buffers on the
   * process kstack without copy_from_user. USER_MODE stays strict below.
   */
  if (current_process->mode == KERNEL_MODE)
    return 0;

  if (!is_user_address(buf, size))
    return -EFAULT;

  return 0;
}
