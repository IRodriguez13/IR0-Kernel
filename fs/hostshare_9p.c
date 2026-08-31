/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: hostshare_9p.c
 * Description: VFS fstype "9p" backed by virtio-9p (QEMU -virtfs host share).
 */

#include <ir0/kmem.h>
#include "vfs.h"
#include <ir0/errno.h>
#include <ir0/virtio_9p.h>
#include <string.h>
#include <stddef.h>

static char g_mount_point[VFS_PATH_MAX];
static int g_mounted;

static const char *rel_of(const char *path)
{
	size_t mlen;

	if (!path || !g_mounted)
		return NULL;
	mlen = strlen(g_mount_point);
	if (strncmp(path, g_mount_point, mlen) != 0)
		return NULL;
	if (path[mlen] == '\0')
		return "";
	if (path[mlen] != '/')
		return NULL;
	return path + mlen + 1;
}

static int hs_stat(const char *path, stat_t *buf)
{
	uint64_t size = 0;
	uint32_t mode = 0;
	int rc;
	const char *rel = rel_of(path);

	if (!buf)
		return -EFAULT;
	if (!rel)
		return -ENOENT;
	if (rel[0] == '\0')
	{
		memset(buf, 0, sizeof(*buf));
		buf->st_mode = S_IFDIR | 0777;
		return 0;
	}
	rc = virtio_9p_stat_file(rel, &size, &mode);
	if (rc < 0)
		return rc;
	memset(buf, 0, sizeof(*buf));
	if (mode != 0)
		buf->st_mode = (mode_t)mode;
	else
		buf->st_mode = S_IFREG | 0755;
	if (S_ISLNK(buf->st_mode))
	{
		/* Keep host symlink type; liberal perms for path checks. */
		buf->st_mode = S_IFLNK | 0777;
	}
	else if (S_ISDIR(buf->st_mode))
		buf->st_mode = S_IFDIR | 0777;
	else if ((buf->st_mode & 0777) == 0)
		buf->st_mode |= 0755;
	buf->st_uid = 0;
	buf->st_gid = 0;
	buf->st_size = (off_t)size;
	return 0;
}

static int hs_mkdir_one(const char *rel, mode_t mode)
{
	uint64_t size = 0;
	uint32_t stmode = 0;
	int rc;

	if (!rel || !rel[0])
		return -EINVAL;
	rc = virtio_9p_stat_file(rel, &size, &stmode);
	if (rc == 0)
	{
		if (S_ISDIR(stmode))
			return 0;
		return -EEXIST;
	}
	return virtio_9p_mkdir(rel, (uint32_t)(mode ? (mode & 0777) : 0755));
}

static int hs_mkdir(const char *path, mode_t mode)
{
	const char *rel = rel_of(path);
	char tmp[VFS_PATH_MAX];
	size_t i;

	if (!rel || !rel[0])
		return -EINVAL;
	if (strlen(rel) >= sizeof(tmp))
		return -ENAMETOOLONG;
	memcpy(tmp, rel, strlen(rel) + 1);

	/* mkdir -p: create each path component */
	for (i = 0; tmp[i]; i++)
	{
		if (tmp[i] != '/')
			continue;
		tmp[i] = '\0';
		if (tmp[0])
		{
			int rc = hs_mkdir_one(tmp, mode);

			if (rc < 0 && rc != -EEXIST)
				return rc;
		}
		tmp[i] = '/';
	}
	return hs_mkdir_one(tmp, mode);
}

static int hs_create(const char *path, mode_t mode)
{
	const char *rel = rel_of(path);
	(void)mode;
	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_write_file(rel, "", 0);
}

static int hs_read(const char *path, void *buf, size_t count, size_t *bytes_read,
		   off_t offset)
{
	size_t got = 0;
	int rc;
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EISDIR;
	if (offset < 0)
		return -EINVAL;
	rc = virtio_9p_read_at(rel, buf, count, (uint64_t)offset, &got);
	if (rc < 0)
		return rc;
	if (bytes_read)
		*bytes_read = got;
	return 0;
}

static int hs_write(const char *path, const void *buf, size_t count,
		    size_t *bytes_written, off_t offset)
{
	const char *rel = rel_of(path);
	int rc;

	if (!rel || !rel[0])
		return -EISDIR;
	if (offset < 0)
		return -EINVAL;
	if (offset == 0)
		rc = virtio_9p_write_file(rel, buf, count);
	else
		rc = virtio_9p_write_at(rel, buf, count, (uint64_t)offset);
	if (rc < 0)
		return rc;
	if (bytes_written)
		*bytes_written = count;
	return 0;
}

static int hs_truncate(const char *path, size_t length)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EISDIR;
	if (length == 0)
		return virtio_9p_write_file(rel, "", 0);
	return virtio_9p_truncate(rel, (uint64_t)length);
}

static int hs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	const char *rel = rel_of(path);
	/*
	 * On the stack this array made the frame 4 KiB, on a getdents chain
	 * that already spent most of the kernel stack before reaching 9p.
	 */
	virtio_9p_dirent_t *buf;
	int n;
	int i;
	int out = 0;

	if (!rel || !entries || max <= 0)
		return -EINVAL;

	buf = kmalloc_try(sizeof(*buf) * 64);
	if (!buf)
		return -ENOMEM;

	n = virtio_9p_readdir(rel, buf, 64);
	if (n < 0)
	{
		kfree(buf);
		return n;
	}
	for (i = 0; i < n && out < max; i++)
	{
		strncpy(entries[out].name, buf[i].name, sizeof(entries[out].name) - 1);
		entries[out].name[sizeof(entries[out].name) - 1] = '\0';
		entries[out].type = buf[i].type;
		out++;
	}
	kfree(buf);
	return out;
}

static int hs_unlink(const char *path)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_unlink(rel, 0);
}

static int hs_rmdir(const char *path)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_unlink(rel, 1);
}

static int hs_link(const char *oldpath, const char *newpath)
{
	const char *old_rel = rel_of(oldpath);
	const char *new_rel = rel_of(newpath);

	if (!old_rel || !old_rel[0] || !new_rel || !new_rel[0])
		return -EINVAL;
	return virtio_9p_link(old_rel, new_rel);
}

static int hs_rename(const char *oldpath, const char *newpath)
{
	const char *old_rel = rel_of(oldpath);
	const char *new_rel = rel_of(newpath);

	if (!old_rel || !old_rel[0] || !new_rel || !new_rel[0])
		return -EINVAL;
	return virtio_9p_rename(old_rel, new_rel);
}

static int hs_symlink(const char *target, const char *linkpath)
{
	const char *rel = rel_of(linkpath);

	if (!target || !rel || !rel[0])
		return -EINVAL;
	return virtio_9p_symlink(rel, target);
}

static int hs_readlink(const char *path, char *buf, size_t buflen)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0] || !buf || buflen == 0)
		return -EINVAL;
	return virtio_9p_readlink(rel, buf, buflen);
}

static struct vfs_ops hs_ops = {
	.stat = hs_stat,
	.mkdir = hs_mkdir,
	.create = hs_create,
	.unlink = hs_unlink,
	.rmdir = hs_rmdir,
	.link = hs_link,
	.rename = hs_rename,
	.symlink = hs_symlink,
	.readlink = hs_readlink,
	.read = hs_read,
	.write = hs_write,
	.truncate = hs_truncate,
	.readdir = hs_readdir,
};

static int hs_mount(const char *dev, const char *dir)
{
	(void)dev;
	if (!dir || !virtio_9p_ready())
		return -ENODEV;
	strncpy(g_mount_point, dir, sizeof(g_mount_point) - 1);
	g_mount_point[sizeof(g_mount_point) - 1] = '\0';
	g_mounted = 1;
	return 0;
}

static int hs_umount(const char *dir)
{
	(void)dir;
	g_mounted = 0;
	g_mount_point[0] = '\0';
	return 0;
}

static struct vfs_fstype hs_fstype = {
	.name = "9p",
	.ops = &hs_ops,
	.mount = hs_mount,
	.umount = hs_umount,
	.next = NULL,
};

int hostshare_9p_register(void)
{
	return vfs_register_fs(&hs_fstype);
}
