/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pseudo_fs_registry.c
 * Description: Longest-prefix registry for /proc and /sys pseudo-fs endpoints.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/pseudo_fs.h>
#include <ir0/clock.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/sysfs.h>
#include <string.h>

static pseudo_fs_entry_t g_proc_entries[PSEUDO_FS_MAX_ENTRIES];
static pseudo_fs_entry_t g_sys_entries[PSEUDO_FS_MAX_ENTRIES];
static pseudo_fs_entry_t g_heart_entries[PSEUDO_FS_MAX_ENTRIES];
static int g_proc_count;
static int g_sys_count;
static int g_heart_count;

typedef struct pseudo_fs_dynamic_reg
{
    char mount_prefix[64];
    pseudo_fs_dynamic_match_fn match;
    const pseudo_fs_ops_t *ops;
    int in_use;
} pseudo_fs_dynamic_reg_t;

typedef struct pseudo_fs_dynamic_open
{
    int in_use;
    int fd;
    const pseudo_fs_ops_t *ops;
    void *ctx;
} pseudo_fs_dynamic_open_t;

static pseudo_fs_dynamic_reg_t g_dyn_regs[PSEUDO_FS_MAX_DYNAMIC];
static int g_dyn_reg_count;
static pseudo_fs_dynamic_open_t g_dyn_opens[PSEUDO_FS_MAX_DYNAMIC];

static void pseudo_fs_normalize(const char *in, char *out, size_t out_sz)
{
    size_t len;

    if (!out || out_sz == 0)
        return;

    out[0] = '\0';
    if (!in || !in[0])
        return;

    if (in[0] != '/')
    {
        snprintf(out, out_sz, "/%s", in);
        return;
    }

    strncpy(out, in, out_sz - 1);
    out[out_sz - 1] = '\0';

    len = strlen(out);
    while (len > 1 && out[len - 1] == '/')
    {
        out[len - 1] = '\0';
        len--;
    }
}

static int pseudo_fs_build_full_path(const char *mount_prefix, const char *rel_path,
                                     char *full_path, size_t full_sz)
{
    char prefix[128];
    char rel[128];

    pseudo_fs_normalize(mount_prefix, prefix, sizeof(prefix));
    pseudo_fs_normalize(rel_path, rel, sizeof(rel));

    if (!prefix[0] || !rel[0])
        return -EINVAL;

    if (rel[0] == '/')
    {
        if (snprintf(full_path, full_sz, "%s%s", prefix, rel) >= (int)full_sz)
            return -ENAMETOOLONG;
    }
    else if (snprintf(full_path, full_sz, "%s/%s", prefix, rel) >= (int)full_sz)
    {
        return -ENAMETOOLONG;
    }

    return 0;
}

static int pseudo_fs_register_table(pseudo_fs_entry_t *table, int *count, int fd_base,
                                    const char *mount_prefix, const char *rel_path,
                                    const pseudo_fs_ops_t *ops, void *ctx)
{
    pseudo_fs_entry_t *slot;
    char full_path[256];
    int rc;

    if (!table || !count || !mount_prefix || !rel_path || !ops)
        return -EINVAL;

    if (*count >= PSEUDO_FS_MAX_ENTRIES)
        return -ENOSPC;

    rc = pseudo_fs_build_full_path(mount_prefix, rel_path, full_path, sizeof(full_path));
    if (rc != 0)
        return rc;

    for (int i = 0; i < *count; i++)
    {
        if (table[i].in_use && strcmp(table[i].full_path, full_path) == 0)
            return -EEXIST;
    }

    slot = &table[*count];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->full_path, full_path, sizeof(slot->full_path) - 1);
    slot->full_path[sizeof(slot->full_path) - 1] = '\0';
    slot->ops = ops;
    slot->ctx = ctx;
    slot->fd = fd_base + *count;
    slot->in_use = 1;
    (*count)++;
    return 0;
}

static const pseudo_fs_entry_t *pseudo_fs_lookup_table(const pseudo_fs_entry_t *table,
                                                       int count, const char *full_path)
{
    char norm[256];
    size_t best_len = 0;
    const pseudo_fs_entry_t *best = NULL;

    if (!table || !full_path)
        return NULL;

    pseudo_fs_normalize(full_path, norm, sizeof(norm));

    for (int i = 0; i < count; i++)
    {
        size_t plen;

        if (!table[i].in_use)
            continue;

        plen = strlen(table[i].full_path);
        if (strncmp(table[i].full_path, norm, plen) != 0)
            continue;

        if (norm[plen] != '\0' && norm[plen] != '/')
            continue;

        if (plen >= best_len)
        {
            best_len = plen;
            best = &table[i];
        }
    }

    return best;
}

/* 1 if lookup hit is the exact path (not a parent prefix of a longer path). */
static int pseudo_fs_entry_is_exact(const pseudo_fs_entry_t *entry,
				    const char *full_path)
{
	char norm[256];

	if (!entry || !full_path)
		return 0;
	pseudo_fs_normalize(full_path, norm, sizeof(norm));
	return strcmp(entry->full_path, norm) == 0;
}

static const pseudo_fs_entry_t *pseudo_fs_find_by_fd_table(const pseudo_fs_entry_t *table,
                                                           int count, int fd)
{
    for (int i = 0; i < count; i++)
    {
        if (table[i].in_use && table[i].fd == fd)
            return &table[i];
    }

    return NULL;
}

int pseudo_fs_register(const char *mount_prefix, const char *rel_path,
                       const pseudo_fs_ops_t *ops, void *ctx)
{
    if (!mount_prefix || !rel_path)
        return -EINVAL;

    if (strncmp(mount_prefix, "/sys", 4) == 0)
    {
        return pseudo_fs_register_table(g_sys_entries, &g_sys_count, PSEUDO_FS_SYS_FD_BASE,
                                      mount_prefix, rel_path, ops, ctx);
    }

    if (strncmp(mount_prefix, "/heart", 6) == 0)
    {
        return pseudo_fs_register_table(g_heart_entries, &g_heart_count,
                                        PSEUDO_FS_HEART_FD_BASE,
                                        mount_prefix, rel_path, ops, ctx);
    }

    return pseudo_fs_register_table(g_proc_entries, &g_proc_count, PSEUDO_FS_PROC_FD_BASE,
                                    mount_prefix, rel_path, ops, ctx);
}

int pseudo_fs_register_dynamic(const char *mount_prefix,
                               pseudo_fs_dynamic_match_fn match,
                               const pseudo_fs_ops_t *ops)
{
    pseudo_fs_dynamic_reg_t *slot;
    char prefix[64];

    if (!mount_prefix || !match || !ops)
        return -EINVAL;

    if (g_dyn_reg_count >= PSEUDO_FS_MAX_DYNAMIC)
        return -ENOSPC;

    pseudo_fs_normalize(mount_prefix, prefix, sizeof(prefix));

    for (int i = 0; i < g_dyn_reg_count; i++)
    {
        if (g_dyn_regs[i].in_use && strcmp(g_dyn_regs[i].mount_prefix, prefix) == 0 &&
            g_dyn_regs[i].match == match)
        {
            return -EEXIST;
        }
    }

    slot = &g_dyn_regs[g_dyn_reg_count];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->mount_prefix, prefix, sizeof(slot->mount_prefix) - 1);
    slot->mount_prefix[sizeof(slot->mount_prefix) - 1] = '\0';
    slot->match = match;
    slot->ops = ops;
    slot->in_use = 1;
    g_dyn_reg_count++;
    return 0;
}

static const pseudo_fs_dynamic_open_t *pseudo_fs_find_dynamic_by_fd(int fd)
{
    for (int i = 0; i < PSEUDO_FS_MAX_DYNAMIC; i++)
    {
        if (g_dyn_opens[i].in_use && g_dyn_opens[i].fd == fd)
            return &g_dyn_opens[i];
    }

    return NULL;
}

static int pseudo_fs_dynamic_try_match(const char *full_path, void **out_ctx,
                                       const pseudo_fs_ops_t **out_ops)
{
    char norm[256];
    int best_prefix = -1;
    size_t best_len = 0;

    if (!full_path || !out_ctx || !out_ops)
        return -EINVAL;

    pseudo_fs_normalize(full_path, norm, sizeof(norm));

    for (int i = 0; i < g_dyn_reg_count; i++)
    {
        size_t plen;

        if (!g_dyn_regs[i].in_use)
            continue;

        plen = strlen(g_dyn_regs[i].mount_prefix);
        if (strncmp(g_dyn_regs[i].mount_prefix, norm, plen) != 0)
            continue;

        if (norm[plen] != '\0' && norm[plen] != '/')
            continue;

        if (plen >= best_len)
        {
            best_len = plen;
            best_prefix = i;
        }
    }

    if (best_prefix < 0)
        return -ENOENT;

    if (g_dyn_regs[best_prefix].match(full_path, out_ctx) != 0)
        return -ENOENT;

    *out_ops = g_dyn_regs[best_prefix].ops;
    return 0;
}

const pseudo_fs_entry_t *pseudo_fs_lookup(const char *full_path)
{
    const pseudo_fs_entry_t *hit;

    if (!full_path)
        return NULL;

    hit = pseudo_fs_lookup_table(g_proc_entries, g_proc_count, full_path);
    if (hit)
        return hit;

    hit = pseudo_fs_lookup_table(g_sys_entries, g_sys_count, full_path);
    if (hit)
        return hit;

    return pseudo_fs_lookup_table(g_heart_entries, g_heart_count, full_path);
}

const pseudo_fs_entry_t *pseudo_fs_find_by_fd(int fd)
{
    const pseudo_fs_entry_t *hit;

    if (pseudo_fs_find_dynamic_by_fd(fd))
        return (const pseudo_fs_entry_t *)(uintptr_t)1;

    hit = pseudo_fs_find_by_fd_table(g_proc_entries, g_proc_count, fd);
    if (hit)
        return hit;

    hit = pseudo_fs_find_by_fd_table(g_sys_entries, g_sys_count, fd);
    if (hit)
        return hit;

    return pseudo_fs_find_by_fd_table(g_heart_entries, g_heart_count, fd);
}

void pseudo_fs_reset(void)
{
    for (int i = 0; i < PSEUDO_FS_MAX_DYNAMIC; i++)
    {
        if (g_dyn_opens[i].in_use && g_dyn_opens[i].ops && g_dyn_opens[i].ops->close)
            g_dyn_opens[i].ops->close(g_dyn_opens[i].ctx);
        else if (g_dyn_opens[i].in_use && g_dyn_opens[i].ctx)
            kfree(g_dyn_opens[i].ctx);

        memset(&g_dyn_opens[i], 0, sizeof(g_dyn_opens[i]));
    }

    memset(g_proc_entries, 0, sizeof(g_proc_entries));
    memset(g_sys_entries, 0, sizeof(g_sys_entries));
    memset(g_heart_entries, 0, sizeof(g_heart_entries));
    memset(g_dyn_regs, 0, sizeof(g_dyn_regs));
    g_proc_count = 0;
    g_sys_count = 0;
    g_heart_count = 0;
    g_dyn_reg_count = 0;
}

int pseudo_fs_proc_init(void)
{
    return 0;
}

int pseudo_fs_sys_init(void)
{
    return 0;
}

int64_t pseudo_fs_read_fd(int fd, char *buf, size_t count, off_t offset)
{
    static char full_buf[4096];
    const pseudo_fs_entry_t *entry;
    const pseudo_fs_dynamic_open_t *dyn;
    off_t inner = 0;
    int64_t full;
    size_t to_copy;

    if (!buf || count == 0)
        return 0;

    dyn = pseudo_fs_find_dynamic_by_fd(fd);
    if (dyn)
    {
        if (!dyn->ops || !dyn->ops->read)
            return -EBADF;

        full = dyn->ops->read(dyn->ctx, full_buf, sizeof(full_buf), &inner);
        if (full < 0)
            return full;

        if (offset >= full)
            return 0;

        to_copy = (size_t)full - (size_t)offset;
        if (to_copy > count)
            to_copy = count;

        memcpy(buf, full_buf + (size_t)offset, to_copy);
        return (int64_t)to_copy;
    }

    entry = pseudo_fs_find_by_fd_table(g_proc_entries, g_proc_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_sys_entries, g_sys_count, fd);
    if (!entry || !entry->ops || !entry->ops->read)
        return -EBADF;

    full = entry->ops->read(entry->ctx, full_buf, sizeof(full_buf), &inner);
    if (full < 0)
        return full;

    if (offset >= full)
        return 0;

    to_copy = (size_t)full - (size_t)offset;
    if (to_copy > count)
        to_copy = count;

    memcpy(buf, full_buf + (size_t)offset, to_copy);
    return (int64_t)to_copy;
}

int64_t pseudo_fs_write_fd(int fd, const char *buf, size_t count)
{
    const pseudo_fs_entry_t *entry;
    const pseudo_fs_dynamic_open_t *dyn;

    dyn = pseudo_fs_find_dynamic_by_fd(fd);
    if (dyn)
    {
        if (!dyn->ops || !dyn->ops->write)
            return -EBADF;

        return dyn->ops->write(dyn->ctx, buf, count);
    }

    entry = pseudo_fs_find_by_fd_table(g_proc_entries, g_proc_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_sys_entries, g_sys_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_heart_entries, g_heart_count, fd);
    if (!entry || !entry->ops || !entry->ops->write)
        return -EBADF;

    return entry->ops->write(entry->ctx, buf, count);
}

void pseudo_fs_stat_now(stat_t *st)
{
    time_t now;

    if (!st)
        return;

    now = clock_get_current_time();
    st->st_atime = now;
    st->st_mtime = now;
    st->st_ctime = now;
}

int pseudo_fs_stat_fd(int fd, stat_t *st)
{
    const pseudo_fs_entry_t *entry;
    pseudo_fs_dynamic_open_t *dyn;

    if (!st)
        return -EINVAL;

    dyn = (pseudo_fs_dynamic_open_t *)pseudo_fs_find_dynamic_by_fd(fd);
    if (dyn)
    {
        if (!dyn->ops || !dyn->ops->stat)
            return -EBADF;

        return dyn->ops->stat(dyn->ctx, st);
    }

    entry = pseudo_fs_find_by_fd_table(g_proc_entries, g_proc_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_sys_entries, g_sys_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_heart_entries, g_heart_count, fd);
    if (!entry)
        return -ENOENT;

    if (entry->ops && entry->ops->stat)
        return entry->ops->stat(entry->ctx, st);

    return pseudo_fs_stat_path(entry->full_path, st);
}

int pseudo_fs_stat_path(const char *full_path, stat_t *st)
{
    const pseudo_fs_entry_t *entry;
    void *ctx = NULL;
    const pseudo_fs_ops_t *ops = NULL;
    int rc;

    if (!full_path || !st)
        return -EINVAL;

    entry = pseudo_fs_lookup(full_path);
    if (entry && pseudo_fs_entry_is_exact(entry, full_path) && entry->ops &&
	entry->ops->stat)
        return entry->ops->stat(entry->ctx, st);

    rc = pseudo_fs_dynamic_try_match(full_path, &ctx, &ops);
    if (rc != 0)
    {
	if (entry && pseudo_fs_entry_is_exact(entry, full_path) && entry->ops &&
	    entry->ops->stat)
		return entry->ops->stat(entry->ctx, st);
	return rc;
    }

    if (!ops || !ops->stat)
    {
        if (ctx && ops && ops->close)
            ops->close(ctx);
        else if (ctx)
            kfree(ctx);
        return -ENOENT;
    }

    rc = ops->stat(ctx, st);
    if (ops->close)
        ops->close(ctx);
    else if (ctx)
        kfree(ctx);

    return rc;
}

int64_t pseudo_fs_acquire_path(const char *full_path, int flags,
			       const pseudo_fs_ops_t **out_ops, void **out_ctx,
			       int *out_dynamic)
{
	const pseudo_fs_entry_t *entry;
	void *ctx = NULL;
	const pseudo_fs_ops_t *ops = NULL;
	int64_t rc;

	if (!full_path || !out_ops || !out_ctx || !out_dynamic)
		return -EINVAL;

	*out_ops = NULL;
	*out_ctx = NULL;
	*out_dynamic = 0;

	entry = pseudo_fs_lookup(full_path);
	if (entry && pseudo_fs_entry_is_exact(entry, full_path))
	{
		if (entry->ops && entry->ops->open)
		{
			rc = entry->ops->open(entry->ctx, flags);
			if (rc < 0)
				return rc;
		}
		*out_ops = entry->ops;
		*out_ctx = entry->ctx;
		*out_dynamic = 0;
		return 0;
	}

	/*
	 * Prefix-only hits (e.g. /sys/class/net for …/eth0/address) must not
	 * steal leaf opens — try dynamic matchers first.
	 */
	rc = pseudo_fs_dynamic_try_match(full_path, &ctx, &ops);
	if (rc != 0)
		return rc;

	if (ops->open)
	{
		rc = ops->open(ctx, flags);
		if (rc < 0)
		{
			if (ops->close)
				ops->close(ctx);
			else
				kfree(ctx);
			return rc;
		}
	}

	*out_ops = ops;
	*out_ctx = ctx;
	*out_dynamic = 1;
	return 0;
}

int64_t pseudo_fs_release_ops(const pseudo_fs_ops_t *ops, void *ctx, int dynamic)
{
	if (!dynamic)
		return 0;
	if (ops && ops->close)
		return ops->close(ctx);
	if (ctx)
		kfree(ctx);
	return 0;
}

int64_t pseudo_fs_ops_read(const pseudo_fs_ops_t *ops, void *ctx, char *buf,
			   size_t count, off_t *offset)
{
	static char full_buf[4096];
	off_t gen_off = 0;
	int64_t full;
	size_t to_copy;

	if (!ops || !ops->read || !buf || !offset)
		return -EINVAL;

	/*
	 * Always regenerate from offset 0 into a bounce buffer, then slice.
	 * Many backends ignore *offset; BusyBox full_read() would otherwise
	 * re-read the same payload (cmdline "sh" → "shshshsh…", "busybox"×N).
	 */
	full = ops->read(ctx, full_buf, sizeof(full_buf), &gen_off);
	if (full < 0)
		return full;

	if (*offset >= full)
		return 0;

	to_copy = (size_t)full - (size_t)*offset;
	if (to_copy > count)
		to_copy = count;

	memcpy(buf, full_buf + (size_t)*offset, to_copy);
	*offset += (off_t)to_copy;
	return (int64_t)to_copy;
}

int64_t pseudo_fs_ops_write(const pseudo_fs_ops_t *ops, void *ctx,
			    const char *buf, size_t count)
{
	if (!ops || !ops->write || !buf)
		return -EINVAL;
	return ops->write(ctx, buf, count);
}

int pseudo_fs_ops_stat(const pseudo_fs_ops_t *ops, void *ctx, stat_t *st)
{
	if (!ops || !ops->stat || !st)
		return -EINVAL;
	return ops->stat(ctx, st);
}

/*
 * Legacy: assign a global PSEUDO_FS_*_FD_BASE virtual fd (host tests / old callers).
 * Kernel syscall open must use pseudo_fs_acquire_path + process fd_table instead.
 */
int64_t pseudo_fs_open_path(const char *full_path, int flags, int *out_fd)
{
	const pseudo_fs_entry_t *entry;
	int64_t rc;

	if (!full_path || !out_fd)
		return -EINVAL;

	entry = pseudo_fs_lookup(full_path);
	if (entry && pseudo_fs_entry_is_exact(entry, full_path))
	{
		if (entry->ops && entry->ops->open)
		{
			rc = entry->ops->open(entry->ctx, flags);
			if (rc < 0)
				return rc;
		}

		*out_fd = entry->fd;
		return 0;
	}

	{
		void *ctx = NULL;
		const pseudo_fs_ops_t *ops = NULL;
		pseudo_fs_dynamic_open_t *slot = NULL;

		rc = pseudo_fs_dynamic_try_match(full_path, &ctx, &ops);
		if (rc != 0)
			return rc;

		for (int i = 0; i < PSEUDO_FS_MAX_DYNAMIC; i++)
		{
			if (!g_dyn_opens[i].in_use)
			{
				slot = &g_dyn_opens[i];
				break;
			}
		}

		if (!slot)
		{
			if (ops->close)
				ops->close(ctx);
			else
				kfree(ctx);
			return -ENFILE;
		}

		if (ops->open)
		{
			rc = ops->open(ctx, flags);
			if (rc < 0)
			{
				if (ops->close)
					ops->close(ctx);
				else
					kfree(ctx);
				return rc;
			}
		}

		slot->in_use = 1;
		slot->fd = PSEUDO_FS_DYN_FD_BASE + (int)(slot - g_dyn_opens);
		slot->ops = ops;
		slot->ctx = ctx;
		*out_fd = slot->fd;
		return 0;
	}
}

int64_t pseudo_fs_close_fd(int fd)
{
    const pseudo_fs_entry_t *entry;
    pseudo_fs_dynamic_open_t *dyn;

    dyn = (pseudo_fs_dynamic_open_t *)pseudo_fs_find_dynamic_by_fd(fd);
    if (dyn)
    {
        int64_t rc = 0;

        if (dyn->ops && dyn->ops->close)
            rc = dyn->ops->close(dyn->ctx);
        else if (dyn->ctx)
            kfree(dyn->ctx);

        memset(dyn, 0, sizeof(*dyn));
        return rc;
    }

    entry = pseudo_fs_find_by_fd_table(g_proc_entries, g_proc_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_sys_entries, g_sys_count, fd);
    if (!entry)
        entry = pseudo_fs_find_by_fd_table(g_heart_entries, g_heart_count, fd);
    if (!entry)
        return -EBADF;

    if (entry->ops && entry->ops->close)
        return entry->ops->close(entry->ctx);

    return 0;
}

static int pseudo_fs_table_has_children(const pseudo_fs_entry_t *table, int count,
                                        const char *dir_path)
{
    char norm[256];
    size_t plen;

    if (!table || !dir_path)
        return 0;

    pseudo_fs_normalize(dir_path, norm, sizeof(norm));
    plen = strlen(norm);

    for (int i = 0; i < count; i++)
    {
        const char *rest;

        if (!table[i].in_use)
            continue;

        if (strncmp(table[i].full_path, norm, plen) != 0)
            continue;

        rest = table[i].full_path + plen;
        if (rest[0] == '\0')
            continue;
        if (rest[0] != '/')
            continue;

        rest++;
        if (rest[0] != '\0')
            return 1;
    }

    return 0;
}

int pseudo_fs_path_has_children(const char *path)
{
    if (!path)
        return 0;

    if (pseudo_fs_table_has_children(g_proc_entries, g_proc_count, path))
        return 1;

    if (pseudo_fs_table_has_children(g_sys_entries, g_sys_count, path))
        return 1;

    if (pseudo_fs_table_has_children(g_heart_entries, g_heart_count, path))
        return 1;

    return sys_class_net_path_has_children(path);
}

static int pseudo_fs_dirent_exists(struct vfs_dirent *entries, int n,
                                   const char *name)
{
    for (int i = 0; i < n; i++)
    {
        if (strcmp(entries[i].name, name) == 0)
            return 1;
    }

    return 0;
}

int pseudo_fs_collect_registry_children(const char *dir_path,
                                        struct vfs_dirent *entries,
                                        int max_entries, int start_n)
{
    char norm[256];
    size_t plen;
    int n;

    if (!dir_path || !entries || max_entries <= 0 || start_n < 0)
        return -EINVAL;

    pseudo_fs_normalize(dir_path, norm, sizeof(norm));
    plen = strlen(norm);
    n = start_n;

    for (int tbl = 0; tbl < 3; tbl++)
    {
        const pseudo_fs_entry_t *table;
        int count;

        if (tbl == 0)
        {
            table = g_proc_entries;
            count = g_proc_count;
        }
        else if (tbl == 1)
        {
            table = g_sys_entries;
            count = g_sys_count;
        }
        else
        {
            table = g_heart_entries;
            count = g_heart_count;
        }

        for (int i = 0; i < count && n < max_entries; i++)
        {
            const char *rest;
            const char *slash;
            char top[VFS_PATH_MAX];
            size_t len;

            if (!table[i].in_use)
                continue;

            if (strncmp(table[i].full_path, norm, plen) != 0)
                continue;

            rest = table[i].full_path + plen;
            if (rest[0] == '\0')
                continue;
            if (rest[0] != '/')
                continue;

            rest++;
            if (!rest[0])
                continue;

            slash = strchr(rest, '/');
            len = slash ? (size_t)(slash - rest) : strlen(rest);
            if (len == 0 || len >= sizeof(top))
                continue;

            memcpy(top, rest, len);
            top[len] = '\0';

            if (pseudo_fs_dirent_exists(entries, n, top))
                continue;

            strncpy(entries[n].name, top, sizeof(entries[n].name) - 1);
            entries[n].name[sizeof(entries[n].name) - 1] = '\0';
            entries[n].type = slash ? DT_DIR : DT_REG;
            n++;
        }
    }

    return sys_class_net_collect_children(norm, entries, max_entries, n);
}
