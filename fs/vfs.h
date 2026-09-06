/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.h
 * Description: IR0 kernel source/header file
 */

/* vfs.h - Minimal path-based Virtual File System */
#pragma once

#include <ir0/stat.h>
#include <ir0/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>
#include <ir0/vfs_backend.h>

struct vfs_dirent;

#define VFS_PATH_MAX 256
#define VFS_NAME_MAX 255

/* struct vfs_ops — see includes/ir0/vfs_backend.h */

/* Filesystem type descriptor — one per FS driver */
struct vfs_fstype {
    const char *name;
    struct vfs_ops *ops;
    int (*mount)(const char *dev, const char *dir);
    int (*umount)(const char *dir);
    struct vfs_fstype *next;
};

/* Mount table entry */
struct vfs_mount {
    char path[VFS_PATH_MAX];
    char dev[64];
    struct vfs_fstype *fs;
    unsigned long flags; /* MS_RDONLY and friends */
    struct vfs_mount *next;
};

/* Linux mount(2) flag bits we honour today (values match linux/fs.h). */
#define IR0_MS_RDONLY  1
#define IR0_MS_REMOUNT 32
#define IR0_MS_BIND    4096
/* Flags that are not implemented yet — reject on new mounts. */
#define IR0_MS_UNSUPPORTED (IR0_MS_BIND)

int vfs_remount(const char *path, unsigned long flags);

/* Open file handle — created by vfs_open, freed by vfs_close */
struct vfs_file {
    char path[VFS_PATH_MAX];
    off_t pos;
    int flags;
    int ref_count;
};

/* Directory entry returned by vfs_readdir */
struct vfs_dirent {
    char name[VFS_PATH_MAX];
    uint8_t type;
};

/*
 * d_type values for vfs_dirent (Linux getdents subset, stable ABI).
 */
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
#define DT_LNK     10

/* Lifecycle */
int vfs_init(void);
int vfs_register_fs(struct vfs_fstype *fs);
int vfs_mount(const char *dev, const char *path, const char *fstype);
int vfs_umount(const char *path);
int vfs_init_with_minix(void);
int vfs_init_root(void);

/* Mount table (single linked list, newest first); NULL if empty */
struct vfs_mount *vfs_get_mounts(void);

/* Best-effort sync: flush all registered block devices (FS-level sync later). */
int vfs_sync(void);

/* File operations — @flags must be IR0_O_* (see includes/ir0/open_flags.h) */
int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out);
int vfs_read(struct vfs_file *f, char *buf, size_t count);
int vfs_write(struct vfs_file *f, const char *buf, size_t count);
int vfs_pread(struct vfs_file *f, char *buf, size_t count, off_t offset);
int vfs_pwrite(struct vfs_file *f, const char *buf, size_t count, off_t offset);
int vfs_close(struct vfs_file *f);
off_t vfs_lseek(struct vfs_file *f, off_t offset, int whence);
void vfs_file_acquire(struct vfs_file *f);

/* Path operations */
int vfs_stat(const char *path, stat_t *buf);
struct ir0_statfs;
int vfs_statfs(const char *path, struct ir0_statfs *buf);
int vfs_mkdir(const char *path, int mode);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rmdir_recursive(const char *path);
int vfs_clear_stale_for_regular_file(const char *path);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_readlink(const char *path, char *buf, size_t buflen);
int vfs_readdir(const char *path, struct vfs_dirent *entries, int max);
int vfs_chown(const char *path, uid_t owner, gid_t group);
int vfs_chmod(const char *path, mode_t mode);
int vfs_truncate(const char *path, size_t length);
int vfs_utimens(const char *path, const struct timespec times[2]);

/* Bulk read helper (ELF loader, etc.) */
int vfs_read_file(const char *path, void **data, size_t *size);

/* Non-zero when the mount holding @path may raise privileges on exec. */
int vfs_path_allows_setid(const char *path);

/* Exec-loader observational audit (single-threaded). */
void vfs_exec_audit_begin(const char *path);
void vfs_exec_audit_end(void);
int vfs_exec_audit_is_active(void);
