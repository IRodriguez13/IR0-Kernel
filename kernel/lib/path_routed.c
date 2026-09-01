/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — routed pseudo-fs/VFS path helpers for syscall layer.
 */

#include <ir0/path_routed.h>
#include <string.h>
#include <ir0/devfs.h>
#include <ir0/errno.h>
#include <ir0/named_fifo.h>
#include <ir0/named_devnode.h>
#include <ir0/named_symlink.h>
#include <ir0/permissions.h>
#include <ir0/procfs.h>
#include <ir0/sysfs.h>
#include <ir0/heartfs.h>
#include <ir0/pseudo_fs.h>
#include <fs/vfs.h>

extern void ensure_devfs_init(void);

static int ir0_access_mode_from_linux(int mode)
{
    int access_mode;

    access_mode = 0;
    if (mode & 4)
        access_mode |= ACCESS_READ;
    if (mode & 2)
        access_mode |= ACCESS_WRITE;
    if (mode & 1)
        access_mode |= ACCESS_EXEC;

    return access_mode;
}

int ir0_is_dev_path(const char *path)
{
    if (!path)
        return 0;

    return strcmp(path, "/dev") == 0 ||
           strcmp(path, "/dev/") == 0 ||
           strncmp(path, "/dev/", 5) == 0;
}

int ir0_stat_path_routed(const char *path, stat_t *st)
{
    int rc;

    if (!path || !st)
        return -EINVAL;

    if (is_proc_path(path))
        return proc_stat(path, st);
    if (is_sys_path(path))
        return sysfs_stat(path, st);
    if (is_heart_path(path))
    {
        if (heart_is_dennis_vfs_path(path))
            return vfs_stat(path, st);
        return heart_stat(path, st);
    }
    if (ir0_is_dev_path(path))
    {
        ensure_devfs_init();
        return devfs_stat_path(path, st);
    }

    /* In-memory runsv FIFOs (mknod S_IFIFO) are not VFS nodes. */
    rc = named_fifo_stat(path, st);
    if (rc == 0)
        return 0;
    if (rc != -ENOENT)
        return rc;

    /* Likewise for mknod device nodes created outside /dev. */
    rc = named_devnode_stat(path, st);
    if (rc == 0)
        return 0;
    if (rc != -ENOENT)
        return rc;

    return vfs_stat(path, st);
}

int ir0_stat_path_routed_follow(const char *path, stat_t *st)
{
    char resolved[256];
    int rc;

    if (!path || !st)
        return -EINVAL;

    strncpy(resolved, path, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';
    rc = ir0_follow_named_symlinks(resolved, sizeof(resolved),
                                   IR0_SYMLINK_FOLLOW_MAX);
    if (rc < 0)
        return rc;
    return ir0_stat_path_routed(resolved, st);
}

int64_t ir0_access_path_routed(const char *resolved_path, int mode,
                               uid_t euid, gid_t egid)
{
    stat_t st;
    int rc;
    int access_mode;

    if (!resolved_path)
        return -EFAULT;
    if (mode & ~7)
        return -EINVAL;

    rc = ir0_stat_path_routed(resolved_path, &st);
    if (rc != 0)
        return rc;

    if (mode == 0)
        return 0;

    access_mode = ir0_access_mode_from_linux(mode);
    if (access_mode == 0)
        return 0;

    if (!ir0_access_from_stat(&st, access_mode, euid, egid))
        return -EACCES;

    return 0;
}

int ir0_getdents_path_routed(const char *path, struct vfs_dirent *entries,
                             int max_entries)
{
    if (!path || !entries || max_entries <= 0)
        return -EINVAL;

    if (strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0)
    {
        ensure_devfs_init();
        return devfs_readdir_root(entries, max_entries);
    }

    if (ir0_is_dev_path(path) && devfs_is_virtual_subdir(path))
    {
        ensure_devfs_init();
        return devfs_readdir_subdir(path, entries, max_entries);
    }

    if (is_heart_path(path))
    {
        if (heart_is_dennis_vfs_path(path))
            return vfs_readdir(path, entries, max_entries);
        return heart_getdents(path, entries, max_entries);
    }

    if (is_proc_path(path))
        return proc_readdir(path, entries, max_entries);

    if (strcmp(path, "/sys") == 0 || strcmp(path, "/sys/") == 0 ||
        (is_sys_path(path) && sysfs_is_virtual_subdir(path)))
    {
        pseudo_fs_nodes_register_all();
        return pseudo_fs_collect_registry_children(path, entries, max_entries, 0);
    }

    return vfs_readdir(path, entries, max_entries);
}
