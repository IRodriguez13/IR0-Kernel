/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: named_fifo.c
 * Description: In-memory named FIFO nodes for runsv supervise paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/named_fifo.h>
#include <ir0/errno.h>
#include <ir0/path.h>
#include <ir0/pipe.h>
#include <ir0/types.h>
#include <ir0/arch_port.h>
#include <string.h>

#define NAMED_FIFO_MAX 128

#ifdef TEST_HOST
static inline uint64_t named_fifo_irq_save(void)
{
    return 0;
}

static inline void named_fifo_irq_restore(uint64_t flags)
{
    (void)flags;
}
#else
static inline uint64_t named_fifo_irq_save(void)
{
    return (uint64_t)irq_save();
}

static inline void named_fifo_irq_restore(uint64_t flags)
{
    irq_restore((unsigned long)flags);
}
#endif

struct named_fifo_entry
{
    char path[256];
    pipe_t *pipe;
    mode_t mode;
    int in_use;
};

static struct named_fifo_entry named_fifos[NAMED_FIFO_MAX];

int named_fifo_is_runsv_supervise_regular_path(const char *path)
{
    static const char *suffixes[] = {
        "/supervise/pid",
        "/supervise/pid.new",
        "/supervise/lock",
        "/supervise/stat",
        "/supervise/stat.new",
        "/supervise/status",
        "/supervise/status.new",
        "/log/supervise/pid",
        "/log/supervise/pid.new",
        "/log/supervise/lock",
        "/log/supervise/stat",
        "/log/supervise/stat.new",
        "/log/supervise/status",
        "/log/supervise/status.new",
        NULL
    };
    size_t plen;
    int i;

    if (!path)
        return 0;

    plen = strlen(path);
    for (i = 0; suffixes[i]; i++)
    {
        size_t slen = strlen(suffixes[i]);

        if (plen >= slen && strcmp(path + plen - slen, suffixes[i]) == 0)
            return 1;
    }
    return 0;
}

int named_fifo_is_runsv_supervise_path(const char *path)
{
    static const char *suffixes[] = {
        "/supervise/control",
        "/supervise/ok",
        "/log/supervise/control",
        "/log/supervise/ok",
        NULL
    };
    size_t plen;
    int i;

    if (!path)
        return 0;

    plen = strlen(path);
    for (i = 0; suffixes[i]; i++)
    {
        size_t slen = strlen(suffixes[i]);

        if (plen >= slen && strcmp(path + plen - slen, suffixes[i]) == 0)
            return 1;
    }
    return 0;
}

static struct named_fifo_entry *named_fifo_find(const char *path)
{
    char norm[256];
    int i;

    if (!path)
        return NULL;

    if (normalize_path(path, norm, sizeof(norm)) != 0)
        return NULL;

    for (i = 0; i < NAMED_FIFO_MAX; i++)
    {
        if (named_fifos[i].in_use && strcmp(named_fifos[i].path, norm) == 0)
            return &named_fifos[i];
    }
    return NULL;
}

int named_fifo_create(const char *path, mode_t mode)
{
    struct named_fifo_entry *slot = NULL;
    pipe_t *pipe;
    uint64_t irq_flags;
    int i;

    if (!path || path[0] != '/')
        return -EINVAL;

    irq_flags = named_fifo_irq_save();

    if (named_fifo_find(path))
    {
        named_fifo_irq_restore(irq_flags);
        return 0;
    }

    for (i = 0; i < NAMED_FIFO_MAX; i++)
    {
        if (!named_fifos[i].in_use)
        {
            slot = &named_fifos[i];
            break;
        }
    }
    if (!slot)
    {
        for (i = 0; i < NAMED_FIFO_MAX; i++)
        {
            if (named_fifos[i].in_use && named_fifos[i].pipe &&
                named_fifos[i].pipe->fd_refs <= 0)
            {
                if (named_fifos[i].pipe)
                    pipe_abort_unopened(named_fifos[i].pipe);
                named_fifos[i].in_use = 0;
                named_fifos[i].path[0] = '\0';
                named_fifos[i].pipe = NULL;
                slot = &named_fifos[i];
                break;
            }
        }
    }
    if (!slot)
    {
        named_fifo_irq_restore(irq_flags);
        return -ENOSPC;
    }

    pipe = pipe_create();
    if (!pipe)
    {
        named_fifo_irq_restore(irq_flags);
        return -ENOMEM;
    }
    /* Inode owns pipe_t until unlink/recycle (see pipe_close_end). */
    pipe->named = 1;

    if (normalize_path(path, slot->path, sizeof(slot->path)) != 0)
    {
        pipe_abort_unopened(pipe);
        named_fifo_irq_restore(irq_flags);
        return -ENAMETOOLONG;
    }
    slot->pipe = pipe;
    slot->mode = (mode_t)(S_IFIFO | (mode & 0777));
    slot->in_use = 1;
    named_fifo_irq_restore(irq_flags);
    return 0;
}

int named_fifo_stat(const char *path, stat_t *buf)
{
    struct named_fifo_entry *e;
    uint64_t irq_flags;

    if (!path || !buf)
        return -EINVAL;

    irq_flags = named_fifo_irq_save();
    e = named_fifo_find(path);
    if (!e)
    {
        named_fifo_irq_restore(irq_flags);
        return -ENOENT;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_mode = e->mode;
    buf->st_nlink = 1;
    named_fifo_irq_restore(irq_flags);
    return 0;
}

pipe_t *named_fifo_lookup(const char *path)
{
    struct named_fifo_entry *e;

    e = named_fifo_find(path);
    if (!e)
        return NULL;
    return e->pipe;
}

int named_fifo_unlink(const char *path)
{
    struct named_fifo_entry *e;
    uint64_t irq_flags;

    irq_flags = named_fifo_irq_save();
    e = named_fifo_find(path);
    if (!e)
    {
        named_fifo_irq_restore(irq_flags);
        return -ENOENT;
    }

    if (e->pipe)
    {
        if (e->pipe->fd_refs <= 0)
            pipe_abort_unopened(e->pipe);
        else
            /* Still open: last pipe_close_end will free. */
            e->pipe->named = 0;
    }

    e->in_use = 0;
    e->path[0] = '\0';
    e->pipe = NULL;
    named_fifo_irq_restore(irq_flags);
    return 0;
}
