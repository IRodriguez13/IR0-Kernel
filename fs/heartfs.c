/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: heartfs.c
 * Description: /heart facade — reexports /proc|/sys + IR0 kernel meta + /heart/src
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/heartfs.h>
#include <ir0/pseudo_fs.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <ir0/version.h>
#include <string.h>

/* Generated at build time by scripts/gen_heart_src_blob.py */
#include <generated/heart_src_blob.h>

static int g_heart_nodes_ready;

static const char heart_readme_text[] =
    "IR0 /heart — unified read-only view of kernel identity and pseudo-fs.\n"
    "Does not replace /proc or /sys; those mounts remain authoritative.\n"
    "Layout: README, MANIFESTO, proc/, sys/, kernel/, src/, dennis/\n"
    "Start with: cat /heart/MANIFESTO\n"
    "Sources to edit: /heart/dennis/src (mount -t 9p dennis /heart/dennis/src)\n";

/*
 * Brief identity for guests (cat /heart/MANIFESTO). Keep short — serial TTY.
 */
static const char heart_manifesto_text[] =
    "IR0 — Independent Research Operating system\n"
    "\n"
    "A Unix-like research kernel with a Linux-compatible userspace ABI\n"
    "goal: musl, BusyBox, and simple desktop clients on x86-64 (ARM64\n"
    "bring-up in progress).\n"
    "\n"
    "What you are running:\n"
    "  - Kernel: IR0 (syscalls, VFS, MM, sched, /proc /sys /dev /heart)\n"
    "  - Userspace: ISD (IR0 Software Distribution) — runit + BusyBox\n"
    "  - Philosophy: honest surfaces, no fake success, Linux as ground\n"
    "    truth for ABI contracts when claimed.\n"
    "\n"
    "Explore:\n"
    "  cat /heart/MANIFESTO   this text\n"
    "  cat /heart/README      /heart layout\n"
    "  cat /heart/kernel/version\n"
    "  man IR0-boot           guest mandocs (BusyBox man + cat7)\n"
    "  ls /usr/ken/games      Ken Thompson nod — small games (Doom)\n"
    "  ls /heart/dennis/src   Dennis Ritchie nod — IR0 sources (9p/writable)\n"
    "\n"
    "Home: Documentation/ in the source tree; SETUP.md for QEMU.\n";

static int64_t heart_static_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    const char *text;
    size_t len;
    size_t off;
    size_t to_copy;

    text = (const char *)ctx;
    if (!text || !buf || !offset)
        return -EINVAL;

    len = strlen(text);
    off = (size_t)*offset;
    if (off >= len)
        return 0;

    to_copy = len - off;
    if (to_copy > count)
        to_copy = count;
    memcpy(buf, text + off, to_copy);
    *offset += (off_t)to_copy;
    return (int64_t)to_copy;
}

static int heart_static_stat(void *ctx, stat_t *st)
{
    const char *text;

    if (!st)
        return -EINVAL;
    text = (const char *)ctx;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = text ? (off_t)strlen(text) : 0;
    pseudo_fs_stat_now(st);
    return 0;
}

static const pseudo_fs_ops_t heart_static_ops = {
    .read = heart_static_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .stat = heart_static_stat,
};

static int64_t heart_src_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    const heart_src_file_t *file;
    size_t off;
    size_t to_copy;

    file = (const heart_src_file_t *)ctx;
    if (!file || !file->data || !buf || !offset)
        return -EINVAL;

    off = (size_t)*offset;
    if (off >= file->size)
        return 0;

    to_copy = file->size - off;
    if (to_copy > count)
        to_copy = count;
    memcpy(buf, file->data + off, to_copy);
    *offset += (off_t)to_copy;
    return (int64_t)to_copy;
}

static int heart_src_stat(void *ctx, stat_t *st)
{
    const heart_src_file_t *file;

    if (!st)
        return -EINVAL;
    file = (const heart_src_file_t *)ctx;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = file ? (off_t)file->size : 0;
    pseudo_fs_stat_now(st);
    return 0;
}

static const pseudo_fs_ops_t heart_src_ops = {
    .read = heart_src_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .stat = heart_src_stat,
};

static char g_heart_version_buf[128];
static char g_heart_build_buf[256];
static char g_heart_features_buf[256];

static void heart_fill_meta_buffers(void)
{
    snprintf(g_heart_version_buf, sizeof(g_heart_version_buf), "%s\n",
             IR0_VERSION_STRING);
    snprintf(g_heart_build_buf, sizeof(g_heart_build_buf),
             "%s %s by %s@%s with %s\n", IR0_BUILD_DATE, IR0_BUILD_TIME,
             IR0_BUILD_USER, IR0_BUILD_HOST, IR0_BUILD_CC);
    snprintf(g_heart_features_buf, sizeof(g_heart_features_buf),
             "pseudo_fs_registry\nheart_facade\nheart_src\nfd_table_pseudo\n");
}

int is_heart_path(const char *path)
{
    if (!path)
        return 0;
    if (strcmp(path, "/heart") == 0)
        return 1;
    return strncmp(path, "/heart/", 7) == 0;
}

int heart_is_dennis_vfs_path(const char *path)
{
    if (!path)
        return 0;
    if (strcmp(path, "/heart/dennis") == 0 ||
        strcmp(path, "/heart/dennis/") == 0)
        return 1;
    return strncmp(path, "/heart/dennis/", 14) == 0;
}

int heart_alias_canonical(const char *path, char *out, size_t out_sz)
{
    const char *rest;

    if (!path || !out || out_sz < 8)
        return 0;

    if (strncmp(path, "/heart/proc/", 12) == 0)
    {
        rest = path + 12;
        if (rest[0] == '\0')
            return 0;
        snprintf(out, out_sz, "/proc/%s", rest);
        return 1;
    }

    if (strncmp(path, "/heart/sys/", 11) == 0)
    {
        rest = path + 11;
        if (rest[0] == '\0')
            return 0;
        snprintf(out, out_sz, "/sys/%s", rest);
        return 1;
    }

    return 0;
}

int heart_is_virtual_subdir(const char *path)
{
    if (!path)
        return 0;

    /* Dennis playground is VFS (disk + optional 9p), not pseudo_fs. */
    if (heart_is_dennis_vfs_path(path))
        return 0;

    if (strcmp(path, "/heart") == 0 || strcmp(path, "/heart/") == 0)
        return 1;
    if (strcmp(path, "/heart/proc") == 0 || strcmp(path, "/heart/proc/") == 0)
        return 1;
    if (strcmp(path, "/heart/sys") == 0 || strcmp(path, "/heart/sys/") == 0)
        return 1;
    if (strcmp(path, "/heart/kernel") == 0 || strcmp(path, "/heart/kernel/") == 0)
        return 1;
    if (strcmp(path, "/heart/src") == 0 || strcmp(path, "/heart/src/") == 0)
        return 1;

    /* Intermediate dirs under /heart/src/... */
    if (strncmp(path, "/heart/src/", 11) == 0 &&
        pseudo_fs_path_has_children(path))
        return 1;

    return 0;
}

static void heart_fill_dir_stat(stat_t *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    st->st_nlink = 2;
    pseudo_fs_stat_now(st);
}

int heart_stat(const char *path, stat_t *st)
{
    char canon[256];
    const pseudo_fs_entry_t *pf;

    if (!path || !st)
        return -EINVAL;
    if (!is_heart_path(path))
        return -EINVAL;

    heart_nodes_register();

    if (heart_is_dennis_vfs_path(path))
        return vfs_stat(path, st);

    if (heart_is_virtual_subdir(path))
    {
        heart_fill_dir_stat(st);
        return 0;
    }

    if (heart_alias_canonical(path, canon, sizeof(canon)))
    {
        pf = pseudo_fs_lookup(canon);
        if (pf && pf->ops && pf->ops->stat)
            return pf->ops->stat(pf->ctx, st);
        return pseudo_fs_stat_path(canon, st);
    }

    pf = pseudo_fs_lookup(path);
    if (pf && pf->ops && pf->ops->stat)
        return pf->ops->stat(pf->ctx, st);

    return pseudo_fs_stat_path(path, st);
}

static int heart_add_dirent(struct vfs_dirent *entries, int max_entries, int n,
                            const char *name, unsigned char type)
{
    if (n < 0 || n >= max_entries || !name || !name[0])
        return n;

    strncpy(entries[n].name, name, sizeof(entries[n].name) - 1);
    entries[n].name[sizeof(entries[n].name) - 1] = '\0';
    entries[n].type = type;
    return n + 1;
}

int heart_getdents(const char *path, struct vfs_dirent *entries, int max_entries)
{
    int n = 0;

    if (!path || !entries || max_entries <= 0)
        return -EINVAL;

    heart_nodes_register();

    if (strcmp(path, "/heart") == 0 || strcmp(path, "/heart/") == 0)
    {
        n = heart_add_dirent(entries, max_entries, n, "README", DT_REG);
        n = heart_add_dirent(entries, max_entries, n, "MANIFESTO", DT_REG);
        n = heart_add_dirent(entries, max_entries, n, "proc", DT_DIR);
        n = heart_add_dirent(entries, max_entries, n, "sys", DT_DIR);
        n = heart_add_dirent(entries, max_entries, n, "kernel", DT_DIR);
        n = heart_add_dirent(entries, max_entries, n, "src", DT_DIR);
        n = heart_add_dirent(entries, max_entries, n, "dennis", DT_DIR);
        return n;
    }

    /*
     * /heart/dennis is VFS — callers should use vfs_readdir. If we are still
     * invoked, report empty rather than inventing pseudo children.
     */
    if (heart_is_dennis_vfs_path(path))
        return -ENOTDIR;

    if (strcmp(path, "/heart/proc") == 0 || strcmp(path, "/heart/proc/") == 0)
        return pseudo_fs_collect_registry_children("/proc", entries, max_entries, 0);

    if (strcmp(path, "/heart/sys") == 0 || strcmp(path, "/heart/sys/") == 0)
        return pseudo_fs_collect_registry_children("/sys", entries, max_entries, 0);

    if (strcmp(path, "/heart/kernel") == 0 || strcmp(path, "/heart/kernel/") == 0)
        return pseudo_fs_collect_registry_children("/heart/kernel", entries,
                                                   max_entries, 0);

    if (strcmp(path, "/heart/src") == 0 || strcmp(path, "/heart/src/") == 0 ||
        (strncmp(path, "/heart/src/", 11) == 0 && heart_is_virtual_subdir(path)))
        return pseudo_fs_collect_registry_children(path, entries, max_entries, 0);

    return -ENOTDIR;
}

void heart_nodes_register(void)
{
    unsigned i;

    if (g_heart_nodes_ready)
        return;

    heart_fill_meta_buffers();

    pseudo_fs_register("/heart", "README", &heart_static_ops,
                       (void *)heart_readme_text);
    pseudo_fs_register("/heart", "MANIFESTO", &heart_static_ops,
                       (void *)heart_manifesto_text);
    pseudo_fs_register("/heart", "kernel/version", &heart_static_ops,
                       (void *)g_heart_version_buf);
    pseudo_fs_register("/heart", "kernel/build", &heart_static_ops,
                       (void *)g_heart_build_buf);
    pseudo_fs_register("/heart", "kernel/features", &heart_static_ops,
                       (void *)g_heart_features_buf);

    for (i = 0; i < HEART_SRC_FILE_COUNT; i++)
    {
        char rel[192];

        snprintf(rel, sizeof(rel), "src/%s", heart_src_files[i].rel_path);
        pseudo_fs_register("/heart", rel, &heart_src_ops,
                           (void *)&heart_src_files[i]);
    }

    g_heart_nodes_ready = 1;
}
