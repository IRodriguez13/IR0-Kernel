/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: named_symlink.c
 * Description: In-memory symbolic links for runsv supervise paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/named_symlink.h>
#include <ir0/errno.h>
#include <ir0/path.h>
#include <string.h>

#define NAMED_SYMLINK_MAX 32

struct named_symlink_entry
{
    char path[256];
    char target[256];
    int in_use;
};

static struct named_symlink_entry named_symlinks[NAMED_SYMLINK_MAX];

static struct named_symlink_entry *named_symlink_find(const char *path)
{
    int i;

    if (!path)
        return NULL;

    for (i = 0; i < NAMED_SYMLINK_MAX; i++)
    {
        if (named_symlinks[i].in_use &&
            strcmp(named_symlinks[i].path, path) == 0)
            return &named_symlinks[i];
    }
    return NULL;
}

int named_symlink_create(const char *linkpath, const char *target)
{
    struct named_symlink_entry *slot = NULL;
    int i;

    if (!linkpath || linkpath[0] != '/' || !target || target[0] == '\0')
        return -EINVAL;

    if (named_symlink_find(linkpath))
        return -EEXIST;

    for (i = 0; i < NAMED_SYMLINK_MAX; i++)
    {
        if (!named_symlinks[i].in_use)
        {
            slot = &named_symlinks[i];
            break;
        }
    }
    if (!slot)
        return -ENOSPC;

    strncpy(slot->path, linkpath, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';
    strncpy(slot->target, target, sizeof(slot->target) - 1);
    slot->target[sizeof(slot->target) - 1] = '\0';
    slot->in_use = 1;
    return 0;
}

int named_symlink_stat(const char *path, stat_t *buf)
{
    struct named_symlink_entry *e;
    size_t tlen;

    if (!path || !buf)
        return -EINVAL;

    e = named_symlink_find(path);
    if (!e)
        return -ENOENT;

    tlen = strlen(e->target);
    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFLNK | 0777;
    buf->st_nlink = 1;
    buf->st_size = (off_t)tlen;
    return 0;
}

const char *named_symlink_target(const char *path)
{
    struct named_symlink_entry *e;

    e = named_symlink_find(path);
    if (!e)
        return NULL;
    return e->target;
}

int named_symlink_unlink(const char *path)
{
    struct named_symlink_entry *e;

    e = named_symlink_find(path);
    if (!e)
        return -ENOENT;

    e->in_use = 0;
    e->path[0] = '\0';
    e->target[0] = '\0';
    return 0;
}

int ir0_follow_named_symlinks(char *path, size_t path_sz, unsigned max_depth)
{
    char component_path[256];
    char merged[256];
    unsigned depth = 0;

    if (!path || path_sz == 0 || path[0] != '/')
        return -EINVAL;

    for (;;)
    {
        const char *p;
        const char *symlink_target = NULL;
        size_t pos;

        p = path + 1;
        component_path[0] = '/';
        component_path[1] = '\0';
        pos = 1;

        while (*p)
        {
            const char *start = p;
            size_t len;

            while (*p && *p != '/')
                p++;

            len = (size_t)(p - start);
            if (pos + len >= sizeof(component_path))
                return -ENAMETOOLONG;

            memcpy(component_path + pos, start, len);
            pos += len;
            component_path[pos] = '\0';

            symlink_target = named_symlink_target(component_path);
            if (symlink_target)
                break;

            if (*p == '/')
            {
                if (pos + 1 >= sizeof(component_path))
                    return -ENAMETOOLONG;
                component_path[pos++] = '/';
                component_path[pos] = '\0';
                p++;
            }
        }

        if (!symlink_target)
            return 0;

        if (depth >= max_depth)
            return -ELOOP;

        depth++;

        {
            const char *rest = (*p != '\0') ? p : "";

            if (symlink_target[0] == '/')
            {
                if (rest[0] == '\0')
                {
                    if (normalize_path(symlink_target, path, path_sz) != 0)
                        return -ENAMETOOLONG;
                }
                else if (rest[0] == '/')
                {
                    if (join_paths(symlink_target, rest + 1, merged, path_sz) != 0)
                        return -ENAMETOOLONG;
                    if (normalize_path(merged, path, path_sz) != 0)
                        return -ENAMETOOLONG;
                }
                else
                {
                    if (join_paths(symlink_target, rest, merged, path_sz) != 0)
                        return -ENAMETOOLONG;
                    if (normalize_path(merged, path, path_sz) != 0)
                        return -ENAMETOOLONG;
                }
            }
            else
            {
                char parent[256];

                if (get_parent_path(component_path, parent,
                                    sizeof(parent)) != 0)
                    return -EINVAL;
                if (join_paths(parent, symlink_target, merged, path_sz) != 0)
                    return -ENAMETOOLONG;
                if (rest[0] != '\0')
                {
                    if (join_paths(merged, rest, merged, path_sz) != 0)
                        return -ENAMETOOLONG;
                }
                if (normalize_path(merged, path, path_sz) != 0)
                    return -ENAMETOOLONG;
            }
        }
    }
}
