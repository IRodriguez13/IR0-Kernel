/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.c
 * Description: Minimal path-based VFS — mount table + ops dispatch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "vfs.h"
#if CONFIG_ENABLE_FS_MINIX
#include "minix_fs.h"
#endif
#if CONFIG_ENABLE_FS_TMPFS
#include "tmpfs.h"
#include "hostshare_9p.h"
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
#include "simplefs.h"
#endif
#if CONFIG_ENABLE_FS_FAT16
#include "fat16_fs.h"
#endif
#if CONFIG_ENABLE_FS_EXT2
#include "ext2_disk.h"
#endif
#include <ir0/path.h>
#include <ir0/logging.h>
#include <ir0/ktm/klog.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/flock.h>
#include <ir0/open_flags.h>
#include <ir0/named_fifo.h>
#include <ir0/exec_read_trace.h>
#include <ir0/permissions.h>
#include <ir0/credentials.h>
#include <ir0/blockdev.h>
#include <ir0/klog.h>
#include <ir0/console_backend.h>
#include <ir0/statfs.h>
#include <ir0/arch_cpu.h>
#include <ir0/cpu.h>
#include <ir0/sched.h>
#include <ir0/process.h>
#include <config.h>
#include <string.h>

#define MAX_PATH 256
#define VFS_READ_FILE_MAX_BYTES (64U * 1024U * 1024U)

/* ------------------------------------------------------------------ */
/*  Global state                                                       */
/* ------------------------------------------------------------------ */
static struct vfs_fstype *fs_types  = NULL;
static struct vfs_mount  *mounts   = NULL;

/*
 * MINIX find_inode() returns a pointer to a static inode. Concurrent
 * vfs_stat / vfs_read / vfs_read_file (ls+stat vs execve of BusyBox)
 * filled struct stat from the wrong inode and tore ELF images.
 *
 * One fs_ops critical section at a time. Waiters schedule; IRQs stay
 * enabled so ATA PIO still uses its own irq_save per command.
 *
 * Recursive: vfs_open -> vfs_truncate, vfs_utimens -> vfs_stat,
 * vfs_read_file -> ops->stat/read, same task.
 */
static int vfs_fs_op_busy;
static int vfs_fs_op_depth;
static const void *vfs_fs_op_owner;

static const void *vfs_fs_op_self(void)
{
	if (current_process)
		return current_process;
	return &vfs_fs_op_busy;
}

static void vfs_fs_op_acquire(void)
{
	const void *me = vfs_fs_op_self();

	for (;;)
	{
		unsigned long flags = irq_save();

		if (vfs_fs_op_busy && vfs_fs_op_owner == me)
		{
			vfs_fs_op_depth++;
			irq_restore(flags);
			return;
		}
		if (!vfs_fs_op_busy)
		{
			vfs_fs_op_busy = 1;
			vfs_fs_op_owner = me;
			vfs_fs_op_depth = 1;
			irq_restore(flags);
			return;
		}
		irq_restore(flags);
		if (!current_process)
		{
			cpu_relax();
			continue;
		}
		sched_schedule_next();
	}
}

static void vfs_fs_op_release(void)
{
	unsigned long flags = irq_save();

	if (vfs_fs_op_depth > 1)
	{
		vfs_fs_op_depth--;
		irq_restore(flags);
		return;
	}
	vfs_fs_op_busy = 0;
	vfs_fs_op_owner = NULL;
	vfs_fs_op_depth = 0;
	irq_restore(flags);
}

static int vfs_ops_stat(struct vfs_ops *ops, const char *path, stat_t *st)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->stat(path, st);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_read(struct vfs_ops *ops, const char *path, void *buf,
			size_t count, size_t *done, off_t offset)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->read(path, buf, count, done, offset);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_write(struct vfs_ops *ops, const char *path, const void *buf,
			 size_t count, size_t *done, off_t offset)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->write(path, buf, count, done, offset);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_create(struct vfs_ops *ops, const char *path, mode_t mode)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->create(path, mode);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_mkdir(struct vfs_ops *ops, const char *path, mode_t mode)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->mkdir(path, mode);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_unlink(struct vfs_ops *ops, const char *path)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->unlink(path);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_rmdir(struct vfs_ops *ops, const char *path)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->rmdir(path);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_readdir(struct vfs_ops *ops, const char *path,
			   struct vfs_dirent *entries, int max)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->readdir(path, entries, max);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_link(struct vfs_ops *ops, const char *oldpath,
			const char *newpath)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->link(oldpath, newpath);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_symlink(struct vfs_ops *ops, const char *target,
			   const char *linkpath)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->symlink(target, linkpath);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_readlink(struct vfs_ops *ops, const char *path, char *buf,
			    size_t buflen)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->readlink(path, buf, buflen);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_rename(struct vfs_ops *ops, const char *oldpath,
			  const char *newpath)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->rename(oldpath, newpath);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_chown(struct vfs_ops *ops, const char *path, uid_t owner,
			 gid_t group)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->chown(path, owner, group);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_chmod(struct vfs_ops *ops, const char *path, mode_t mode)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->chmod(path, mode);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_truncate(struct vfs_ops *ops, const char *path, size_t length)
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->truncate(path, length);
	vfs_fs_op_release();
	return ret;
}

static int vfs_ops_utimens(struct vfs_ops *ops, const char *path,
			   const struct timespec times[2])
{
	int ret;

	vfs_fs_op_acquire();
	ret = ops->utimens(path, times);
	vfs_fs_op_release();
	return ret;
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static int validate_path(const char *path)
{
    if (!path)
        return -EINVAL;
    size_t len = strlen(path);
    if (len == 0)
        return -EINVAL;
    if (len >= MAX_PATH)
        return -ENAMETOOLONG;

    int comp = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            comp = 0;
        else if (++comp > 255)
            return -ENAMETOOLONG;

        if (*p < 0x20 || *p == 0x7F ||
            *p == '\\' || *p == '|' || *p == '<' || *p == '>')
            return -EINVAL;
    }
    return 0;
}

/**
 * Find the mount whose path is the longest prefix of @path.
 */
static struct vfs_mount *find_mount(const char *path)
{
    struct vfs_mount *best = NULL;
    size_t best_len = 0;
    size_t plen = strlen(path);

    for (struct vfs_mount *m = mounts; m; m = m->next) {
        size_t mlen = strlen(m->path);
        if (mlen > plen)
            continue;
        if (strncmp(path, m->path, mlen) != 0)
            continue;
        if (mlen < plen && m->path[mlen - 1] != '/' && path[mlen] != '/')
            continue;
        if (mlen > best_len) {
            best_len = mlen;
            best = m;
        }
    }
    return best;
}

static int mount_is_readonly(const char *path)
{
    struct vfs_mount *m = find_mount(path);

    return m && (m->flags & IR0_MS_RDONLY);
}

static int vfs_exec_audit_active;
static const char *vfs_exec_audit_path;

void vfs_exec_audit_begin(const char *path)
{
    /* Legacy FASE50 exec ATA dump retired; KTM covers validation. */
    (void)path;
    vfs_exec_audit_active = 0;
    vfs_exec_audit_path = NULL;
}

void vfs_exec_audit_end(void)
{
    vfs_exec_audit_active = 0;
    vfs_exec_audit_path = NULL;
}

int vfs_exec_audit_is_active(void)
{
    return vfs_exec_audit_active;
}

static void vfs_exec_audit_log(const char *stage, const char *path, int ret,
                               off_t offset, size_t req, size_t got,
                               uint64_t st_ino, int64_t st_size)
{
    if (!vfs_exec_audit_active)
        return;

    klog_print("[EXEC_AUDIT][VFS] stage=");
    klog_print(stage ? stage : "?");
    klog_print(" path=");
    klog_print(path ? path : "(null)");
    klog_print(" ret=");
    klog_hex64((uint64_t)(int64_t)ret);
    klog_print(" offset=");
    klog_hex64((uint64_t)offset);
    klog_print(" req=");
    klog_hex64((uint64_t)req);
    klog_print(" got=");
    klog_hex64((uint64_t)got);
    klog_print(" st_ino=");
    klog_hex64(st_ino);
    klog_print(" st_size=");
    klog_hex64((uint64_t)st_size);
    klog_print("\n");
}

/*
 * Set-user-ID / set-group-ID on exec is honoured only on the trusted on-disk
 * root filesystem. IR0 has no per-mount MS_NOSUID yet (sys_mount takes no
 * flags), so every other backend — pseudo-filesystems, devfs, host 9p shares —
 * is treated as nosuid instead of pretending the flag exists.
 */
int vfs_path_allows_setid(const char *path)
{
    struct vfs_mount *m;

    if (!path)
        return 0;

    m = find_mount(path);
    if (!m || !m->fs || !m->fs->name)
        return 0;

    return strcmp(m->fs->name, "minix") == 0;
}

/**
 * Return the vfs_ops for the filesystem that owns @path, or NULL.
 */
static struct vfs_ops *ops_for_path(const char *path)
{
    struct vfs_mount *m = find_mount(path);
    if (m && m->fs && m->fs->ops)
        return m->fs->ops;
    return NULL;
}

/**
 * Check that the current process has execute on every directory component.
 */
static int check_dir_traverse(const char *path)
{
    if (!ir0_current_cred())
        return -EINVAL;
    if (ir0_cred_is_root())
        return 0;
    if (strcmp(path, "/") == 0)
        return 0;

    char dir[MAX_PATH];
    strncpy(dir, "/", sizeof(dir));
    const char *p = path + 1;

    while (*p) {
        const char *slash = strchr(p, '/');
        if (!slash)
            break;
        size_t clen = (size_t)(slash - p);
        size_t dlen = strlen(dir);
        if (dlen + 1 + clen >= sizeof(dir))
            return -ENAMETOOLONG;
        if (dlen > 1)
            strncat(dir, "/", sizeof(dir) - strlen(dir) - 1);
        strncat(dir, p, clen);
        dir[sizeof(dir) - 1] = '\0';

        if (!ir0_check_file_access(dir, ACCESS_EXEC))
            return -EACCES;
        p = slash + 1;
    }
    return 0;
}

/**
 * Convenience: extract parent directory of @path into @out.
 * Returns 0 on success.
 */
static int parent_dir(const char *path, char *out, size_t out_sz)
{
    const char *last = strrchr(path, '/');
    if (!last || last == path) {
        strncpy(out, "/", out_sz);
        out[out_sz - 1] = '\0';
        return 0;
    }
    size_t len = (size_t)(last - path);
    if (len >= out_sz)
        return -ENAMETOOLONG;
    strncpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int build_path(char *dest, size_t sz, const char *dir, const char *name)
{
    if (!dest || !dir || !name || sz == 0)
        return -EINVAL;
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    if (dlen + nlen + 2 > sz)
        return -ENAMETOOLONG;
    size_t i = 0;
    for (size_t j = 0; j < dlen && i < sz - 1; j++)
        dest[i++] = dir[j];
    if (dlen > 0 && dir[dlen - 1] != '/' && i < sz - 1)
        dest[i++] = '/';
    for (size_t j = 0; j < nlen && i < sz - 1; j++)
        dest[i++] = name[j];
    dest[i] = '\0';
    return 0;
}

static void normalize_mount_path(const char *in, char *out, size_t out_sz)
{
    if (!in || !out || out_sz == 0)
        return;

    strncpy(out, in, out_sz - 1);
    out[out_sz - 1] = '\0';

    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/')
    {
        out[len - 1] = '\0';
        len--;
    }
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

int vfs_register_fs(struct vfs_fstype *fs)
{
    if (!fs || !fs->name || strlen(fs->name) == 0)
        return -EINVAL;

    for (struct vfs_fstype *t = fs_types; t; t = t->next)
        if (strcmp(t->name, fs->name) == 0)
            return -EEXIST;

    fs->next = fs_types;
    fs_types = fs;
    return 0;
}

int vfs_init(void)
{
    fs_types = NULL;
    mounts   = NULL;
#if CONFIG_ENABLE_FS_MINIX
    minix_fs_register();
#endif
#if CONFIG_ENABLE_FS_TMPFS
    tmpfs_register();
    hostshare_9p_register();
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
    simplefs_register();
#endif
#if CONFIG_ENABLE_FS_FAT16
    fat16_fs_register();
#endif
#if CONFIG_ENABLE_FS_EXT2
    ext2_fs_register();
#endif
    if (DEBUG_VFS)
    {
        klog_info("VFS", "CLASSIFY VFS_FS_CONTRACT_ACTIVE");
        klog_info("VFS", "CLASSIFY VFS_FS_CONTRACT_DOCUMENTED");
        klog_info("VFS", "CLASSIFY SYSCALLS_USE_VFS_ONLY");
        klog_info("VFS", "CLASSIFY FUTURE_FS_READY");
    }
    return 0;
}

struct vfs_mount *vfs_get_mounts(void)
{
    return mounts;
}

int vfs_sync(void)
{
    /*
     * Best-effort: flush every registered block device. Per-FS sync hooks
     * can be added to vfs_ops later; MINIX already writes through on mutate.
     */
    ir0_block_flush_all();
    return 0;
}

int vfs_mount(const char *dev, const char *path, const char *fstype)
{
    char mount_path[MAX_PATH];
    const char *resolved_fstype = fstype;

    if (!fstype || !path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    int ret = validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));

    /* ramfs is intentionally an alias of tmpfs in IR0 runtime mounts. */
    if (strcmp(resolved_fstype, "ramfs") == 0)
        resolved_fstype = "tmpfs";

    for (struct vfs_mount *existing = mounts; existing; existing = existing->next)
    {
        if (strcmp(existing->path, mount_path) == 0)
            return -EBUSY;
    }

    struct vfs_fstype *ft = NULL;
    for (struct vfs_fstype *t = fs_types; t; t = t->next) {
        if (strcmp(t->name, resolved_fstype) == 0) {
            ft = t;
            break;
        }
    }
    if (!ft)
        return -ENODEV;

    struct vfs_mount *m = kmalloc(sizeof(*m));
    if (!m)
        return -ENOMEM;

    strncpy(m->path, mount_path, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = '\0';
    if (dev) {
        strncpy(m->dev, dev, sizeof(m->dev) - 1);
        m->dev[sizeof(m->dev) - 1] = '\0';
    } else {
        m->dev[0] = '\0';
    }
    m->fs = ft;
    m->flags = 0;
    m->next = mounts;

    ret = ft->mount(dev, mount_path);
    if (ret != 0)
    {
        kfree(m);
        return ret;
    }

    mounts = m;
    return 0;
}

int vfs_remount(const char *path, unsigned long flags)
{
    char mount_path[MAX_PATH];
    struct vfs_mount *m;
    int ret;

    if (!path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    ret = validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));
    m = find_mount(mount_path);
    if (!m)
        return -EINVAL;

    /* Only RO bit is toggled today; other MS_* bits are ignored honestly. */
    if (flags & IR0_MS_RDONLY)
        m->flags |= IR0_MS_RDONLY;
    else
        m->flags &= ~IR0_MS_RDONLY;
    return 0;
}

int vfs_umount(const char *path)
{
    char mount_path[MAX_PATH];
    struct vfs_mount *prev = NULL;
    struct vfs_mount *victim = NULL;
    int ret;

    if (!path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    ret = validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));
    if (strcmp(mount_path, "/") == 0)
        return -EBUSY;

    for (struct vfs_mount *m = mounts; m; m = m->next)
    {
        if (strcmp(m->path, mount_path) == 0)
        {
            victim = m;
            break;
        }
        prev = m;
    }

    if (!victim)
        return -ENOENT;

    for (struct vfs_mount *m = mounts; m; m = m->next)
    {
        size_t vlen = strlen(victim->path);
        if (m == victim)
            continue;
        if (strncmp(m->path, victim->path, vlen) == 0 &&
            m->path[vlen] == '/')
            return -EBUSY;
    }

    if (victim->fs && victim->fs->umount)
    {
        int ret = victim->fs->umount(victim->path);
        if (ret != 0)
            return ret;
    }

    if (prev)
        prev->next = victim->next;
    else
        mounts = victim->next;

    kfree(victim);
    return 0;
}

int vfs_init_root(void)
{
    const char *root_blk = CONFIG_ROOT_BLOCK_DEVICE;
    const char *root_fs = CONFIG_ROOT_FILESYSTEM;
    char root_dev_path[64];
    int can_use_block_dev = 1;

#define VFS_MSG(s) klog_info("VFS", (s))
#define VFS_ERR(s) klog_error("VFS", (s))

    VFS_MSG("Initializing VFS...");
    int ret = vfs_init();
    if (ret != 0) {
        VFS_ERR("vfs_init failed");
        return ret;
    }
    VFS_MSG("vfs_init OK (builtin filesystems registered)");

    snprintf(root_dev_path, sizeof(root_dev_path), "/dev/%s", root_blk);

#if !CONFIG_ENABLE_FS_MINIX
    if (strcmp(root_fs, "minix") == 0)
        can_use_block_dev = 0;
#endif

    if (can_use_block_dev && !ir0_block_name_is_present(root_blk)) {
        VFS_ERR("No block device configured for root available");
        return -ENODEV;
    }

    VFS_MSG("Mounting root filesystem...");
    ret = vfs_mount(root_dev_path, "/", root_fs);
    if (ret != 0) {
        VFS_MSG("configured root mount failed, falling back to tmpfs root");
#if CONFIG_ENABLE_FS_TMPFS
        ret = vfs_mount("none", "/", "tmpfs");
        if (ret != 0) {
            VFS_ERR("tmpfs fallback also failed");
            return ret;
        }
        VFS_MSG("tmpfs root mounted (fallback)");
#else
        VFS_ERR("tmpfs fallback disabled by configuration");
        return ret;
#endif
    } else {
        VFS_MSG("vfs_mount OK (configured root)");
    }
#undef VFS_MSG
#undef VFS_ERR
    return 0;
}

int vfs_init_with_minix(void)
{
    return vfs_init_root();
}

/* ------------------------------------------------------------------ */
/*  File operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!out)     return -EFAULT;
    if (!ir0_current_cred())
        return -ESRCH;

    if (!ir0_open_flags_ok_for_vfs(flags))
    {
        if (DEBUG_VFS)
            klog_info("VFS", "CLASSIFY VFS_LINUX_RAW_FLAGS_REJECTED");
        return -EINVAL;
    }

    if (mount_is_readonly(path) &&
        ((flags & IR0_O_ACCMODE) != IR0_O_RDONLY || (flags & IR0_O_CREAT)))
        return -EROFS;

    ret = check_dir_traverse(path);
    if (ret != 0)
        return ret;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops)
        return -ENODEV;

    if (flags & IR0_O_CREAT)
    {
        char parent[MAX_PATH];
        stat_t st;
        int target_exists;

        if (parent_dir(path, parent, sizeof(parent)) == 0)
        {
            if (!ops->stat || vfs_ops_stat(ops, parent, &st) != 0)
                return -ENOENT;
            if (!ir0_check_file_access(parent, ACCESS_WRITE))
                return -EACCES;
        }

        target_exists = (ops->stat && vfs_ops_stat(ops, path, &st) == 0) ? 1 : 0;

        if (target_exists && S_ISDIR(st.st_mode) && !(flags & IR0_O_DIRECTORY))
        {
            ret = vfs_rmdir_recursive(path);
            if (ret != 0)
                return ret;
            target_exists = 0;
        }

        if (!target_exists)
        {
            if (ops->create)
            {
                /* open(2): the caller's mode is authoritative, 0000 included. */
                mode_t fmode = mode & 07777;

                ret = vfs_ops_create(ops, path, fmode);
                if (ret == -EISDIR)
                {
                    ret = vfs_rmdir_recursive(path);
                    if (ret == 0)
                        ret = vfs_ops_create(ops, path, fmode);
                }
                if (ret != 0)
                    return ret;
                if (DEBUG_VFS)
                    klog_info("VFS", "CLASSIFY VFS_CREATE_SEMANTICS_GENERIC");
            }
            else
            {
                return -ENOSYS;
            }
        }
        else if (flags & IR0_O_EXCL)
        {
            return -EEXIST;
        }
    }
    else
    {
        int acc = 0;
        int accmode = flags & IR0_O_ACCMODE;
        stat_t st;

        if (!ops->stat || vfs_ops_stat(ops, path, &st) != 0)
            return -ENOENT;

        if (accmode == IR0_O_RDONLY || accmode == IR0_O_RDWR)
            acc |= 0x1;
        if (accmode == IR0_O_WRONLY || accmode == IR0_O_RDWR)
            acc |= 0x2;
        if (!ir0_check_file_access(path, acc))
            return -EACCES;
    }

    if ((flags & IR0_O_TRUNC) && ((flags & IR0_O_ACCMODE) != IR0_O_RDONLY))
    {
        stat_t st;

        if (!ops->stat || vfs_ops_stat(ops, path, &st) != 0)
            return -ENOENT;
        if (S_ISDIR(st.st_mode))
            return -EISDIR;
        if (S_ISREG(st.st_mode))
        {
            ret = vfs_truncate(path, 0);
            if (ret != 0)
                return ret;
            if (DEBUG_VFS)
                klog_info("VFS", "CLASSIFY VFS_TRUNCATE_GENERIC");
        }
    }

    struct vfs_file *f = kmalloc_try(sizeof(*f));
    if (!f)
        return -ENOMEM;

    strncpy(f->path, path, sizeof(f->path) - 1);
    f->path[sizeof(f->path) - 1] = '\0';
    f->pos   = 0;
    f->flags = flags;
    f->ref_count = 1;
    *out = f;
    return 0;
}

int vfs_read(struct vfs_file *f, char *buf, size_t count)
{
    if (!f)   return -EBADF;
    if (!buf) return -EFAULT;
    if (count == 0) return 0;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_WRONLY)
        return -EBADF;

    struct vfs_ops *ops = ops_for_path(f->path);
    if (!ops || !ops->read)
        return -ENOSYS;

    size_t done = 0;
    int ret = vfs_ops_read(ops, f->path, buf, count, &done, f->pos);
    if (vfs_exec_audit_active)
    {
        vfs_exec_audit_log("vfs_read", f->path, ret, f->pos, count, done, 0, -1);
    }
    if (ret != 0)
        return ret;
    f->pos += (off_t)done;
    return (int)done;
}

int vfs_write(struct vfs_file *f, const char *buf, size_t count)
{
    if (!f)   return -EBADF;
    if (!buf) return -EFAULT;
    if (count == 0) return 0;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_RDONLY)
        return -EBADF;
    if (mount_is_readonly(f->path))
        return -EROFS;

    struct vfs_ops *ops = ops_for_path(f->path);
    if (!ops || !ops->write)
        return -ENOSYS;

    /*
     * O_APPEND: move to end before every write.
     * We grab the current size via stat.
     */
    if (f->flags & IR0_O_APPEND) {
        stat_t st;
        if (ops->stat && vfs_ops_stat(ops, f->path, &st) == 0)
            f->pos = st.st_size;
    }

    size_t done = 0;
    int ret = vfs_ops_write(ops, f->path, buf, count, &done, f->pos);
    if (ret != 0)
        return ret;
    f->pos += (off_t)done;
    return (int)done;
}

int vfs_pread(struct vfs_file *f, char *buf, size_t count, off_t offset)
{
    if (!f)
        return -EBADF;
    if (!buf)
        return -EFAULT;
    if (count == 0)
        return 0;
    if (offset < 0)
        return -EINVAL;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_WRONLY)
        return -EBADF;

    struct vfs_ops *ops = ops_for_path(f->path);
    if (!ops || !ops->read)
        return -ENOSYS;

    size_t done = 0;
    int ret = vfs_ops_read(ops, f->path, buf, count, &done, offset);

    if (ret != 0)
        return ret;
    return (int)done;
}

int vfs_pwrite(struct vfs_file *f, const char *buf, size_t count, off_t offset)
{
    if (!f)
        return -EBADF;
    if (!buf)
        return -EFAULT;
    if (count == 0)
        return 0;
    if (offset < 0)
        return -EINVAL;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_RDONLY)
        return -EBADF;
    if (mount_is_readonly(f->path))
        return -EROFS;

    struct vfs_ops *ops = ops_for_path(f->path);
    if (!ops || !ops->write)
        return -ENOSYS;

    size_t done = 0;
    int ret = vfs_ops_write(ops, f->path, buf, count, &done, offset);

    if (ret != 0)
        return ret;
    return (int)done;
}

int vfs_close(struct vfs_file *f)
{
    if (!f)
        return -EBADF;
    if (f->ref_count > 0)
        f->ref_count--;
    if (f->ref_count <= 0) {
        /* Last reference to the description: its flock(2) lock dies with it. */
        ir0_flock_release_file(f);
        kfree(f);
    }
    return 0;
}

void vfs_file_acquire(struct vfs_file *f)
{
    if (!f)
        return;
    f->ref_count++;
}

off_t vfs_lseek(struct vfs_file *f, off_t offset, int whence)
{
    if (!f)
        return -EBADF;

    off_t new_pos;
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = f->pos + offset;
        break;
    case SEEK_END: {
        struct vfs_ops *ops = ops_for_path(f->path);
        if (!ops || !ops->stat)
            return -ENOSYS;
        stat_t st;
        if (vfs_ops_stat(ops, f->path, &st) != 0)
            return -EIO;
        new_pos = st.st_size + offset;
        break;
    }
    default:
        return -EINVAL;
    }

    if (new_pos < 0)
        return -EINVAL;
    f->pos = new_pos;
    return new_pos;
}

/* ------------------------------------------------------------------ */
/*  Path operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_stat(const char *path, stat_t *buf)
{
    if (!path || !buf)
        return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0) return ret;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->stat)
        return -ENODEV;
    memset(buf, 0, sizeof(*buf));
    return vfs_ops_stat(ops, path, buf);
}

int vfs_statfs(const char *path, struct ir0_statfs *buf)
{
	struct vfs_mount *m;
	const char *fst;
	int ret;

	if (!path || !buf)
		return -EINVAL;
	ret = validate_path(path);
	if (ret != 0)
		return ret;

	m = find_mount(path);
	if (!m || !m->fs || !m->fs->name)
		return -ENODEV;

	memset(buf, 0, sizeof(*buf));
	fst = m->fs->name;

#if CONFIG_ENABLE_FS_MINIX
	if (strcmp(fst, "minix") == 0)
	{
		int rc;

		vfs_fs_op_acquire();
		rc = minix_fs_statfs(buf);
		vfs_fs_op_release();
		return rc;
	}
#endif
#if CONFIG_ENABLE_FS_TMPFS
	if (strcmp(fst, "tmpfs") == 0)
	{
		buf->f_type = IR0_TMPFS_MAGIC;
		buf->f_bsize = 4096;
		buf->f_frsize = 4096;
		buf->f_blocks = 0;
		buf->f_bfree = 0;
		buf->f_bavail = 0;
		buf->f_namelen = 255;
		return 0;
	}
	if (strcmp(fst, "9p") == 0)
	{
		buf->f_type = IR0_9P_MAGIC;
		buf->f_bsize = 4096;
		buf->f_frsize = 4096;
		buf->f_blocks = 0;
		buf->f_bfree = 0;
		buf->f_bavail = 0;
		buf->f_namelen = 255;
		return 0;
	}
#endif
	/* Unknown FS: honest empty stats (df skips f_blocks==0 unless -a). */
	buf->f_bsize = 1024;
	buf->f_frsize = 1024;
	buf->f_namelen = 255;
	return 0;
}

int vfs_mkdir(const char *path, int mode)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    if (mount_is_readonly(path))
        return -EROFS;

    char parent[MAX_PATH];
    if (parent_dir(path, parent, sizeof(parent)) == 0)
        if (strcmp(parent, "/") != 0)
            if (!ir0_check_file_access(parent, ACCESS_WRITE))
                return -EACCES;

    if (strcmp(path, "/") == 0)
        return -EEXIST;

    stat_t st;
    if (vfs_stat(path, &st) == 0 && (st.st_mode & S_IFMT) != 0)
        return -EEXIST;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->mkdir)
        return -ENOSYS;
    return vfs_ops_mkdir(ops, path, (mode_t)mode);
}

int vfs_unlink(const char *path)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    if (mount_is_readonly(path))
        return -EROFS;
    if (strcmp(path, "/") == 0)
        return -EPERM;

    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return -ENOENT;

    if (S_ISDIR(st.st_mode))
        return -EISDIR;

    if (!ir0_check_file_access(path, ACCESS_WRITE))
        return -EACCES;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->unlink)
        return -ENOSYS;
    return vfs_ops_unlink(ops, path);
}

int vfs_link(const char *oldpath, const char *newpath)
{
    int ret = validate_path(oldpath);
    if (ret != 0) return ret;
    ret = validate_path(newpath);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st;
    if (vfs_stat(oldpath, &st) != 0)
        return -ENOENT;
    if (!ir0_check_file_access(oldpath, ACCESS_READ))
        return -EACCES;

    char parent[MAX_PATH];
    if (parent_dir(newpath, parent, sizeof(parent)) == 0)
        if (!ir0_check_file_access(parent, ACCESS_WRITE))
            return -EACCES;

    if (vfs_stat(newpath, &st) == 0)
        return -EEXIST;

    struct vfs_ops *ops = ops_for_path(oldpath);
    if (!ops || !ops->link)
        return -ENOSYS;
    return vfs_ops_link(ops, oldpath, newpath);
}

int vfs_symlink(const char *target, const char *linkpath)
{
    int ret;
    struct vfs_ops *ops;

    if (!target || !linkpath)
        return -EINVAL;
    ret = validate_path(linkpath);
    if (ret != 0)
        return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    ops = ops_for_path(linkpath);
    if (!ops || !ops->symlink)
        return -ENOSYS;
    return vfs_ops_symlink(ops, target, linkpath);
}

int vfs_readlink(const char *path, char *buf, size_t buflen)
{
    int ret;
    struct vfs_ops *ops;

    if (!path || !buf || buflen == 0)
        return -EINVAL;
    ret = validate_path(path);
    if (ret != 0)
        return ret;
    ops = ops_for_path(path);
    if (!ops || !ops->readlink)
        return -ENOSYS;
    return vfs_ops_readlink(ops, path, buf, buflen);
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    int ret = validate_path(oldpath);
    if (ret != 0) return ret;
    ret = validate_path(newpath);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st_old;
    int retry_rename;

    if (vfs_stat(oldpath, &st_old) != 0)
        return -ENOENT;

    retry_rename = 0;
    if (S_ISDIR(st_old.st_mode))
    {
        if (named_fifo_is_runsv_supervise_regular_path(oldpath))
        {
            ret = vfs_clear_stale_for_regular_file(oldpath);
            if (ret != 0)
                return ret;
            retry_rename = 1;
        }
        else
            return -EISDIR;
    }

    if (retry_rename)
    {
        if (vfs_stat(oldpath, &st_old) != 0)
            return -ENOENT;
        if (S_ISDIR(st_old.st_mode))
            return -EISDIR;
    }

    if (!ir0_check_file_access(oldpath, ACCESS_READ | ACCESS_WRITE))
        return -EACCES;

    char parent[MAX_PATH];
    if (parent_dir(newpath, parent, sizeof(parent)) == 0)
        if (!ir0_check_file_access(parent, ACCESS_WRITE))
            return -EACCES;

    struct vfs_mount *m_old = find_mount(oldpath);
    struct vfs_mount *m_new = find_mount(newpath);
    if (m_old != m_new)
        return -EXDEV;

    stat_t st_new;

    if (vfs_stat(newpath, &st_new) == 0)
    {
        if (S_ISDIR(st_new.st_mode))
        {
            ret = vfs_rmdir_recursive(newpath);
            if (ret != 0)
                return ret;
        }
        else if (vfs_unlink(newpath) != 0)
        {
            return -EACCES;
        }
    }

    struct vfs_ops *ops = ops_for_path(oldpath);
    if (!ops)
        return -ENOSYS;
    if (ops->rename)
        return vfs_ops_rename(ops, oldpath, newpath);
    if (!ops->link)
        return -ENOSYS;
    ret = vfs_ops_link(ops, oldpath, newpath);
    if (ret != 0)
        return ret;
    return vfs_unlink(oldpath);
}

int vfs_rmdir(const char *path)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    char norm[MAX_PATH];
    if (normalize_path(path, norm, sizeof(norm)) != 0)
        return -ENAMETOOLONG;
    if (strcmp(norm, "/") == 0)
        return -EPERM;

    stat_t st;
    if (vfs_stat(norm, &st) != 0)
        return -ENOENT;
    if (!S_ISDIR(st.st_mode))
        return -ENOTDIR;

    ret = check_dir_traverse(norm);
    if (ret != 0)
        return ret;

    char parent[MAX_PATH];
    if (parent_dir(norm, parent, sizeof(parent)) == 0)
    {
        if (!ir0_check_file_access(parent, ACCESS_WRITE | ACCESS_EXEC))
            return -EACCES;
    }

    struct vfs_ops *ops = ops_for_path(norm);
    if (!ops || !ops->rmdir)
        return -ENOSYS;
    return vfs_ops_rmdir(ops, norm);
}

static int rmdir_recursive_impl(const char *path, int depth)
{
    if (depth > 32)
        return -ELOOP;

    int ret = validate_path(path);
    if (ret != 0) return ret;

    char norm[MAX_PATH];
    if (normalize_path(path, norm, sizeof(norm)) != 0)
        return -ENAMETOOLONG;
    if (norm[0] == '/' && norm[1] == '\0')
        return -EPERM;

    stat_t st;
    if (vfs_stat(norm, &st) != 0)
        return -ENOENT;
    if (!S_ISDIR(st.st_mode))
        return vfs_unlink(norm);

    struct vfs_dirent entries[32];
    int n = vfs_readdir(norm, entries, 32);
    if (n < 0) {
        struct vfs_ops *ops = ops_for_path(norm);
        if (ops && ops->rmdir)
            return vfs_ops_rmdir(ops, norm);
        return n;
    }

    size_t nlen = strlen(norm);
    for (int i = 0; i < n; i++) {
        if (entries[i].name[0] == '\0')
            continue;
        if (entries[i].name[0] == '.' &&
            (entries[i].name[1] == '\0' ||
             (entries[i].name[1] == '.' && entries[i].name[2] == '\0')))
            continue;

        char full[MAX_PATH];
        if (build_path(full, sizeof(full), norm, entries[i].name) != 0)
            continue;
        char resolved[MAX_PATH];
        if (normalize_path(full, resolved, sizeof(resolved)) != 0)
            continue;
        if (resolved[0] == '/' && resolved[1] == '\0')
            continue;
        if (strcmp(resolved, norm) == 0)
            continue;

        size_t rlen = strlen(resolved);
        if (rlen < nlen)
            continue;
        if (rlen <= nlen || strncmp(resolved, norm, nlen) != 0 || resolved[nlen] != '/')
            continue;

        stat_t est;
        if (vfs_stat(resolved, &est) == 0) {
            if (S_ISDIR(est.st_mode)) {
                int rc = rmdir_recursive_impl(resolved, depth + 1);
                if (rc != 0)
                    return rc;
            } else {
                int rc = vfs_unlink(resolved);
                if (rc != 0)
                    return rc;
            }
        }
    }

    struct vfs_ops *ops = ops_for_path(norm);
    if (ops && ops->rmdir)
        return vfs_ops_rmdir(ops, norm);
    return -ENOSYS;
}

int vfs_rmdir_recursive(const char *path)
{
    return rmdir_recursive_impl(path, 0);
}

/*
 * vfs_clear_stale_for_regular_file - Remove wrong-type VFS nodes blocking O_CREAT.
 * Clears empty/non-empty directories and non-regular files so a regular file or
 * named FIFO can be created at @path (runit supervise/pid.new, mkfifo paths).
 */
int vfs_clear_stale_for_regular_file(const char *path)
{
    stat_t st;
    int rc;

    if (!path)
        return -EINVAL;

    for (;;)
    {
        if (vfs_stat(path, &st) != 0)
            return 0;
        if (S_ISREG(st.st_mode))
        {
            rc = vfs_unlink(path);
            if (rc == -EISDIR)
                rc = vfs_rmdir_recursive(path);
            if (rc != 0)
                return rc;
            continue;
        }
        if (S_ISFIFO(st.st_mode))
            return 0;
        if (S_ISDIR(st.st_mode))
        {
            rc = vfs_rmdir_recursive(path);
            if (rc != 0)
                return rc;
            continue;
        }
        rc = vfs_unlink(path);
        if (rc == -EISDIR)
            rc = vfs_rmdir_recursive(path);
        if (rc != 0)
            return rc;
    }
}

int vfs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
    if (!path || !entries || max <= 0)
        return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    if (!ir0_check_file_access(path, ACCESS_EXEC))
        return -EACCES;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->readdir)
        return -ENOSYS;
    return vfs_ops_readdir(ops, path, entries, max);
}

int vfs_chown(const char *path, uid_t owner, gid_t group)
{
    if (!path) return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    if (!ir0_cred_is_root())
        return -EPERM;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->chown)
        return -ENOSYS;
    return vfs_ops_chown(ops, path, owner, group);
}

int vfs_chmod(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st;
    ret = vfs_stat(path, &st);
    if (ret != 0)
        return ret;

    if (!ir0_cred_is_root() && ir0_current_cred()->euid != st.st_uid)
        return -EPERM;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->chmod)
        return -ENOSYS;
    return vfs_ops_chmod(ops, path, mode);
}

int vfs_truncate(const char *path, size_t length)
{
    if (!path)
        return -EINVAL;
    if (!ir0_current_cred())
        return -ESRCH;
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    if (!ir0_check_file_access(path, ACCESS_WRITE))
        return -EACCES;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->truncate)
        return -ENOSYS;
    return vfs_ops_truncate(ops, path, length);
}

int vfs_utimens(const char *path, const struct timespec times[2])
{
    struct vfs_ops *ops;
    stat_t st;

    if (!path)
        return -EINVAL;
    if (!ir0_current_cred())
        return -ESRCH;
    if (validate_path(path) != 0)
        return -EINVAL;
    if (vfs_stat(path, &st) != 0)
        return -ENOENT;

    ops = ops_for_path(path);
    if (!ops || !ops->utimens)
        return -ENOSYS;
    return vfs_ops_utimens(ops, path, times);
}


int vfs_read_file(const char *path, void **data, size_t *size)
{
    struct vfs_ops *ops;
    stat_t st;
    int ret;
    size_t fsize;
    void *buf;
    size_t total;

    if (!path || !data || !size || path[0] != '/')
    {
        if (vfs_exec_audit_active)
            vfs_exec_audit_log("read_file_bad_path", path, -EINVAL, 0, 0, 0, 0, -1);
        return -EINVAL;
    }

    ops = ops_for_path(path);
    if (!ops || !ops->read || !ops->stat)
    {
        if (vfs_exec_audit_active)
        {
            klog_print("[EXEC_AUDIT][VFS] stage=lookup_no_ops path=");
            klog_print(path);
            klog_print(" ops=");
            klog_hex64((uint64_t)(uintptr_t)ops);
            klog_print("\n");
            vfs_exec_audit_log("lookup_fail", path, -ENOSYS, 0, 0, 0, 0, -1);
        }
        return -ENOSYS;
    }

    if (vfs_exec_audit_active)
    {
        klog_print("[EXEC_AUDIT][VFS] stage=lookup_ok path=");
        klog_print(path);
        klog_print(" ops=");
        klog_hex64((uint64_t)(uintptr_t)ops);
        klog_print("\n");
    }

    vfs_fs_op_acquire();

    ret = ops->stat(path, &st);
    if (ret != 0)
    {
        vfs_exec_audit_log("stat_fail", path, ret, 0, 0, 0, 0, -1);
        vfs_fs_op_release();
        return ret;
    }

    if (st.st_size < 0)
    {
        vfs_exec_audit_log("stat_bad_size", path, -EIO, 0, 0, 0,
                           (uint64_t)st.st_ino, st.st_size);
        vfs_fs_op_release();
        return -EIO;
    }

    fsize = (size_t)st.st_size;
    vfs_exec_audit_log("stat_ok", path, 0, 0, 0, 0,
                       (uint64_t)st.st_ino, st.st_size);

    if (fsize > VFS_READ_FILE_MAX_BYTES)
    {
        vfs_exec_audit_log("file_too_large", path, -EFBIG, 0, fsize, 0,
                           (uint64_t)st.st_ino, st.st_size);
        vfs_fs_op_release();
        return -EFBIG;
    }
    if (fsize == 0)
    {
        *data = NULL;
        *size = 0;
        vfs_exec_audit_log("file_size_zero", path, 0, 0, 0, 0,
                           (uint64_t)st.st_ino, 0);
        vfs_fs_op_release();
        return 0;
    }

    buf = kmalloc_try(fsize);
    if (!buf)
    {
        vfs_exec_audit_log("kmalloc_fail", path, -ENOMEM, 0, fsize, 0,
                           (uint64_t)st.st_ino, st.st_size);
        vfs_fs_op_release();
        return -ENOMEM;
    }

    total = 0;

    while (total < fsize)
    {
        size_t chunk = 0;

        ret = ops->read(path, (uint8_t *)buf + total, fsize - total,
                        &chunk, (off_t)total);
        if (ret != 0)
        {
            vfs_exec_audit_log("read_err", path, ret, (off_t)total,
                               fsize - total, total,
                               (uint64_t)st.st_ino, st.st_size);
            exec_read_trace_vfs_read_file(path, (uint64_t)st.st_ino,
                                          st.st_size, (off_t)total,
                                          fsize - total, ret);
            kfree(buf);
            vfs_fs_op_release();
            return ret;
        }
        if (chunk == 0)
        {
            vfs_exec_audit_log("read_zero", path, -EIO, (off_t)total,
                               fsize - total, total,
                               (uint64_t)st.st_ino, st.st_size);
            kfree(buf);
            vfs_fs_op_release();
            return -EIO;
        }
        vfs_exec_audit_log("read_chunk", path, 0, (off_t)total,
                           fsize - total, chunk,
                           (uint64_t)st.st_ino, st.st_size);
        total += chunk;
    }

    if (total < fsize)
    {
        vfs_exec_audit_log("read_short", path, -EIO, (off_t)total,
                           fsize, total,
                           (uint64_t)st.st_ino, st.st_size);
        kfree(buf);
        vfs_fs_op_release();
        return -EIO;
    }

    *data = buf;
    *size = total;
    vfs_exec_audit_log("read_ok", path, 0, 0, fsize, total,
                       (uint64_t)st.st_ino, st.st_size);
    vfs_fs_op_release();
    return 0;
}
