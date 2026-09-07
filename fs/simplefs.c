/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: simplefs.c
 * Description: Minimal mountable filesystem engine for simplefs/fat16-like backends.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "vfs.h"
#include "simplefs.h"
#include <config.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <ir0/clock.h>
#include <ir0/blockdev.h>
#include <ir0/credentials.h>
#include <ir0/permissions.h>
#include <string.h>

#if CONFIG_ENABLE_FS_FAT16
#include "fat16_disk.h"
#endif

#define SIMPLEFS_MAX_STORES 16
#define SIMPLEFS_MAX_MOUNTS 32
#define SIMPLEFS_MAX_ENTRIES 128
#define SIMPLEFS_MAX_NAME_LEN 63
#define SIMPLEFS_MAX_FILE_SIZE 65536

typedef struct simplefs_entry
{
    int in_use;
    int is_dir;
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    mode_t mode;
    uid_t uid;
    gid_t gid;
    uint32_t ino;
    uint8_t *data;
    size_t size;
    time_t mtime;
} simplefs_entry_t;

typedef struct simplefs_store
{
    int in_use;
    int mount_refcount;
    char fs_name[16];
    char dev[64];
    int strict_83_names;
    uint32_t next_ino;
    simplefs_entry_t entries[SIMPLEFS_MAX_ENTRIES];
} simplefs_store_t;

typedef struct simplefs_mount
{
    int in_use;
    char fs_name[16];
    char mount_path[256];
    simplefs_store_t *store;
} simplefs_mount_t;

typedef struct simplefs_backend
{
    int in_use;
    int strict_83_names;
    char fs_name[16];
    struct vfs_ops ops;
    struct vfs_fstype type;
} simplefs_backend_t;

static simplefs_store_t g_stores[SIMPLEFS_MAX_STORES];
static simplefs_mount_t g_mounts[SIMPLEFS_MAX_MOUNTS];
static simplefs_backend_t g_backends[4];

static int simplefs_name_valid_83(const char *name)
{
    int seen_dot = 0;
    int base_len = 0;
    int ext_len = 0;

    if (!name || !name[0])
        return 0;

    for (const char *p = name; *p; p++)
    {
        char c = *p;
        if (c == '/')
            return 0;
        if (c == '.')
        {
            if (seen_dot)
                return 0;
            seen_dot = 1;
            continue;
        }
        if (c == ' ' || c < 0x20 || c == 0x7F)
            return 0;
        if (!seen_dot)
        {
            base_len++;
            if (base_len > 8)
                return 0;
        }
        else
        {
            ext_len++;
            if (ext_len > 3)
                return 0;
        }
    }

    if (base_len == 0)
        return 0;
    return 1;
}

static int simplefs_validate_entry_name(simplefs_store_t *store, const char *name)
{
    size_t len = strlen(name);

    if (len == 0 || len > SIMPLEFS_MAX_NAME_LEN)
        return -EINVAL;
    if (store->strict_83_names)
    {
        if (!simplefs_name_valid_83(name))
            return -EINVAL;
    }
    return 0;
}

static simplefs_store_t *simplefs_find_store(const char *fs_name, const char *dev)
{
    for (int i = 0; i < SIMPLEFS_MAX_STORES; i++)
    {
        if (!g_stores[i].in_use)
            continue;
        if (strcmp(g_stores[i].fs_name, fs_name) != 0)
            continue;
        if (strcmp(g_stores[i].dev, dev) == 0)
            return &g_stores[i];
    }
    return NULL;
}

static void simplefs_store_release(simplefs_store_t *store)
{
    int j;

    if (!store || !store->in_use)
        return;

    for (j = 0; j < SIMPLEFS_MAX_ENTRIES; j++)
    {
        if (!store->entries[j].in_use)
            continue;
        if (store->entries[j].data)
        {
            kfree(store->entries[j].data);
            store->entries[j].data = NULL;
        }
    }

    memset(store, 0, sizeof(*store));
}

static simplefs_store_t *simplefs_get_or_create_store(const char *fs_name, const char *dev, int strict_83_names)
{
    simplefs_store_t *store = simplefs_find_store(fs_name, dev);
    if (store)
        return store;

    for (int i = 0; i < SIMPLEFS_MAX_STORES; i++)
    {
        if (g_stores[i].in_use)
            continue;
        memset(&g_stores[i], 0, sizeof(g_stores[i]));
        g_stores[i].in_use = 1;
        g_stores[i].strict_83_names = strict_83_names;
        g_stores[i].next_ino = 1;
        strncpy(g_stores[i].fs_name, fs_name, sizeof(g_stores[i].fs_name) - 1);
        g_stores[i].fs_name[sizeof(g_stores[i].fs_name) - 1] = '\0';
        strncpy(g_stores[i].dev, dev, sizeof(g_stores[i].dev) - 1);
        g_stores[i].dev[sizeof(g_stores[i].dev) - 1] = '\0';
        g_stores[i].mount_refcount = 0;
        return &g_stores[i];
    }
    return NULL;
}

static void simplefs_normalize_mount(const char *src, char *dst, size_t dst_sz)
{
    size_t len;

    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
    len = strlen(dst);
    while (len > 1 && dst[len - 1] == '/')
    {
        dst[len - 1] = '\0';
        len--;
    }
}

static simplefs_mount_t *simplefs_find_mount_for_path(const char *fs_name, const char *path)
{
    simplefs_mount_t *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < SIMPLEFS_MAX_MOUNTS; i++)
    {
        size_t mlen;

        if (!g_mounts[i].in_use)
            continue;
        if (strcmp(g_mounts[i].fs_name, fs_name) != 0)
            continue;
        mlen = strlen(g_mounts[i].mount_path);
        if (strncmp(path, g_mounts[i].mount_path, mlen) != 0)
            continue;
        if (path[mlen] != '\0' && path[mlen] != '/')
            continue;
        if (mlen > best_len)
        {
            best_len = mlen;
            best = &g_mounts[i];
        }
    }
    return best;
}

static int simplefs_relname(const char *mount_path, const char *full_path, char *name, size_t name_sz, int *is_root)
{
    const char *rel;
    const char *slash;

    *is_root = 0;
    if (strcmp(full_path, mount_path) == 0)
    {
        *is_root = 1;
        name[0] = '\0';
        return 0;
    }

    rel = full_path + strlen(mount_path);
    if (rel[0] == '/')
        rel++;
    if (rel[0] == '\0')
    {
        *is_root = 1;
        name[0] = '\0';
        return 0;
    }

    slash = strchr(rel, '/');
    if (slash)
        return -ENOTSUPP;
    if (strlen(rel) >= name_sz)
        return -ENAMETOOLONG;
    strncpy(name, rel, name_sz - 1);
    name[name_sz - 1] = '\0';
    return 0;
}

static simplefs_entry_t *simplefs_find_entry(simplefs_store_t *store, const char *name)
{
    for (int i = 0; i < SIMPLEFS_MAX_ENTRIES; i++)
    {
        if (!store->entries[i].in_use)
            continue;
        if (strcmp(store->entries[i].name, name) == 0)
            return &store->entries[i];
    }
    return NULL;
}

static simplefs_entry_t *simplefs_alloc_entry(simplefs_store_t *store)
{
    for (int i = 0; i < SIMPLEFS_MAX_ENTRIES; i++)
    {
        if (store->entries[i].in_use)
            continue;
        memset(&store->entries[i], 0, sizeof(store->entries[i]));
        store->entries[i].in_use = 1;
        store->entries[i].ino = ++store->next_ino;
        store->entries[i].mtime = (time_t)(clock_get_uptime_milliseconds() / 1000);
        return &store->entries[i];
    }
    return NULL;
}

/*
 * simplefs_check_block_device - Require ATA block names to be registered.
 * Virtual backing devices (simple0, fat0, ...) skip this check.
 */
static int simplefs_check_block_device(const char *dev)
{
    const char *name;

    if (!dev || strcmp(dev, "none") == 0)
        return 0;

    name = dev;
    if (strncmp(dev, "/dev/", 5) == 0)
        name = dev + 5;

    if (strcmp(name, "hda") != 0 && strcmp(name, "hdb") != 0 &&
        strcmp(name, "hdc") != 0 && strcmp(name, "hdd") != 0)
        return 0;

    return ir0_block_name_is_present(name) ? 0 : -ENXIO;
}

#if CONFIG_ENABLE_FS_FAT16
static int simplefs_fat16_dev_is_block(const char *dev)
{
    const char *name;

    if (!dev || strcmp(dev, "none") == 0)
        return 0;

    name = dev;
    if (strncmp(dev, "/dev/", 5) == 0)
        name = dev + 5;

    if (name[0] != 'h' || name[1] != 'd')
        return 0;
    if (name[2] < 'a' || name[2] > 'd')
        return 0;
    if (name[3] == '\0')
        return 1;
    if (name[3] >= '1' && name[3] <= '9' && name[4] == '\0')
        return 1;
    return 0;
}

static int simplefs_fat16_on_disk(const char *fs_name, const char *path)
{
    return strcmp(fs_name, "fat16") == 0 && fat16_disk_path_is_mounted(path);
}
#endif

static int simplefs_umount_common(const char *fs_name, const char *dir)
{
    char mount_norm[256];

    if (!dir || !fs_name)
        return -EINVAL;

    simplefs_normalize_mount(dir, mount_norm, sizeof(mount_norm));

    for (int i = 0; i < SIMPLEFS_MAX_MOUNTS; i++)
    {
        if (!g_mounts[i].in_use)
            continue;
        if (strcmp(g_mounts[i].fs_name, fs_name) != 0)
            continue;
        if (strcmp(g_mounts[i].mount_path, mount_norm) != 0)
            continue;

        {
            simplefs_store_t *stor = g_mounts[i].store;

            g_mounts[i].in_use = 0;
            g_mounts[i].store = NULL;
            g_mounts[i].mount_path[0] = '\0';

            if (stor)
            {
                if (stor->mount_refcount > 0)
                    stor->mount_refcount--;
                if (stor->mount_refcount == 0)
                    simplefs_store_release(stor);
            }
        }
        return 0;
    }

    return -EINVAL;
}

static int simplefs_mount_common(const char *fs_name, const char *dev, const char *dir, int strict_83_names)
{
    char mount_norm[256];
    simplefs_store_t *store;
    int blk_rc;

    if (!dir || !fs_name)
        return -EINVAL;

    blk_rc = simplefs_check_block_device(dev);
    if (blk_rc != 0)
        return blk_rc;

    simplefs_normalize_mount(dir, mount_norm, sizeof(mount_norm));
    store = simplefs_get_or_create_store(fs_name, dev ? dev : "none", strict_83_names);
    if (!store)
        return -ENOMEM;

    for (int i = 0; i < SIMPLEFS_MAX_MOUNTS; i++)
    {
        if (g_mounts[i].in_use && strcmp(g_mounts[i].mount_path, mount_norm) == 0)
            return -EBUSY;
    }

    for (int i = 0; i < SIMPLEFS_MAX_MOUNTS; i++)
    {
        if (g_mounts[i].in_use)
            continue;
        memset(&g_mounts[i], 0, sizeof(g_mounts[i]));
        g_mounts[i].in_use = 1;
        g_mounts[i].store = store;
        strncpy(g_mounts[i].fs_name, fs_name, sizeof(g_mounts[i].fs_name) - 1);
        g_mounts[i].fs_name[sizeof(g_mounts[i].fs_name) - 1] = '\0';
        strncpy(g_mounts[i].mount_path, mount_norm, sizeof(g_mounts[i].mount_path) - 1);
        g_mounts[i].mount_path[sizeof(g_mounts[i].mount_path) - 1] = '\0';
        if (store)
            store->mount_refcount++;
        return 0;
    }

    return -ENOSPC;
}

static simplefs_mount_t *simplefs_context_mount(const char *fs_name, const char *path, char *name, int *is_root, int *rc)
{
    simplefs_mount_t *mnt = simplefs_find_mount_for_path(fs_name, path);
    if (!mnt)
    {
        *rc = -ENODEV;
        return NULL;
    }
    *rc = simplefs_relname(mnt->mount_path, path, name, SIMPLEFS_MAX_NAME_LEN + 1, is_root);
    if (*rc != 0)
        return NULL;
    return mnt;
}

static int simplefs_stat_common(const char *fs_name, const char *path, stat_t *buf)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

    if (!buf)
        return -EFAULT;
#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_stat(path, buf);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;

    memset(buf, 0, sizeof(*buf));
    if (is_root)
    {
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 1;
        return 0;
    }

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;

    buf->st_mode = e->mode;
    buf->st_uid = e->uid;
    buf->st_gid = e->gid;
    buf->st_size = (off_t)e->size;
    buf->st_ino = e->ino;
    buf->st_nlink = 1;
    buf->st_mtime = e->mtime;
    return 0;
}

static int simplefs_mkdir_common(const char *fs_name, const char *path, mode_t mode)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;
    const struct ir0_task_cred *cr = ir0_current_cred();
    mode_t umask_value = (mode_t)(cr ? cr->umask : DEFAULT_UMASK);

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_mkdir(path, mode);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EEXIST;
    if (simplefs_validate_entry_name(mnt->store, name) != 0)
        return -EINVAL;
    if (simplefs_find_entry(mnt->store, name))
        return -EEXIST;

    e = simplefs_alloc_entry(mnt->store);
    if (!e)
        return -ENOSPC;
    e->is_dir = 1;
    e->mode = S_IFDIR | ((mode & 0777) & ~umask_value);
    e->uid = cr ? cr->euid : ROOT_UID;
    e->gid = cr ? cr->egid : ROOT_GID;
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    return 0;
}

static int simplefs_create_common(const char *fs_name, const char *path, mode_t mode)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;
    const struct ir0_task_cred *cr = ir0_current_cred();
    mode_t umask_value = (mode_t)(cr ? cr->umask : DEFAULT_UMASK);

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_create(path, mode);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EEXIST;
    if (simplefs_validate_entry_name(mnt->store, name) != 0)
        return -EINVAL;

    e = simplefs_find_entry(mnt->store, name);
    if (e)
    {
        if (e->is_dir)
            return -EISDIR;
        return 0;
    }

    e = simplefs_alloc_entry(mnt->store);
    if (!e)
        return -ENOSPC;
    e->is_dir = 0;
    e->mode = S_IFREG | ((mode & 0777) & ~umask_value);
    e->uid = cr ? cr->euid : ROOT_UID;
    e->gid = cr ? cr->egid : ROOT_GID;
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    return 0;
}

static int simplefs_unlink_common(const char *fs_name, const char *path)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_unlink(path);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EBUSY;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    if (e->is_dir)
        return -EISDIR;
    if (e->data)
        kfree(e->data);
    memset(e, 0, sizeof(*e));
    return 0;
}

static int simplefs_rmdir_common(const char *fs_name, const char *path)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_rmdir(path);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EBUSY;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    if (!e->is_dir)
        return -ENOTDIR;
    memset(e, 0, sizeof(*e));
    return 0;
}

static int simplefs_read_common(const char *fs_name, const char *path, void *buf, size_t count, size_t *bytes_read, off_t offset)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    size_t avail;
    size_t to_read;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

    if (!buf)
        return -EFAULT;
    if (bytes_read)
        *bytes_read = 0;
#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_read(path, buf, count, bytes_read, offset);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EISDIR;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    if (e->is_dir)
        return -EISDIR;
    if (offset < 0 || (size_t)offset >= e->size)
        return 0;

    avail = e->size - (size_t)offset;
    to_read = (count < avail) ? count : avail;
    if (to_read > 0 && e->data)
        memcpy(buf, e->data + offset, to_read);
    if (bytes_read)
        *bytes_read = to_read;
    return 0;
}

static int simplefs_write_common(const char *fs_name, const char *path, const void *buf, size_t count, size_t *bytes_written, off_t offset)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    size_t new_size;
    uint8_t *new_data;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

    if (!buf)
        return -EFAULT;
    if (bytes_written)
        *bytes_written = 0;
#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_write(path, buf, count, bytes_written, offset);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EISDIR;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    if (e->is_dir)
        return -EISDIR;
    if (offset < 0)
        return -EINVAL;

    if ((size_t)offset > SIMPLEFS_MAX_FILE_SIZE ||
        count > SIMPLEFS_MAX_FILE_SIZE - (size_t)offset)
        return -EFBIG;
    new_size = (size_t)offset + count;

    if (new_size > e->size)
    {
        new_data = (uint8_t *)kmalloc_try(new_size);
        if (!new_data)
            return -ENOMEM;
        if (e->data)
        {
            memcpy(new_data, e->data, e->size);
            kfree(e->data);
        }
        if (new_size > e->size)
            memset(new_data + e->size, 0, new_size - e->size);
        e->data = new_data;
        e->size = new_size;
    }

    memcpy(e->data + offset, buf, count);
    e->mtime = (time_t)(clock_get_uptime_milliseconds() / 1000);
    if (bytes_written)
        *bytes_written = count;
    return 0;
}

static int simplefs_chown_common(const char *fs_name, const char *path, uid_t owner, gid_t group)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_chown(path, owner, group);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return 0;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    e->uid = owner;
    e->gid = group;
    return 0;
}

static int simplefs_chmod_common(const char *fs_name, const char *path, mode_t mode)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_chmod(path, mode);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return 0;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    e->mode = (e->mode & ~0777) | (mode & 0777);
    return 0;
}

static int simplefs_truncate_common(const char *fs_name, const char *path, size_t length)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    uint8_t *new_data = NULL;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_truncate(path, length);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    if (is_root)
        return -EISDIR;

    e = simplefs_find_entry(mnt->store, name);
    if (!e)
        return -ENOENT;
    if (e->is_dir)
        return -EISDIR;
    if (length > SIMPLEFS_MAX_FILE_SIZE)
        return -EFBIG;
    if (length == 0)
    {
        if (e->data)
            kfree(e->data);
        e->data = NULL;
        e->size = 0;
        return 0;
    }

    new_data = (uint8_t *)kmalloc_try(length);
    if (!new_data)
        return -ENOMEM;
    if (e->data)
    {
        size_t copy = (e->size < length) ? e->size : length;
        memcpy(new_data, e->data, copy);
        if (copy < length)
            memset(new_data + copy, 0, length - copy);
        kfree(e->data);
    }
    else
    {
        memset(new_data, 0, length);
    }

    e->data = new_data;
    e->size = length;
    return 0;
}

static int simplefs_readdir_common(const char *fs_name, const char *path, struct vfs_dirent *entries, int max_entries)
{
    char name[SIMPLEFS_MAX_NAME_LEN + 1];
    int is_root = 0;
    int rc = 0;
    int out = 0;
    simplefs_mount_t *mnt;
    simplefs_entry_t *e;

    if (!entries || max_entries <= 0)
        return -EINVAL;
#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_on_disk(fs_name, path))
        return fat16_disk_readdir(path, entries, max_entries);
#endif
    mnt = simplefs_context_mount(fs_name, path, name, &is_root, &rc);
    if (!mnt)
        return rc;
    /*
     * The store is flat: an entry's name is its whole path relative to the
     * mount point, so nesting is expressed by '/' inside that name. A
     * directory listing is therefore the set of entries under "<dir>/" whose
     * remainder holds no further separator.
     *
     * Subdirectories used to return 0 here, reporting every one of them as
     * empty: ls showed nothing below the mount root and a recursive rmdir
     * saw no children, so it failed with ENOTEMPTY on a tree it had just
     * been asked to delete. The mount root had the mirror problem, listing
     * nested entries under their full relative name.
     */
    size_t plen = 0;
    char prefix[SIMPLEFS_MAX_NAME_LEN + 2];

    if (!is_root)
    {
        e = simplefs_find_entry(mnt->store, name);
        if (!e)
            return -ENOENT;
        if (!e->is_dir)
            return -ENOTDIR;

        plen = strlen(name);
        if (plen + 2 > sizeof(prefix))
            return -ENAMETOOLONG;
        memcpy(prefix, name, plen);
        prefix[plen++] = '/';
        prefix[plen] = '\0';
    }

    for (int i = 0; i < SIMPLEFS_MAX_ENTRIES && out < max_entries; i++)
    {
        const char *ename;
        const char *child;

        if (!mnt->store->entries[i].in_use)
            continue;

        ename = mnt->store->entries[i].name;
        if (plen != 0)
        {
            if (strncmp(ename, prefix, plen) != 0)
                continue;
            child = ename + plen;
        }
        else
        {
            child = ename;
        }

        if (child[0] == '\0')
            continue;
        /* Deeper descendant, not an immediate child. */
        if (strchr(child, '/') != NULL)
            continue;

        strncpy(entries[out].name, child, sizeof(entries[out].name) - 1);
        entries[out].name[sizeof(entries[out].name) - 1] = '\0';
        entries[out].type = mnt->store->entries[i].is_dir ? DT_DIR : DT_REG;
        out++;
    }
    return out;
}

static int simplefs_mount_simple(const char *dev, const char *dir)
{
    return simplefs_mount_common("simplefs", dev, dir, 0);
}

static int simplefs_mount_fat16(const char *dev, const char *dir)
{
#if CONFIG_ENABLE_FS_FAT16
    if (simplefs_fat16_dev_is_block(dev))
    {
        int blk_rc = simplefs_check_block_device(dev);

        if (blk_rc != 0)
            return blk_rc;
        return fat16_disk_mount(dev, dir);
    }
#endif
    return simplefs_mount_common("fat16", dev, dir, 1);
}

static int simplefs_umount_simple(const char *dir)
{
    return simplefs_umount_common("simplefs", dir);
}

static int simplefs_umount_fat16(const char *dir)
{
#if CONFIG_ENABLE_FS_FAT16
    if (fat16_disk_umount(dir) == 0)
        return 0;
#endif
    return simplefs_umount_common("fat16", dir);
}

#define SIMPLEFS_DEFINE_OPS(prefix, fsName) \
static int prefix##_stat(const char *path, stat_t *buf) { return simplefs_stat_common(fsName, path, buf); } \
static int prefix##_mkdir(const char *path, mode_t mode) { return simplefs_mkdir_common(fsName, path, mode); } \
static int prefix##_create(const char *path, mode_t mode) { return simplefs_create_common(fsName, path, mode); } \
static int prefix##_unlink(const char *path) { return simplefs_unlink_common(fsName, path); } \
static int prefix##_rmdir(const char *path) { return simplefs_rmdir_common(fsName, path); } \
static int __attribute__((unused)) prefix##_link(const char *oldpath __attribute__((unused)), const char *newpath __attribute__((unused))) { return -ENOSYS; } \
static int prefix##_chown(const char *path, uid_t owner, gid_t group) { return simplefs_chown_common(fsName, path, owner, group); } \
static int prefix##_chmod(const char *path, mode_t mode) { return simplefs_chmod_common(fsName, path, mode); } \
static int prefix##_readdir(const char *path, struct vfs_dirent *entries, int max) { return simplefs_readdir_common(fsName, path, entries, max); } \
static int prefix##_read(const char *path, void *buf, size_t count, size_t *bytes_read, off_t offset) { return simplefs_read_common(fsName, path, buf, count, bytes_read, offset); } \
static int prefix##_write(const char *path, const void *buf, size_t count, size_t *bytes_written, off_t offset) { return simplefs_write_common(fsName, path, buf, count, bytes_written, offset); } \
static int prefix##_truncate(const char *path, size_t length) { return simplefs_truncate_common(fsName, path, length); }

SIMPLEFS_DEFINE_OPS(simplefs, "simplefs")
SIMPLEFS_DEFINE_OPS(fat16mini, "fat16")

int simplefs_engine_register(const char *fs_name, int strict_83_names)
{
    simplefs_backend_t *be = NULL;
    int (*mount_fn)(const char *, const char *) = NULL;

    if (!fs_name)
        return -EINVAL;
    for (int i = 0; i < 4; i++)
    {
        if (g_backends[i].in_use && strcmp(g_backends[i].fs_name, fs_name) == 0)
            return 0;
    }
    for (int i = 0; i < 4; i++)
    {
        if (!g_backends[i].in_use)
        {
            be = &g_backends[i];
            break;
        }
    }
    if (!be)
        return -ENOSPC;

    memset(be, 0, sizeof(*be));
    be->in_use = 1;
    be->strict_83_names = strict_83_names;
    strncpy(be->fs_name, fs_name, sizeof(be->fs_name) - 1);
    be->fs_name[sizeof(be->fs_name) - 1] = '\0';

    if (strcmp(fs_name, "fat16") == 0)
    {
        be->ops.stat = fat16mini_stat;
        be->ops.mkdir = fat16mini_mkdir;
        be->ops.create = fat16mini_create;
        be->ops.unlink = fat16mini_unlink;
        be->ops.rmdir = fat16mini_rmdir;
#if CONFIG_ENABLE_FS_FAT16
        be->ops.link = fat16_disk_link;
#else
        be->ops.link = fat16mini_link;
#endif
        be->ops.chown = fat16mini_chown;
        be->ops.chmod = fat16mini_chmod;
        be->ops.readdir = fat16mini_readdir;
        be->ops.read = fat16mini_read;
        be->ops.write = fat16mini_write;
        be->ops.truncate = fat16mini_truncate;
        mount_fn = simplefs_mount_fat16;
    }
    else
    {
        be->ops.stat = simplefs_stat;
        be->ops.mkdir = simplefs_mkdir;
        be->ops.create = simplefs_create;
        be->ops.unlink = simplefs_unlink;
        be->ops.rmdir = simplefs_rmdir;
        be->ops.link = simplefs_link;
        be->ops.chown = simplefs_chown;
        be->ops.chmod = simplefs_chmod;
        be->ops.readdir = simplefs_readdir;
        be->ops.read = simplefs_read;
        be->ops.write = simplefs_write;
        be->ops.truncate = simplefs_truncate;
        mount_fn = simplefs_mount_simple;
    }

    be->type.name = be->fs_name;
    be->type.ops = &be->ops;
    be->type.mount = mount_fn;
    be->type.umount = (strcmp(fs_name, "fat16") == 0) ?
                      simplefs_umount_fat16 : simplefs_umount_simple;
    be->type.next = NULL;
    return vfs_register_fs(&be->type);
}

int simplefs_register(void)
{
    return simplefs_engine_register("simplefs", 0);
}
