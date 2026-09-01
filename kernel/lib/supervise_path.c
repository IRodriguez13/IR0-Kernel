/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: supervise_path.c
 * Description: runsv supervise path stale-VFS purge (tier-1 runit)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/supervise_path.h>
#include <ir0/named_fifo.h>
#include <ir0/open_flags.h>
#include <ir0/vfs.h>
#include <ir0/errno.h>

static int ir0_supervise_clear_stale_regular(const char *path)
{
  if (!path)
    return -EINVAL;
  if (!named_fifo_is_runsv_supervise_regular_path(path))
    return 0;
  return vfs_clear_stale_for_regular_file(path);
}

int ir0_supervise_prepare_open(const char *path, int ir0_flags)
{
  int rc;
  stat_t vst;

  if (!path)
    return -EINVAL;

  if (named_fifo_is_runsv_supervise_regular_path(path) &&
      (ir0_flags & IR0_O_CREAT))
  {
    rc = ir0_supervise_clear_stale_regular(path);
    if (rc != 0)
      return rc;
  }

  if (named_fifo_is_runsv_supervise_path(path) &&
      !named_fifo_lookup(path))
  {
    if (vfs_stat(path, &vst) == 0 && !S_ISFIFO(vst.st_mode))
    {
      rc = vfs_clear_stale_for_regular_file(path);
      if (rc != 0)
        return rc;
    }
  }

  return 0;
}

int ir0_supervise_prepare_rename(const char *path)
{
  return ir0_supervise_clear_stale_regular(path);
}

int ir0_supervise_clear_stale_if_not_fifo(const char *path, const stat_t *vfs_st)
{
  if (!path || !vfs_st)
    return -EINVAL;
  if (!named_fifo_is_runsv_supervise_path(path) || S_ISFIFO(vfs_st->st_mode))
    return 0;
  return vfs_clear_stale_for_regular_file(path);
}
