/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ext2_disk.c
 * Description: Minimal read-only EXT2 (superblock + inodes + direct blocks).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ext2_disk.h"
#include "vfs.h"
#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/klog.h>
#include <ir0/stat.h>
#include <string.h>

#define EXT2_MAGIC 0xEF53
#define EXT2_ROOT_INO 2
#define EXT2_MAX_MOUNTS 2
#define EXT2_MAX_PATH 256
#define EXT2_NAME_MAX 255

struct ext2_super
{
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	uint32_t s_log_frag_size;
	uint32_t s_blocks_per_group;
	uint32_t s_frags_per_group;
	uint32_t s_inodes_per_group;
	uint32_t s_mtime;
	uint32_t s_wtime;
	uint16_t s_mnt_count;
	uint16_t s_max_mnt_count;
	uint16_t s_magic;
	uint16_t s_state;
	uint16_t s_errors;
	uint16_t s_minor_rev_level;
	uint32_t s_lastcheck;
	uint32_t s_checkinterval;
	uint32_t s_creator_os;
	uint32_t s_rev_level;
	uint16_t s_def_resuid;
	uint16_t s_def_resgid;
	uint32_t s_first_ino;
	uint16_t s_inode_size;
} __attribute__((packed));

struct ext2_bgd
{
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint8_t bg_reserved[12];
} __attribute__((packed));

struct ext2_inode
{
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_links_count;
	uint32_t i_blocks;
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[15];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t i_osd2[12];
} __attribute__((packed));

struct ext2_dirent
{
	uint32_t inode;
	uint16_t rec_len;
	uint8_t name_len;
	uint8_t file_type;
} __attribute__((packed));

struct ext2_vol
{
	int in_use;
	char blk_name[16];
	char mount_path[EXT2_MAX_PATH];
	uint32_t block_size;
	uint32_t inodes_per_group;
	uint16_t inode_size;
	uint32_t first_data_block;
	uint32_t blocks_per_group;
	uint32_t inode_table_block;
};

static struct ext2_vol g_vols[EXT2_MAX_MOUNTS];

static int ext2_read_block(struct ext2_vol *v, uint32_t block, void *buf)
{
	uint32_t sectors = v->block_size / 512;
	uint32_t lba = block * sectors;

	if (ir0_block_read_by_name(v->blk_name, lba, sectors, buf))
		return -EIO;
	return 0;
}

static int ext2_read_inode(struct ext2_vol *v, uint32_t ino, struct ext2_inode *out)
{
	uint8_t *buf;
	uint32_t group, index, block, offset;
	int ret;

	if (ino == 0)
		return -EINVAL;
	group = (ino - 1) / v->inodes_per_group;
	index = (ino - 1) % v->inodes_per_group;
	(void)group; /* single-group smoke images */
	block = v->inode_table_block + (index * v->inode_size) / v->block_size;
	offset = (index * v->inode_size) % v->block_size;

	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	ret = ext2_read_block(v, block, buf);
	if (ret == 0)
		memcpy(out, buf + offset, sizeof(*out));
	kfree(buf);
	return ret;
}

static struct ext2_vol *ext2_find(const char *path, char *rel, size_t rel_sz, int *is_root)
{
	size_t best = 0;
	struct ext2_vol *best_vol = NULL;

	if (!path || !rel)
		return NULL;
	for (int i = 0; i < EXT2_MAX_MOUNTS; i++)
	{
		struct ext2_vol *v = &g_vols[i];
		size_t mlen;

		if (!v->in_use)
			continue;
		mlen = strlen(v->mount_path);
		if (strncmp(path, v->mount_path, mlen) != 0)
			continue;
		if (path[mlen] != '\0' && path[mlen] != '/')
			continue;
		if (mlen >= best)
		{
			best = mlen;
			best_vol = v;
		}
	}
	if (!best_vol)
		return NULL;
	if (path[best] == '\0' || (path[best] == '/' && path[best + 1] == '\0'))
	{
		*is_root = 1;
		rel[0] = '\0';
	}
	else
	{
		*is_root = 0;
		strncpy(rel, path + best + (path[best] == '/' ? 1 : 0), rel_sz - 1);
		rel[rel_sz - 1] = '\0';
	}
	return best_vol;
}

static int ext2_lookup(struct ext2_vol *v, uint32_t dir_ino, const char *name,
		       uint32_t *out_ino)
{
	struct ext2_inode ino;
	uint8_t *buf;
	uint32_t b;
	int ret;

	ret = ext2_read_inode(v, dir_ino, &ino);
	if (ret != 0)
		return ret;
	if ((ino.i_mode & 0xF000) != 0x4000)
		return -ENOTDIR;

	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;

	for (b = 0; b < 12; b++)
	{
		uint32_t off = 0;

		if (ino.i_block[b] == 0)
			break;
		ret = ext2_read_block(v, ino.i_block[b], buf);
		if (ret != 0)
		{
			kfree(buf);
			return ret;
		}
		while (off + 8 <= v->block_size)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

			if (de->rec_len < 8 || de->rec_len > v->block_size - off)
			{
				kfree(buf);
				return -EIO;
			}
			if (de->name_len > de->rec_len - 8)
			{
				kfree(buf);
				return -EIO;
			}
			if (de->inode != 0 && de->name_len == strlen(name) &&
			    memcmp((char *)de + 8, name, de->name_len) == 0)
			{
				*out_ino = de->inode;
				kfree(buf);
				return 0;
			}
			off += de->rec_len;
		}
	}
	kfree(buf);
	return -ENOENT;
}

static int ext2_resolve(struct ext2_vol *v, const char *rel, uint32_t *out_ino)
{
	if (!rel || rel[0] == '\0')
	{
		*out_ino = EXT2_ROOT_INO;
		return 0;
	}
	/* MVP: single path component under root (no nested dirs). */
	if (strchr(rel, '/'))
		return -ENOENT;
	return ext2_lookup(v, EXT2_ROOT_INO, rel, out_ino);
}

static int ext2_stat_path(const char *path, stat_t *st)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode ino;
	uint32_t inum;
	int ret;

	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &inum);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, inum, &ino);
	if (ret != 0)
		return ret;
	memset(st, 0, sizeof(*st));
	st->st_mode = ino.i_mode;
	st->st_uid = ino.i_uid;
	st->st_gid = ino.i_gid;
	st->st_size = ino.i_size;
	st->st_nlink = ino.i_links_count;
	st->st_ino = inum;
	return 0;
}

static int ext2_read_path(const char *path, void *buf, size_t count, size_t *nread,
			  off_t offset)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode ino;
	uint32_t inum;
	uint8_t *block_buf;
	size_t done = 0;
	int ret;

	if (offset < 0)
		return -EINVAL;
	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &inum);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, inum, &ino);
	if (ret != 0)
		return ret;
	if ((ino.i_mode & 0xF000) == 0x4000)
		return -EISDIR;
	if ((uint64_t)offset >= ino.i_size)
	{
		*nread = 0;
		return 0;
	}
	if ((uint64_t)offset + count > ino.i_size)
		count = (size_t)(ino.i_size - (uint32_t)offset);

	block_buf = kmalloc(v->block_size);
	if (!block_buf)
		return -ENOMEM;

	while (done < count)
	{
		uint32_t file_off = (uint32_t)offset + (uint32_t)done;
		uint32_t blk_index = file_off / v->block_size;
		uint32_t blk_off = file_off % v->block_size;
		uint32_t phys;
		size_t chunk;

		if (blk_index >= 12)
		{
			ret = -EFBIG; /* no indirect blocks in MVP */
			break;
		}
		phys = ino.i_block[blk_index];
		if (phys == 0)
			break;
		ret = ext2_read_block(v, phys, block_buf);
		if (ret != 0)
			break;
		chunk = v->block_size - blk_off;
		if (chunk > count - done)
			chunk = count - done;
		memcpy((uint8_t *)buf + done, block_buf + blk_off, chunk);
		done += chunk;
	}
	kfree(block_buf);
	if (ret != 0 && done == 0)
		return ret;
	*nread = done;
	return 0;
}

static int ext2_readdir_path(const char *path, struct vfs_dirent *entries, int max)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode ino;
	uint32_t inum;
	uint8_t *buf;
	int count = 0;
	int ret;
	uint32_t b;

	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &inum);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, inum, &ino);
	if (ret != 0)
		return ret;
	if ((ino.i_mode & 0xF000) != 0x4000)
		return -ENOTDIR;

	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;

	for (b = 0; b < 12 && count < max; b++)
	{
		uint32_t off = 0;

		if (ino.i_block[b] == 0)
			break;
		ret = ext2_read_block(v, ino.i_block[b], buf);
		if (ret != 0)
		{
			kfree(buf);
			return ret;
		}
		while (off + 8 <= v->block_size && count < max)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

			if (de->rec_len < 8)
				break;
			if (de->inode != 0 && de->name_len > 0)
			{
				size_t n = de->name_len;

				if (n >= VFS_PATH_MAX)
					n = VFS_PATH_MAX - 1;
				memcpy(entries[count].name, (char *)de + 8, n);
				entries[count].name[n] = '\0';
				entries[count].type =
					(de->file_type == 2) ? DT_DIR : DT_REG;
				count++;
			}
			off += de->rec_len;
		}
	}
	kfree(buf);
	return count;
}

static int ext2_ro_mkdir(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return -EROFS;
}

static int ext2_ro_create(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return -EROFS;
}

static int ext2_ro_unlink(const char *path)
{
	(void)path;
	return -EROFS;
}

static int ext2_ro_rmdir(const char *path)
{
	(void)path;
	return -EROFS;
}

static int ext2_ro_chown(const char *path, uid_t owner, gid_t group)
{
	(void)path;
	(void)owner;
	(void)group;
	return -EROFS;
}

static int ext2_ro_chmod(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return -EROFS;
}

static int ext2_ro_write(const char *path, const void *buf, size_t count,
			 size_t *bytes_written, off_t offset)
{
	(void)path;
	(void)buf;
	(void)count;
	(void)bytes_written;
	(void)offset;
	return -EROFS;
}

static int ext2_ro_truncate(const char *path, size_t length)
{
	(void)path;
	(void)length;
	return -EROFS;
}

static struct vfs_ops ext2_ops = {
	.stat = ext2_stat_path,
	.mkdir = ext2_ro_mkdir,
	.create = ext2_ro_create,
	.unlink = ext2_ro_unlink,
	.rmdir = ext2_ro_rmdir,
	.link = NULL,
	.chown = ext2_ro_chown,
	.chmod = ext2_ro_chmod,
	.readdir = ext2_readdir_path,
	.read = ext2_read_path,
	.write = ext2_ro_write,
	.truncate = ext2_ro_truncate,
};

static int ext2_parse_dev(const char *dev, char *blk, size_t blk_sz)
{
	if (!dev || strlen(dev) < 8 || strncmp(dev, "/dev/", 5) != 0)
		return -EINVAL;
	strncpy(blk, dev + 5, blk_sz - 1);
	blk[blk_sz - 1] = '\0';
	return 0;
}

int ext2_disk_mount(const char *dev, const char *mount_dir)
{
	struct ext2_vol *slot = NULL;
	char blk[16];
	uint8_t sector[1024];
	struct ext2_super *sb;
	struct ext2_bgd *bgd;
	int ret;

	if (!dev || !mount_dir)
		return -EINVAL;
	for (int i = 0; i < EXT2_MAX_MOUNTS; i++)
	{
		if (!g_vols[i].in_use)
		{
			slot = &g_vols[i];
			break;
		}
	}
	if (!slot)
		return -ENOSPC;

	ret = ext2_parse_dev(dev, blk, sizeof(blk));
	if (ret != 0)
		return ret;
	if (!ir0_block_name_is_present(blk))
		return -ENXIO;

	/* Superblock at byte 1024 → LBA 2 for 512-byte sectors. */
	if (ir0_block_read_by_name(blk, 2, 2, sector))
		return -EIO;
	sb = (struct ext2_super *)sector;
	if (sb->s_magic != EXT2_MAGIC)
		return -EINVAL;

	memset(slot, 0, sizeof(*slot));
	strncpy(slot->blk_name, blk, sizeof(slot->blk_name) - 1);
	slot->block_size = 1024U << sb->s_log_block_size;
	slot->inodes_per_group = sb->s_inodes_per_group;
	slot->inode_size = sb->s_inode_size ? sb->s_inode_size : 128;
	slot->first_data_block = sb->s_first_data_block;
	slot->blocks_per_group = sb->s_blocks_per_group;

	/* Group descriptor follows superblock block. */
	{
		uint32_t gd_block = slot->first_data_block + 1;
		uint8_t *gbuf = kmalloc(slot->block_size);

		if (!gbuf)
			return -ENOMEM;
		ret = 0;
		{
			uint32_t sectors = slot->block_size / 512;
			uint32_t lba = gd_block * sectors;

			if (ir0_block_read_by_name(blk, lba, sectors, gbuf))
				ret = -EIO;
		}
		if (ret != 0)
		{
			kfree(gbuf);
			return ret;
		}
		bgd = (struct ext2_bgd *)gbuf;
		slot->inode_table_block = bgd->bg_inode_table;
		kfree(gbuf);
	}

	strncpy(slot->mount_path, mount_dir, sizeof(slot->mount_path) - 1);
	slot->in_use = 1;
	klog_print("EXT2_MOUNT_OK\n");
	return 0;
}

static int ext2_mount_cb(const char *dev, const char *dir)
{
	return ext2_disk_mount(dev, dir);
}

static int ext2_umount_cb(const char *dir)
{
	for (int i = 0; i < EXT2_MAX_MOUNTS; i++)
	{
		if (g_vols[i].in_use && strcmp(g_vols[i].mount_path, dir) == 0)
		{
			g_vols[i].in_use = 0;
			return 0;
		}
	}
	return -EINVAL;
}

static struct vfs_fstype ext2_fstype = {
	.name = "ext2",
	.ops = &ext2_ops,
	.mount = ext2_mount_cb,
	.umount = ext2_umount_cb,
	.next = NULL,
};

int ext2_fs_register(void)
{
	return vfs_register_fs(&ext2_fstype);
}
