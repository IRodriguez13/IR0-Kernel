/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stat_user.c
 * Description: kernel stat_t → Linux/musl userspace struct stat conversion
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/stat_user.h>
#include <ir0/copy_user.h>
#include <string.h>

void ir0_stat_to_user(const stat_t *kst, struct ir0_user_stat *ust)
{
    if (!kst || !ust)
        return;

    memset(ust, 0, sizeof(*ust));
    ust->st_dev = (uint64_t)kst->st_dev;
    ust->st_ino = (uint64_t)kst->st_ino;
    ust->st_nlink = (unsigned long)kst->st_nlink;
    ust->st_mode = (unsigned)kst->st_mode;
    ust->st_uid = (unsigned)kst->st_uid;
    ust->st_gid = (unsigned)kst->st_gid;
    ust->st_rdev = (uint64_t)kst->st_rdev;
    ust->st_size = (int64_t)kst->st_size;
    ust->st_blksize = (int64_t)kst->st_blksize;
    ust->st_blocks = (int64_t)kst->st_blocks;
    ust->st_atim.tv_sec = (int64_t)kst->st_atime;
    ust->st_mtim.tv_sec = (int64_t)kst->st_mtime;
    ust->st_ctim.tv_sec = (int64_t)kst->st_ctime;
}

int ir0_copy_stat_to_user(void *user_buf, const stat_t *kst)
{
    struct ir0_user_stat ust;

    if (!user_buf || !kst)
        return -1;

    ir0_stat_to_user(kst, &ust);
    if (copy_to_user(user_buf, &ust, sizeof(ust)) != 0)
        return -1;
    return 0;
}
