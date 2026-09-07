/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fat16_disk.c
 * Description: FAT16 on ir0 block_dev — read/write, create, unlink, mkdir, truncate.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "fat16_disk.h"
#include "vfs.h"
#include <ir0/blockdev.h>
#include <ir0/partition.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <string.h>

#define FAT16_MAX_MOUNTS      4
#define FAT16_MAX_PATH        256
#define FAT16_MAX_NAME          12
#define FAT16_ROOT_MAX        512
#define FAT16_CLUSTER_EOF   0xFFF8U
#define FAT16_CLUSTER_FREE  0x0000U
#define FAT16_CLUSTER_BAD   0xFFF7U

struct fat16_vol
{
	int in_use;
	char blk_name[16];
	char mount_path[FAT16_MAX_PATH];
	uint32_t base_lba;
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t num_fats;
	uint16_t root_entry_count;
	uint16_t fat_sectors;
	uint32_t root_dir_sectors;
	uint32_t first_data_sector;
	uint32_t total_sectors;
	uint32_t data_clusters;
};

static struct fat16_vol g_vols[FAT16_MAX_MOUNTS];

struct fat16_dirent
{
	uint8_t name[11];
	uint8_t attr;
	uint8_t reserved;
	uint8_t ctime_tenth;
	uint16_t ctime;
	uint16_t cdate;
	uint16_t adate;
	uint16_t cluster_hi;
	uint16_t mtime;
	uint16_t mdate;
	uint16_t cluster_lo;
	uint32_t size;
} __attribute__((packed));

#define FAT_ATTR_RO    0x01U
#define FAT_ATTR_DIR   0x10U
#define FAT_ATTR_LFN   0x0FU
#define FAT_ATTR_ARCH  0x20U

/* Location of a directory entry on disk (for updates). */
struct fat16_ent_ref
{
	int is_root;
	uint16_t dir_cluster; /* start cluster of containing dir if !is_root */
	uint32_t index;       /* entry index within that directory */
	struct fat16_dirent de;
};

static struct fat16_vol *fat16_find_mount(const char *vfs_path, char *rel, size_t rel_sz,
					  int *is_root)
{
	size_t best = 0;
	struct fat16_vol *best_vol = NULL;

	if (!vfs_path || !rel || rel_sz == 0)
		return NULL;

	for (int i = 0; i < FAT16_MAX_MOUNTS; i++)
	{
		struct fat16_vol *v = &g_vols[i];
		size_t mlen;

		if (!v->in_use)
			continue;
		mlen = strlen(v->mount_path);
		if (strncmp(vfs_path, v->mount_path, mlen) != 0)
			continue;
		if (vfs_path[mlen] != '\0' && vfs_path[mlen] != '/')
			continue;
		if (mlen >= best)
		{
			best = mlen;
			best_vol = v;
		}
	}

	if (!best_vol)
		return NULL;

	if (vfs_path[best] == '\0')
	{
		rel[0] = '\0';
		*is_root = 1;
		return best_vol;
	}

	if (vfs_path[best] == '/')
		best++;

	{
		size_t rlen = strlen(vfs_path + best);

		if (rlen >= rel_sz)
			return NULL;
		memcpy(rel, vfs_path + best, rlen + 1);
	}
	*is_root = 0;
	return best_vol;
}

static int fat16_read_sectors(struct fat16_vol *v, uint32_t rel_lba, uint8_t count,
			      void *buf)
{
	if (!v || !buf || count == 0)
		return -EINVAL;
	if (ir0_block_read_by_name(v->blk_name, v->base_lba + rel_lba, count, buf))
		return -EIO;
	return 0;
}

static int fat16_write_sectors(struct fat16_vol *v, uint32_t rel_lba, uint8_t count,
			       const void *buf)
{
	if (!v || !buf || count == 0)
		return -EINVAL;
	if (ir0_block_write_by_name(v->blk_name, v->base_lba + rel_lba, count, buf))
		return -EIO;
	return 0;
}

static int fat16_read_sector(struct fat16_vol *v, uint32_t rel_lba, void *buf)
{
	return fat16_read_sectors(v, rel_lba, 1, buf);
}

static int fat16_write_sector(struct fat16_vol *v, uint32_t rel_lba, const void *buf)
{
	return fat16_write_sectors(v, rel_lba, 1, buf);
}

int fat16_disk_probe_bpb(const uint8_t sector[512], uint16_t *bytes_per_sector_out)
{
	uint16_t bps;
	uint16_t fat_sectors;
	uint16_t root_cnt;
	uint32_t root_secs;
	uint32_t data_secs;
	uint32_t total_sec;

	if (!sector)
		return -EINVAL;

	if (sector[510] != 0x55 || sector[511] != 0xAA)
		return -EINVAL;

	bps = (uint16_t)sector[0x0B] | ((uint16_t)sector[0x0C] << 8);
	if (bps != 512)
		return -ENOTSUPP;

	if (memcmp(&sector[0x36], "FAT16   ", 8) != 0 &&
	    memcmp(&sector[0x36], "FAT     ", 8) != 0)
		return -ENOTSUPP;

	fat_sectors = (uint16_t)sector[0x16] | ((uint16_t)sector[0x17] << 8);
	if (fat_sectors == 0)
	{
		fat_sectors = (uint16_t)sector[0x24] | ((uint16_t)sector[0x25] << 8);
		if (fat_sectors == 0)
			return -EINVAL;
	}

	root_cnt = (uint16_t)sector[0x11] | ((uint16_t)sector[0x12] << 8);
	root_secs = ((uint32_t)root_cnt * 32U + (uint32_t)bps - 1U) / (uint32_t)bps;

	total_sec = (uint16_t)sector[0x13] | ((uint16_t)sector[0x14] << 8);
	if (total_sec == 0)
		total_sec = (uint32_t)sector[0x20] | ((uint32_t)sector[0x21] << 8) |
			    ((uint32_t)sector[0x22] << 16) | ((uint32_t)sector[0x23] << 24);

	data_secs = total_sec - (uint32_t)sector[0x0E] -
		    (uint32_t)sector[0x10] * (uint32_t)fat_sectors - root_secs;
	if (data_secs == 0)
		return -EINVAL;

	if (bytes_per_sector_out)
		*bytes_per_sector_out = bps;
	return 0;
}

static int fat16_load_bpb(struct fat16_vol *v, const uint8_t *sector)
{
	uint16_t total16;
	uint32_t total32;
	uint32_t root_secs;

	if (fat16_disk_probe_bpb(sector, &v->bytes_per_sector) != 0)
		return -EINVAL;

	v->sectors_per_cluster = sector[0x0D];
	if (v->sectors_per_cluster == 0 ||
	    (v->sectors_per_cluster & (v->sectors_per_cluster - 1)) != 0)
		return -EINVAL;

	v->reserved_sectors = (uint16_t)sector[0x0E] | ((uint16_t)sector[0x0F] << 8);
	v->num_fats = sector[0x10];
	if (v->num_fats == 0)
		return -EINVAL;

	v->root_entry_count = (uint16_t)sector[0x11] | ((uint16_t)sector[0x12] << 8);
	v->fat_sectors = (uint16_t)sector[0x16] | ((uint16_t)sector[0x17] << 8);
	if (v->fat_sectors == 0)
		v->fat_sectors = (uint16_t)sector[0x24] | ((uint16_t)sector[0x25] << 8);

	total16 = (uint16_t)sector[0x13] | ((uint16_t)sector[0x14] << 8);
	total32 = (uint32_t)sector[0x20] | ((uint32_t)sector[0x21] << 8) |
		  ((uint32_t)sector[0x22] << 16) | ((uint32_t)sector[0x23] << 24);
	v->total_sectors = total16 ? total16 : total32;

	root_secs = ((uint32_t)v->root_entry_count * 32U + 511U) / 512U;
	v->root_dir_sectors = root_secs;
	v->first_data_sector = (uint32_t)v->reserved_sectors +
			       (uint32_t)v->num_fats * (uint32_t)v->fat_sectors +
			       root_secs;

	{
		uint32_t data_secs = v->total_sectors - v->first_data_sector;

		v->data_clusters = data_secs / (uint32_t)v->sectors_per_cluster;
	}
	return 0;
}

static int fat16_parse_dev(const char *dev, char *blk_out, size_t blk_sz,
			   uint32_t *part_lba_out, int *use_whole_disk)
{
	const char *name;
	uint8_t disk_id;
	char letter;

	if (!dev || !blk_out || !part_lba_out || !use_whole_disk)
		return -EINVAL;

	(void)blk_sz;

	name = dev;
	if (strncmp(dev, "/dev/", 5) == 0)
		name = dev + 5;

	if (strlen(name) < 3 || name[0] != 'h' || name[1] != 'd')
		return -EINVAL;

	letter = name[2];
	if (letter < 'a' || letter > 'd')
		return -EINVAL;
	disk_id = (uint8_t)(letter - 'a');

	if (name[3] == '\0')
	{
		*use_whole_disk = 1;
		blk_out[0] = 'h';
		blk_out[1] = 'd';
		blk_out[2] = letter;
		blk_out[3] = '\0';
		*part_lba_out = 0;
		return 0;
	}

	if (name[3] >= '1' && name[3] <= '9' && name[4] == '\0')
	{
		partition_info_t pi;
		uint8_t part_idx = (uint8_t)(name[3] - '1');

		if (get_partition_info(disk_id, part_idx, &pi) != 0)
			return -ENODEV;
		*use_whole_disk = 0;
		blk_out[0] = 'h';
		blk_out[1] = 'd';
		blk_out[2] = letter;
		blk_out[3] = '\0';
		*part_lba_out = (uint32_t)pi.start_lba;
		(void)disk_id;
		return 0;
	}

	return -EINVAL;
}

static int fat16_find_fat16_partition(uint8_t disk_id, uint32_t *lba_out)
{
	uint8_t mbr[512];
	int i;

	if (ir0_block_read_by_name(ir0_block_legacy_name(disk_id), 0, 1, mbr))
		return -EIO;
	if (mbr[510] != 0x55 || mbr[511] != 0xAA)
		return -ENOENT;

	for (i = 0; i < 4; i++)
	{
		const uint8_t *ent = &mbr[0x1BE + i * 16];
		uint8_t sys = ent[4];
		uint32_t start = (uint32_t)ent[8] | ((uint32_t)ent[9] << 8) |
				 ((uint32_t)ent[10] << 16) | ((uint32_t)ent[11] << 24);

		if (sys == 0x04 || sys == 0x06 || sys == 0x0E)
		{
			*lba_out = start;
			return 0;
		}
	}
	return -ENOENT;
}

static void fat16_format_dirent_name(const struct fat16_dirent *de, char *out, size_t out_sz)
{
	char base[9];
	char ext[4];
	int bi = 0;
	int ei = 0;
	int i;

	for (i = 0; i < 8; i++)
	{
		if (de->name[i] == ' ')
			break;
		base[bi++] = (char)de->name[i];
	}
	base[bi] = '\0';

	for (i = 8; i < 11; i++)
	{
		if (de->name[i] == ' ')
			break;
		ext[ei++] = (char)de->name[i];
	}
	ext[ei] = '\0';

	if (ext[0])
	{
		size_t bl = strlen(base);
		size_t el = strlen(ext);

		if (bl + 1 + el >= out_sz)
			return;
		memcpy(out, base, bl);
		out[bl] = '.';
		memcpy(out + bl + 1, ext, el + 1);
	}
	else
	{
		strncpy(out, base, out_sz - 1);
		out[out_sz - 1] = '\0';
	}
}

static int fat16_name_match(const char *want, const struct fat16_dirent *de)
{
	char formatted[FAT16_MAX_NAME];
	char a[FAT16_MAX_NAME];
	char b[FAT16_MAX_NAME];
	size_t i;

	if (de->name[0] == 0x00 || de->name[0] == (uint8_t)0xE5)
		return 0;
	if (de->attr == FAT_ATTR_LFN)
		return 0;
	fat16_format_dirent_name(de, formatted, sizeof(formatted));
	for (i = 0; i < FAT16_MAX_NAME; i++)
	{
		char c = want[i];

		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		a[i] = c;
		if (want[i] == '\0')
			break;
	}
	a[FAT16_MAX_NAME - 1] = '\0';
	for (i = 0; i < FAT16_MAX_NAME; i++)
	{
		char c = formatted[i];

		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		b[i] = c;
		if (formatted[i] == '\0')
			break;
	}
	b[FAT16_MAX_NAME - 1] = '\0';
	return strcmp(a, b) == 0;
}

static int fat16_encode_83(const char *name, uint8_t out[11])
{
	const char *dot;
	size_t base_len;
	size_t ext_len;
	size_t i;

	if (!name || !out || name[0] == '\0' || strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0)
		return -EINVAL;

	memset(out, ' ', 11);
	dot = strchr(name, '.');
	if (dot)
	{
		base_len = (size_t)(dot - name);
		ext_len = strlen(dot + 1);
		if (base_len == 0 || base_len > 8 || ext_len > 3)
			return -EINVAL;
		for (i = 0; i < base_len; i++)
		{
			char c = name[i];

			if (c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			out[i] = (uint8_t)c;
		}
		for (i = 0; i < ext_len; i++)
		{
			char c = dot[1 + i];

			if (c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			out[8 + i] = (uint8_t)c;
		}
	}
	else
	{
		base_len = strlen(name);
		if (base_len == 0 || base_len > 8)
			return -EINVAL;
		for (i = 0; i < base_len; i++)
		{
			char c = name[i];

			if (c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			out[i] = (uint8_t)c;
		}
	}
	return 0;
}

static uint32_t fat16_cluster_lba(struct fat16_vol *v, uint16_t cluster)
{
	return v->first_data_sector +
	       (uint32_t)(cluster - 2) * (uint32_t)v->sectors_per_cluster;
}

static uint16_t fat16_get_fat(struct fat16_vol *v, uint16_t cluster)
{
	uint32_t fat_offset = (uint32_t)cluster * 2U;
	uint32_t fat_sector = v->reserved_sectors + fat_offset / 512U;
	uint32_t ent_offset = fat_offset % 512U;
	uint8_t sec[512];

	if (fat16_read_sector(v, fat_sector, sec) != 0)
		return 0;
	return (uint16_t)sec[ent_offset] | ((uint16_t)sec[ent_offset + 1] << 8);
}

static int fat16_set_fat(struct fat16_vol *v, uint16_t cluster, uint16_t value)
{
	uint32_t fat_offset = (uint32_t)cluster * 2U;
	uint32_t ent_offset = fat_offset % 512U;
	uint8_t sec[512];
	uint8_t fi;
	int ret;

	for (fi = 0; fi < v->num_fats; fi++)
	{
		uint32_t fat_sector = v->reserved_sectors +
				      (uint32_t)fi * (uint32_t)v->fat_sectors +
				      fat_offset / 512U;

		ret = fat16_read_sector(v, fat_sector, sec);
		if (ret != 0)
			return ret;
		sec[ent_offset] = (uint8_t)(value & 0xFF);
		sec[ent_offset + 1] = (uint8_t)((value >> 8) & 0xFF);
		ret = fat16_write_sector(v, fat_sector, sec);
		if (ret != 0)
			return ret;
	}
	return 0;
}

static uint16_t fat16_next_cluster(struct fat16_vol *v, uint16_t cluster)
{
	return fat16_get_fat(v, cluster);
}

static int fat16_alloc_cluster(struct fat16_vol *v, uint16_t *out)
{
	uint16_t c;

	if (!out)
		return -EINVAL;
	for (c = 2; c < 2 + v->data_clusters; c++)
	{
		uint16_t val = fat16_get_fat(v, c);

		if (val == FAT16_CLUSTER_FREE)
		{
			int ret = fat16_set_fat(v, c, FAT16_CLUSTER_EOF);

			if (ret != 0)
				return ret;
			*out = c;
			return 0;
		}
	}
	return -ENOSPC;
}

static int fat16_free_chain(struct fat16_vol *v, uint16_t start)
{
	uint16_t c = start;

	while (c >= 2 && c < FAT16_CLUSTER_EOF && c != FAT16_CLUSTER_BAD)
	{
		uint16_t next = fat16_get_fat(v, c);
		int ret = fat16_set_fat(v, c, FAT16_CLUSTER_FREE);

		if (ret != 0)
			return ret;
		c = next;
	}
	return 0;
}

static int fat16_zero_cluster(struct fat16_vol *v, uint16_t cluster)
{
	uint32_t bytes = (uint32_t)v->sectors_per_cluster * 512U;
	uint8_t *buf;
	int ret;

	buf = (uint8_t *)kmalloc_try(bytes);
	if (!buf)
		return -ENOMEM;
	memset(buf, 0, bytes);
	ret = fat16_write_sectors(v, fat16_cluster_lba(v, cluster),
				  v->sectors_per_cluster, buf);
	kfree(buf);
	return ret;
}

static int fat16_read_root_raw(struct fat16_vol *v, uint8_t **buf_out, uint32_t *bytes_out)
{
	uint32_t root_bytes = v->root_dir_sectors * 512U;
	uint8_t *buf;
	int ret;

	buf = (uint8_t *)kmalloc_try(root_bytes);
	if (!buf)
		return -ENOMEM;
	ret = fat16_read_sectors(v,
				 v->reserved_sectors +
					 (uint32_t)v->num_fats * v->fat_sectors,
				 (uint8_t)v->root_dir_sectors, buf);
	if (ret != 0)
	{
		kfree(buf);
		return ret;
	}
	*buf_out = buf;
	*bytes_out = root_bytes;
	return 0;
}

static int fat16_write_root_raw(struct fat16_vol *v, const uint8_t *buf)
{
	return fat16_write_sectors(v,
				   v->reserved_sectors +
					   (uint32_t)v->num_fats * v->fat_sectors,
				   (uint8_t)v->root_dir_sectors, buf);
}

static int fat16_load_dir_buf(struct fat16_vol *v, int is_root, uint16_t dir_cluster,
			      uint8_t **buf_out, uint32_t *bytes_out)
{
	if (is_root)
		return fat16_read_root_raw(v, buf_out, bytes_out);

	{
		uint32_t cluster_bytes = (uint32_t)v->sectors_per_cluster * 512U;
		uint16_t c = dir_cluster;
		uint32_t total = 0;
		uint32_t nclusters = 0;
		uint8_t *buf;
		uint32_t off = 0;

		while (c >= 2 && c < FAT16_CLUSTER_EOF && nclusters < 64)
		{
			nclusters++;
			c = fat16_next_cluster(v, c);
		}
		if (nclusters == 0)
			return -ENOENT;
		total = nclusters * cluster_bytes;
		buf = (uint8_t *)kmalloc_try(total);
		if (!buf)
			return -ENOMEM;
		c = dir_cluster;
		while (c >= 2 && c < FAT16_CLUSTER_EOF && off < total)
		{
			int ret = fat16_read_sectors(v, fat16_cluster_lba(v, c),
						     v->sectors_per_cluster, buf + off);

			if (ret != 0)
			{
				kfree(buf);
				return ret;
			}
			off += cluster_bytes;
			c = fat16_next_cluster(v, c);
		}
		*buf_out = buf;
		*bytes_out = total;
		return 0;
	}
}

static int fat16_store_dir_buf(struct fat16_vol *v, int is_root, uint16_t dir_cluster,
			       const uint8_t *buf, uint32_t bytes)
{
	if (is_root)
		return fat16_write_root_raw(v, buf);

	{
		uint32_t cluster_bytes = (uint32_t)v->sectors_per_cluster * 512U;
		uint16_t c = dir_cluster;
		uint32_t off = 0;

		while (c >= 2 && c < FAT16_CLUSTER_EOF && off < bytes)
		{
			int ret = fat16_write_sectors(v, fat16_cluster_lba(v, c),
						      v->sectors_per_cluster, buf + off);

			if (ret != 0)
				return ret;
			off += cluster_bytes;
			c = fat16_next_cluster(v, c);
		}
		return 0;
	}
}

static int fat16_write_dirent_at(struct fat16_vol *v, const struct fat16_ent_ref *ref)
{
	uint8_t *buf = NULL;
	uint32_t bytes = 0;
	int ret;

	ret = fat16_load_dir_buf(v, ref->is_root, ref->dir_cluster, &buf, &bytes);
	if (ret != 0)
		return ret;
	if ((ref->index + 1U) * 32U > bytes)
	{
		kfree(buf);
		return -ENOSPC;
	}
	memcpy(buf + ref->index * 32U, &ref->de, 32);
	ret = fat16_store_dir_buf(v, ref->is_root, ref->dir_cluster, buf, bytes);
	kfree(buf);
	return ret;
}

static int fat16_find_in_dir(struct fat16_vol *v, int is_root, uint16_t dir_cluster,
			     const char *name, struct fat16_ent_ref *out)
{
	uint8_t *buf = NULL;
	uint32_t bytes = 0;
	uint32_t i;
	uint32_t nent;
	int ret;

	ret = fat16_load_dir_buf(v, is_root, dir_cluster, &buf, &bytes);
	if (ret != 0)
		return ret;
	nent = bytes / 32U;
	for (i = 0; i < nent; i++)
	{
		struct fat16_dirent *de = (struct fat16_dirent *)(buf + i * 32U);

		if (de->name[0] == 0x00)
			break;
		if (fat16_name_match(name, de))
		{
			out->is_root = is_root;
			out->dir_cluster = dir_cluster;
			out->index = i;
			out->de = *de;
			kfree(buf);
			return 0;
		}
	}
	kfree(buf);
	return -ENOENT;
}

static int fat16_find_free_slot(struct fat16_vol *v, int is_root, uint16_t dir_cluster,
				struct fat16_ent_ref *out)
{
	uint8_t *buf = NULL;
	uint32_t bytes = 0;
	uint32_t i;
	uint32_t nent;
	int ret;

	ret = fat16_load_dir_buf(v, is_root, dir_cluster, &buf, &bytes);
	if (ret != 0)
		return ret;
	nent = bytes / 32U;
	for (i = 0; i < nent; i++)
	{
		struct fat16_dirent *de = (struct fat16_dirent *)(buf + i * 32U);

		if (de->name[0] == 0x00 || de->name[0] == (uint8_t)0xE5)
		{
			out->is_root = is_root;
			out->dir_cluster = dir_cluster;
			out->index = i;
			memset(&out->de, 0, sizeof(out->de));
			kfree(buf);
			return 0;
		}
	}
	kfree(buf);
	return -ENOSPC;
}

static int fat16_dir_is_empty(struct fat16_vol *v, uint16_t dir_cluster)
{
	uint8_t *buf = NULL;
	uint32_t bytes = 0;
	uint32_t i;
	uint32_t nent;
	int ret;

	ret = fat16_load_dir_buf(v, 0, dir_cluster, &buf, &bytes);
	if (ret != 0)
		return ret;
	nent = bytes / 32U;
	for (i = 0; i < nent; i++)
	{
		struct fat16_dirent *de = (struct fat16_dirent *)(buf + i * 32U);
		char name[FAT16_MAX_NAME];

		if (de->name[0] == 0x00)
			break;
		if (de->name[0] == (uint8_t)0xE5 || de->attr == FAT_ATTR_LFN)
			continue;
		fat16_format_dirent_name(de, name, sizeof(name));
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;
		kfree(buf);
		return 0; /* not empty */
	}
	kfree(buf);
	return 1;
}

static char *fat16_strtok(char *s, char **save)
{
	char *start;

	if (s)
		*save = s;
	if (!*save)
		return NULL;
	while (**save == '/')
		(*save)++;
	if (**save == '\0')
		return NULL;
	start = *save;
	while (**save && **save != '/')
		(*save)++;
	if (**save == '/')
	{
		**save = '\0';
		(*save)++;
	}
	else
		*save = NULL;
	return start;
}

/*
 * Resolve relative path under mount. If want_parent, fill leaf + parent dir
 * without requiring the leaf to exist (for create/mkdir).
 */
static int fat16_resolve_path(struct fat16_vol *v, const char *rel, int want_parent,
			      char *leaf_out, size_t leaf_sz, struct fat16_ent_ref *out,
			      int *parent_is_root, uint16_t *parent_cluster)
{
	char path[FAT16_MAX_PATH];
	char *save = NULL;
	char *tok;
	int is_root = 1;
	uint16_t cur_cluster = 0;
	struct fat16_ent_ref cur;
	int have = 0;
	char *tokens[32];
	int ntok = 0;
	int i;

	if (!rel || rel[0] == '\0')
		return -EISDIR;
	strncpy(path, rel, sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	tok = fat16_strtok(path, &save);
	while (tok != NULL && ntok < 32)
	{
		tokens[ntok++] = tok;
		tok = fat16_strtok(NULL, &save);
	}

	if (ntok == 0)
		return -EISDIR;

	for (i = 0; i < ntok; i++)
	{
		int last = (i == ntok - 1);

		if (want_parent && last)
		{
			if (leaf_out && leaf_sz)
			{
				strncpy(leaf_out, tokens[i], leaf_sz - 1);
				leaf_out[leaf_sz - 1] = '\0';
			}
			if (parent_is_root)
				*parent_is_root = is_root;
			if (parent_cluster)
				*parent_cluster = cur_cluster;
			return 0;
		}

		if (fat16_find_in_dir(v, is_root, cur_cluster, tokens[i], &cur) != 0)
			return -ENOENT;
		have = 1;
		if (cur.de.attr & FAT_ATTR_DIR)
		{
			is_root = 0;
			cur_cluster = cur.de.cluster_lo;
		}
		else if (!last)
			return -ENOTDIR;
	}

	if (!have)
		return -ENOENT;
	if (out)
		*out = cur;
	return 0;
}

static int fat16_read_file(struct fat16_vol *v, const struct fat16_dirent *de,
			   void *buf, size_t count, size_t *nread, off_t offset)
{
	uint16_t cluster;
	uint32_t cluster_bytes;
	uint8_t *cluster_buf;
	size_t done = 0;
	size_t file_size = de->size;
	size_t skip = (size_t)offset;
	int ret = 0;

	if (!buf || !nread)
		return -EINVAL;
	*nread = 0;
	if (offset < 0)
		return -EINVAL;
	if ((size_t)offset >= file_size)
		return 0;
	if (count > file_size - (size_t)offset)
		count = file_size - (size_t)offset;

	cluster = de->cluster_lo;
	cluster_bytes = (uint32_t)v->sectors_per_cluster * 512U;
	cluster_buf = (uint8_t *)kmalloc_try(cluster_bytes);
	if (!cluster_buf)
		return -ENOMEM;

	while (cluster >= 2 && cluster < FAT16_CLUSTER_EOF && done < count)
	{
		uint32_t rel = fat16_cluster_lba(v, cluster);
		size_t coff;

		ret = fat16_read_sectors(v, rel, v->sectors_per_cluster, cluster_buf);
		if (ret != 0)
			break;

		for (coff = 0; coff < cluster_bytes && done < count; coff++)
		{
			if (skip > 0)
			{
				skip--;
				continue;
			}
			((uint8_t *)buf)[done++] = cluster_buf[coff];
		}
		cluster = fat16_next_cluster(v, cluster);
	}

	kfree(cluster_buf);
	if (ret != 0)
		return ret;
	*nread = done;
	return 0;
}

static int fat16_ensure_size(struct fat16_vol *v, struct fat16_ent_ref *ref, size_t need)
{
	uint32_t cluster_bytes = (uint32_t)v->sectors_per_cluster * 512U;
	uint16_t cluster = ref->de.cluster_lo;
	uint16_t last = 0;
	size_t have = 0;
	int ret;

	if (need == 0)
	{
		if (ref->de.cluster_lo >= 2)
		{
			ret = fat16_free_chain(v, ref->de.cluster_lo);
			if (ret != 0)
				return ret;
		}
		ref->de.cluster_lo = 0;
		ref->de.cluster_hi = 0;
		ref->de.size = 0;
		return fat16_write_dirent_at(v, ref);
	}

	if (cluster < 2)
	{
		ret = fat16_alloc_cluster(v, &cluster);
		if (ret != 0)
			return ret;
		ret = fat16_zero_cluster(v, cluster);
		if (ret != 0)
			return ret;
		ref->de.cluster_lo = cluster;
		ref->de.cluster_hi = 0;
		last = cluster;
		have = cluster_bytes;
	}
	else
	{
		while (cluster >= 2 && cluster < FAT16_CLUSTER_EOF)
		{
			last = cluster;
			have += cluster_bytes;
			cluster = fat16_next_cluster(v, cluster);
		}
	}

	while (have < need)
	{
		uint16_t neu;

		ret = fat16_alloc_cluster(v, &neu);
		if (ret != 0)
			return ret;
		ret = fat16_zero_cluster(v, neu);
		if (ret != 0)
			return ret;
		ret = fat16_set_fat(v, last, neu);
		if (ret != 0)
			return ret;
		last = neu;
		have += cluster_bytes;
	}

	/* Shrink: free excess clusters beyond need. */
	if (ref->de.size > need || have > need)
	{
		size_t keep = (need + cluster_bytes - 1) / cluster_bytes;
		size_t idx = 0;
		uint16_t c = ref->de.cluster_lo;
		uint16_t prev = 0;

		while (c >= 2 && c < FAT16_CLUSTER_EOF)
		{
			uint16_t next = fat16_next_cluster(v, c);

			idx++;
			if (idx == keep)
			{
				ret = fat16_set_fat(v, c, FAT16_CLUSTER_EOF);
				if (ret != 0)
					return ret;
				if (next >= 2 && next < FAT16_CLUSTER_EOF)
					fat16_free_chain(v, next);
				break;
			}
			prev = c;
			(void)prev;
			c = next;
		}
	}

	ref->de.size = (uint32_t)need;
	return fat16_write_dirent_at(v, ref);
}

static int fat16_write_file(struct fat16_vol *v, struct fat16_ent_ref *ref,
			    const void *buf, size_t count, size_t *nwrote, off_t offset)
{
	uint32_t cluster_bytes;
	uint8_t *cluster_buf;
	size_t done = 0;
	size_t end;
	uint16_t cluster;
	size_t skip;
	int ret = 0;

	if (!buf || !nwrote || offset < 0)
		return -EINVAL;
	*nwrote = 0;
	if ((uint64_t)offset > UINT32_MAX ||
	    count > (size_t)(UINT32_MAX - (uint64_t)offset))
		return -EFBIG;
	end = (size_t)offset + count;
	if (end > ref->de.size)
	{
		ret = fat16_ensure_size(v, ref, end);
		if (ret != 0)
			return ret;
	}

	cluster = ref->de.cluster_lo;
	cluster_bytes = (uint32_t)v->sectors_per_cluster * 512U;
	cluster_buf = (uint8_t *)kmalloc_try(cluster_bytes);
	if (!cluster_buf)
		return -ENOMEM;
	skip = (size_t)offset;

	while (cluster >= 2 && cluster < FAT16_CLUSTER_EOF && done < count)
	{
		size_t coff;
		int dirty = 0;

		ret = fat16_read_sectors(v, fat16_cluster_lba(v, cluster),
					 v->sectors_per_cluster, cluster_buf);
		if (ret != 0)
			break;

		for (coff = 0; coff < cluster_bytes && done < count; coff++)
		{
			if (skip > 0)
			{
				skip--;
				continue;
			}
			cluster_buf[coff] = ((const uint8_t *)buf)[done++];
			dirty = 1;
		}
		if (dirty)
		{
			ret = fat16_write_sectors(v, fat16_cluster_lba(v, cluster),
						  v->sectors_per_cluster, cluster_buf);
			if (ret != 0)
				break;
		}
		cluster = fat16_next_cluster(v, cluster);
	}

	kfree(cluster_buf);
	if (ret != 0)
		return ret;
	*nwrote = done;
	ref->de.attr |= FAT_ATTR_ARCH;
	return fat16_write_dirent_at(v, ref);
}

static int fat16_init_subdir(struct fat16_vol *v, uint16_t cluster, uint16_t parent_cluster,
			     int parent_is_root)
{
	uint32_t bytes = (uint32_t)v->sectors_per_cluster * 512U;
	uint8_t *buf;
	struct fat16_dirent *dot;
	struct fat16_dirent *dotdot;
	int ret;

	buf = (uint8_t *)kmalloc_try(bytes);
	if (!buf)
		return -ENOMEM;
	memset(buf, 0, bytes);
	dot = (struct fat16_dirent *)buf;
	dotdot = (struct fat16_dirent *)(buf + 32);
	memset(dot->name, ' ', 11);
	dot->name[0] = '.';
	dot->attr = FAT_ATTR_DIR;
	dot->cluster_lo = cluster;
	memset(dotdot->name, ' ', 11);
	dotdot->name[0] = '.';
	dotdot->name[1] = '.';
	dotdot->attr = FAT_ATTR_DIR;
	dotdot->cluster_lo = parent_is_root ? 0 : parent_cluster;
	ret = fat16_write_sectors(v, fat16_cluster_lba(v, cluster), v->sectors_per_cluster,
				  buf);
	kfree(buf);
	return ret;
}

int fat16_disk_mount(const char *dev, const char *mount_dir)
{
	struct fat16_vol *slot = NULL;
	char blk[16];
	uint32_t part_lba = 0;
	int whole = 0;
	uint8_t boot[512];
	int ret;
	char mount_norm[FAT16_MAX_PATH];
	uint8_t disk_id;

	if (!dev || !mount_dir)
		return -EINVAL;

	for (int i = 0; i < FAT16_MAX_MOUNTS; i++)
	{
		if (!g_vols[i].in_use)
		{
			slot = &g_vols[i];
			break;
		}
	}
	if (!slot)
		return -ENOSPC;

	ret = fat16_parse_dev(dev, blk, sizeof(blk), &part_lba, &whole);
	if (ret != 0)
		return ret;
	if (!ir0_block_name_is_present(blk))
		return -ENXIO;

	disk_id = (uint8_t)(blk[2] - 'a');

	if (whole)
	{
		if (ir0_block_read_by_name(blk, 0, 1, boot))
			return -EIO;
		if (fat16_disk_probe_bpb(boot, NULL) != 0)
		{
			if (fat16_find_fat16_partition(disk_id, &part_lba) != 0)
				return -EINVAL;
			whole = 0;
		}
	}

	memset(slot, 0, sizeof(*slot));
	strncpy(slot->blk_name, blk, sizeof(slot->blk_name) - 1);
	slot->base_lba = part_lba;

	if (ir0_block_read_by_name(blk, part_lba, 1, boot))
		return -EIO;
	ret = fat16_load_bpb(slot, boot);
	if (ret != 0)
		return ret;

	strncpy(mount_norm, mount_dir, sizeof(mount_norm) - 1);
	mount_norm[sizeof(mount_norm) - 1] = '\0';
	if (mount_norm[0] != '/')
		return -EINVAL;
	strncpy(slot->mount_path, mount_norm, sizeof(slot->mount_path) - 1);
	slot->mount_path[sizeof(slot->mount_path) - 1] = '\0';
	slot->in_use = 1;
	return 0;
}

int fat16_disk_umount(const char *mount_dir)
{
	if (!mount_dir)
		return -EINVAL;
	for (int i = 0; i < FAT16_MAX_MOUNTS; i++)
	{
		if (g_vols[i].in_use && strcmp(g_vols[i].mount_path, mount_dir) == 0)
		{
			memset(&g_vols[i], 0, sizeof(g_vols[i]));
			return 0;
		}
	}
	return -ENOENT;
}

int fat16_disk_path_is_mounted(const char *vfs_path)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;

	return fat16_find_mount(vfs_path, rel, sizeof(rel), &is_root) != NULL;
}

int fat16_disk_stat(const char *path, stat_t *buf)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	int ret;

	if (!buf)
		return -EFAULT;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	memset(buf, 0, sizeof(*buf));
	if (is_root)
	{
		buf->st_mode = S_IFDIR | 0777;
		buf->st_nlink = 1;
		return 0;
	}
	ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
	if (ret != 0)
		return ret;
	buf->st_mode = (ref.de.attr & FAT_ATTR_DIR) ? (S_IFDIR | 0777)
						    : (S_IFREG | 0666);
	buf->st_size = ref.de.size;
	buf->st_nlink = 1;
	return 0;
}

int fat16_disk_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	uint8_t *buf = NULL;
	uint32_t bytes = 0;
	uint32_t i;
	uint32_t nent;
	int out = 0;
	int dir_is_root;
	uint16_t dir_cluster;
	int ret;

	if (!entries || max <= 0)
		return -EINVAL;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
	{
		dir_is_root = 1;
		dir_cluster = 0;
	}
	else
	{
		ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
		if (ret != 0)
			return ret;
		if (!(ref.de.attr & FAT_ATTR_DIR))
			return -ENOTDIR;
		dir_is_root = 0;
		dir_cluster = ref.de.cluster_lo;
	}

	ret = fat16_load_dir_buf(v, dir_is_root, dir_cluster, &buf, &bytes);
	if (ret != 0)
		return ret;
	nent = bytes / 32U;
	for (i = 0; i < nent && out < max; i++)
	{
		struct fat16_dirent *de = (struct fat16_dirent *)(buf + i * 32U);

		if (de->name[0] == 0x00)
			break;
		if (de->name[0] == (uint8_t)0xE5 || de->attr == FAT_ATTR_LFN)
			continue;
		fat16_format_dirent_name(de, entries[out].name, sizeof(entries[out].name));
		if (entries[out].name[0] == '\0')
			continue;
		entries[out].type = (de->attr & FAT_ATTR_DIR) ? DT_DIR : DT_REG;
		out++;
	}
	kfree(buf);
	return out;
}

int fat16_disk_read(const char *path, void *buf, size_t count, size_t *bytes_read,
		    off_t offset)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	int ret;

	if (!buf || !bytes_read)
		return -EINVAL;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EISDIR;
	ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
	if (ret != 0)
		return ret;
	if (ref.de.attr & FAT_ATTR_DIR)
		return -EISDIR;
	return fat16_read_file(v, &ref.de, buf, count, bytes_read, offset);
}

int fat16_disk_create(const char *path, mode_t mode)
{
	char rel[FAT16_MAX_PATH];
	char leaf[FAT16_MAX_NAME];
	int is_root = 0;
	int parent_is_root = 1;
	uint16_t parent_cluster = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref slot;
	struct fat16_ent_ref existing;
	int ret;

	(void)mode;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EISDIR;
	if (fat16_resolve_path(v, rel, 0, NULL, 0, &existing, NULL, NULL) == 0)
		return -EEXIST;
	ret = fat16_resolve_path(v, rel, 1, leaf, sizeof(leaf), NULL, &parent_is_root,
				 &parent_cluster);
	if (ret != 0)
		return ret;
	ret = fat16_find_free_slot(v, parent_is_root, parent_cluster, &slot);
	if (ret != 0)
		return ret;
	memset(&slot.de, 0, sizeof(slot.de));
	ret = fat16_encode_83(leaf, slot.de.name);
	if (ret != 0)
		return ret;
	slot.de.attr = FAT_ATTR_ARCH;
	slot.de.size = 0;
	return fat16_write_dirent_at(v, &slot);
}

int fat16_disk_mkdir(const char *path, mode_t mode)
{
	char rel[FAT16_MAX_PATH];
	char leaf[FAT16_MAX_NAME];
	int is_root = 0;
	int parent_is_root = 1;
	uint16_t parent_cluster = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref slot;
	struct fat16_ent_ref existing;
	uint16_t neu;
	int ret;

	(void)mode;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EEXIST;
	if (fat16_resolve_path(v, rel, 0, NULL, 0, &existing, NULL, NULL) == 0)
		return -EEXIST;
	ret = fat16_resolve_path(v, rel, 1, leaf, sizeof(leaf), NULL, &parent_is_root,
				 &parent_cluster);
	if (ret != 0)
		return ret;
	ret = fat16_alloc_cluster(v, &neu);
	if (ret != 0)
		return ret;
	ret = fat16_init_subdir(v, neu, parent_cluster, parent_is_root);
	if (ret != 0)
	{
		fat16_free_chain(v, neu);
		return ret;
	}
	ret = fat16_find_free_slot(v, parent_is_root, parent_cluster, &slot);
	if (ret != 0)
	{
		fat16_free_chain(v, neu);
		return ret;
	}
	memset(&slot.de, 0, sizeof(slot.de));
	ret = fat16_encode_83(leaf, slot.de.name);
	if (ret != 0)
	{
		fat16_free_chain(v, neu);
		return ret;
	}
	slot.de.attr = FAT_ATTR_DIR;
	slot.de.cluster_lo = neu;
	return fat16_write_dirent_at(v, &slot);
}

int fat16_disk_unlink(const char *path)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	int ret;

	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EBUSY;
	ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
	if (ret != 0)
		return ret;
	if (ref.de.attr & FAT_ATTR_DIR)
		return -EISDIR;
	if (ref.de.cluster_lo >= 2)
	{
		ret = fat16_free_chain(v, ref.de.cluster_lo);
		if (ret != 0)
			return ret;
	}
	ref.de.name[0] = 0xE5;
	return fat16_write_dirent_at(v, &ref);
}

int fat16_disk_rmdir(const char *path)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	int empty;
	int ret;

	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EBUSY;
	ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
	if (ret != 0)
		return ret;
	if (!(ref.de.attr & FAT_ATTR_DIR))
		return -ENOTDIR;
	empty = fat16_dir_is_empty(v, ref.de.cluster_lo);
	if (empty < 0)
		return empty;
	if (!empty)
		return -ENOTEMPTY;
	ret = fat16_free_chain(v, ref.de.cluster_lo);
	if (ret != 0)
		return ret;
	ref.de.name[0] = 0xE5;
	return fat16_write_dirent_at(v, &ref);
}

int fat16_disk_link(const char *oldpath, const char *newpath)
{
	char orel[FAT16_MAX_PATH];
	char nrel[FAT16_MAX_PATH];
	char leaf[FAT16_MAX_NAME];
	int o_root = 0;
	int n_root = 0;
	int parent_is_root = 1;
	uint16_t parent_cluster = 0;
	struct fat16_vol *ov;
	struct fat16_vol *nv;
	struct fat16_ent_ref oldref;
	struct fat16_ent_ref slot;
	struct fat16_ent_ref existing;
	uint8_t *data = NULL;
	size_t nread = 0;
	size_t nwrote = 0;
	int ret;

	ov = fat16_find_mount(oldpath, orel, sizeof(orel), &o_root);
	nv = fat16_find_mount(newpath, nrel, sizeof(nrel), &n_root);
	if (!ov || ov != nv)
		return -EXDEV;
	if (o_root || n_root)
		return -EPERM;
	ret = fat16_resolve_path(ov, orel, 0, NULL, 0, &oldref, NULL, NULL);
	if (ret != 0)
		return ret;
	if (oldref.de.attr & FAT_ATTR_DIR)
		return -EPERM;
	if (fat16_resolve_path(ov, nrel, 0, NULL, 0, &existing, NULL, NULL) == 0)
		return -EEXIST;
	ret = fat16_resolve_path(ov, nrel, 1, leaf, sizeof(leaf), NULL, &parent_is_root,
				 &parent_cluster);
	if (ret != 0)
		return ret;
	ret = fat16_find_free_slot(ov, parent_is_root, parent_cluster, &slot);
	if (ret != 0)
		return ret;

	/*
	 * FAT has no real hard links. Copy into a fresh cluster chain so a
	 * subsequent unlink of @oldpath (vfs_rename) does not free @newpath data.
	 */
	memset(&slot.de, 0, sizeof(slot.de));
	ret = fat16_encode_83(leaf, slot.de.name);
	if (ret != 0)
		return ret;
	slot.de.attr = FAT_ATTR_ARCH;
	slot.de.size = 0;
	ret = fat16_write_dirent_at(ov, &slot);
	if (ret != 0)
		return ret;

	if (oldref.de.size == 0)
		return 0;

	data = (uint8_t *)kmalloc_try(oldref.de.size);
	if (!data)
		return -ENOMEM;
	ret = fat16_read_file(ov, &oldref.de, data, oldref.de.size, &nread, 0);
	if (ret != 0 || nread != oldref.de.size)
	{
		kfree(data);
		return ret != 0 ? ret : -EIO;
	}
	/* Re-resolve new entry for write (index may be stale after write_dirent). */
	ret = fat16_resolve_path(ov, nrel, 0, NULL, 0, &slot, NULL, NULL);
	if (ret != 0)
	{
		kfree(data);
		return ret;
	}
	ret = fat16_write_file(ov, &slot, data, nread, &nwrote, 0);
	kfree(data);
	if (ret != 0)
		return ret;
	if (nwrote != nread)
		return -EIO;
	return 0;
}

int fat16_disk_chown(const char *path, uid_t owner, gid_t group)
{
	(void)path;
	(void)owner;
	(void)group;
	return 0;
}

int fat16_disk_chmod(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return 0;
}

int fat16_disk_write(const char *path, const void *buf, size_t count,
		     size_t *bytes_written, off_t offset)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	int ret;

	if (!buf || !bytes_written)
		return -EINVAL;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EISDIR;
	ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
	if (ret != 0)
		return ret;
	if (ref.de.attr & FAT_ATTR_DIR)
		return -EISDIR;
	return fat16_write_file(v, &ref, buf, count, bytes_written, offset);
}

int fat16_disk_truncate(const char *path, size_t length)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_ent_ref ref;
	int ret;

	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EISDIR;
	ret = fat16_resolve_path(v, rel, 0, NULL, 0, &ref, NULL, NULL);
	if (ret != 0)
		return ret;
	if (ref.de.attr & FAT_ATTR_DIR)
		return -EISDIR;
	return fat16_ensure_size(v, &ref, length);
}
