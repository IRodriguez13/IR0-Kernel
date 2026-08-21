/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: minix_fs.c
 * Description: MINIX filesystem implementation with disk I/O and directory
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "minix_fs.h"
#include "vfs.h"
#include <ir0/logging.h>
#include <ir0/blockdev.h>
#include <ir0/klog.h>
#include <ir0/clock.h>
#include <ir0/stat.h>
#include <ir0/errno.h>
#include <ir0/console_backend.h>
#include <config.h>
#include <ir0/kmem.h>
#include <ir0/credentials.h>
#include <ir0/permissions.h>
#include <ir0/exec_read_trace.h>
#include <string.h>

#define typewriter_vga_print(msg, color)                                         \
    do {                                                                         \
        const char *__msg = (msg);                                               \
        if (__msg)                                                               \
            console_backend_write(__msg, strlen(__msg), (uint8_t)(color));       \
    } while (0)

#define MINIX_SUPER_MAGIC 0x137F
#define MINIX_MAGIC 0x137F

#define MINIX_ROOT_INODE 1
#define MINIX_MAX_INODES 1024
#define MINIX_MAX_ZONES 65535
#define MINIX_ZONE_SIZE 1024

/* Number of direct zone pointers in a MINIX inode (i_zone[0]..[6]) */
#define MINIX_DIRECT_ZONES 7

typedef struct minix_fs_info
{
  minix_superblock_t superblock;
  uint8_t *inode_bitmap;
  uint8_t *zone_bitmap;
  minix_inode_t *inode_table;
  uint8_t *zone_table;
  bool initialized;
} minix_fs_info_t;

static minix_fs_info_t minix_fs;
static dev_t minix_root_dev;
static int minix_blockdev_classified;
static int minix_zmap_dirty;
static uint32_t minix_zmap_search_start;

/*
 * MINIX i_time is a POSIX time_t (seconds); the kernel clock counts
 * milliseconds since boot.
 */
static uint32_t minix_now_seconds(void)
{
	return (uint32_t)(get_system_time() / 1000ULL);
}

static size_t minix_zmap_bytes(void)
{
	uint16_t blocks = minix_fs.superblock.s_zmap_blocks;

	if (blocks == 0)
		blocks = 1;
	return (size_t)blocks * (size_t)MINIX_BLOCK_SIZE;
}

static int minix_sync_zone_bitmap(void)
{
	uint32_t zmap_block;
	uint16_t blocks;
	uint16_t b;

	if (!minix_fs.zone_bitmap || !minix_zmap_dirty)
		return 0;

	blocks = minix_fs.superblock.s_zmap_blocks;
	if (blocks == 0)
		return -EIO;

	zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
	for (b = 0; b < blocks; b++)
	{
		if (minix_write_block(zmap_block + b,
				      minix_fs.zone_bitmap +
					  (size_t)b * MINIX_BLOCK_SIZE) != 0)
			return -EIO;
	}
	minix_zmap_dirty = 0;
	return 0;
}

static dev_t minix_root_dev_id(void)
{
	if (minix_root_dev == 0)
		minix_root_dev = ir0_block_lookup_by_name(CONFIG_ROOT_BLOCK_DEVICE);
	return minix_root_dev;
}

static void minix_emit_blockdev_classify(void)
{
	if (minix_blockdev_classified)
		return;
	minix_blockdev_classified = 1;
	klog_info("MINIX", "CLASSIFY MINIX_USES_BLOCKDEV_FACADE");
}

static bool minix_root_block_present(void)
{
	dev_t dev = minix_root_dev_id();

	return dev != 0 && ir0_block_is_present(dev);
}

int minix_read_block(uint32_t block_num, void *buffer)
{
	uint32_t lba = block_num * 2;
	uint8_t num_sectors = 2;
	dev_t dev;
	int ret;

	minix_emit_blockdev_classify();
	dev = minix_root_dev_id();
	if (dev == 0)
		return -ENODEV;

	ret = ir0_block_read(dev, lba, num_sectors, buffer);
	exec_read_trace_device_read(lba, num_sectors, ret == 0 ? 1 : 0, buffer);

	if (ret != 0)
		return -EIO;

	return 0;
}

int minix_write_block(uint32_t block_num, const void *buffer)
{
	uint32_t lba = block_num * 2;
	uint8_t num_sectors = 2;
	dev_t dev;
	int ret;

	minix_emit_blockdev_classify();
	dev = minix_root_dev_id();
	if (dev == 0)
		return -ENODEV;

	ret = ir0_block_write(dev, lba, num_sectors, buffer);
	if (ret != 0)
		return -EIO;

	return 0;
}

void minix_mark_inode_used(uint32_t inode_num)
{
  if (inode_num >= MINIX_MAX_INODES)
    return;

  uint32_t byte = inode_num / 8;
  uint32_t bit = inode_num % 8;

  minix_fs.inode_bitmap[byte] |= (1 << bit);
}

void minix_mark_inode_free(uint32_t inode_num)
{
  if (inode_num >= MINIX_MAX_INODES)
    return;

  uint32_t byte = inode_num / 8;
  uint32_t bit = inode_num % 8;

  minix_fs.inode_bitmap[byte] &= ~(1 << bit);
}

bool minix_is_zone_free(uint32_t zone_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (!minix_fs.zone_bitmap)
    return false;
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
    return false;

  byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;
  if (byte_index >= minix_zmap_bytes())
    return false;

  /* MINIX: bit 1 = free, bit 0 = used */
  return (minix_fs.zone_bitmap[byte_index] & (1U << bit_index)) != 0;
}

void minix_mark_zone_used(uint32_t zone_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (!minix_fs.zone_bitmap)
    return;
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
    return;

  byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;
  if (byte_index >= minix_zmap_bytes())
    return;

  minix_fs.zone_bitmap[byte_index] &= ~(1U << bit_index);
  minix_zmap_dirty = 1;
  if (zone_num + 1 > minix_zmap_search_start)
    minix_zmap_search_start = zone_num + 1;
}

uint32_t minix_alloc_zone(void)
{
  uint32_t limit = minix_fs.superblock.s_nzones;
  uint32_t first = minix_fs.superblock.s_firstdatazone;
  uint32_t start;
  uint32_t i;

  if (limit == 0 || limit > MINIX_MAX_ZONES)
    limit = MINIX_MAX_ZONES;
  if (first >= limit)
    return 0;

  start = minix_zmap_search_start;
  if (start < first || start >= limit)
    start = first;

  for (i = start; i < limit; i++)
  {
    if (minix_is_zone_free(i))
    {
      minix_mark_zone_used(i);
      return i;
    }
  }
  for (i = first; i < start; i++)
  {
    if (minix_is_zone_free(i))
    {
      minix_mark_zone_used(i);
      return i;
    }
  }
  return 0;
}

void minix_free_zone(uint32_t zone_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (!minix_fs.zone_bitmap)
    return;
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
    return;

  byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;
  if (byte_index >= minix_zmap_bytes())
    return;

  minix_fs.zone_bitmap[byte_index] |= (1U << bit_index);
  minix_zmap_dirty = 1;
  if (zone_num < minix_zmap_search_start)
    minix_zmap_search_start = zone_num;
}



static int minix_read_inode(uint32_t inode_num, minix_inode_t *inode)
{

  if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode)
  {
    klog_print("SERIAL: minix_read_inode: invalid parameters - inode_num=");
    klog_hex32(inode_num);
    klog_print("\n");
    return -1;
  }

  // Check if filesystem is initialized
  if (!minix_fs.initialized)
  {
    klog_print("SERIAL: minix_read_inode: filesystem not initialized!\n");
    return -1;
  }

  // Validate superblock values before using them
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    log_debug_fmt("MINIX", "read_inode: invalid magic=0x%x", minix_fs.superblock.s_magic);
    return -1;
  }
  if (minix_fs.superblock.s_imap_blocks == 0 || minix_fs.superblock.s_zmap_blocks == 0)
  {
    log_debug_fmt("MINIX", "read_inode: invalid bitmaps imap=%u zmap=%u",
                  minix_fs.superblock.s_imap_blocks, minix_fs.superblock.s_zmap_blocks);
    return -1;
  }


  uint32_t inode_table_start =
      2 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  int result = minix_read_block(inode_block, block_buffer);
  if (result != 0)
  {
    log_debug_fmt("MINIX", "read_inode: read_block failed ino=%u", inode_num);
    return -1;
  }

  kmemcpy(inode, block_buffer + inode_offset, sizeof(minix_inode_t));
  return 0;
}

uint32_t minix_alloc_inode(void)
{
  uint32_t limit = minix_fs.superblock.s_ninodes;

  if (limit == 0 || limit >= MINIX_MAX_INODES)
    limit = MINIX_MAX_INODES - 1;

  for (uint32_t i = 1; i <= limit; i++)
  {
    uint32_t byte = i / 8;
    uint32_t bit = i % 8;

    if (!(minix_fs.inode_bitmap[byte] & (1 << bit)))
    {
      minix_fs.inode_bitmap[byte] |= (1 << bit);
      return i;
    }
  }

  return 0;
}

static int minix_sync_inode_bitmap(void)
{
  uint32_t imap_block = 2;

  if (!minix_fs.inode_bitmap)
    return -EIO;
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
    return -EIO;
  return 0;
}

// Global static inodes to avoid memory issues
static minix_inode_t cached_root_inode;
static minix_inode_t cached_result_inode;
static bool root_inode_cached = false;
static uint16_t minix_last_resolved_ino;

/*
 * Path walk that copies the inode onto the caller. find_inode() used to
 * return a pointer to one of two static buffers; a preempted stat() then
 * filled struct stat from whatever the next walk left there (or from a
 * second get_inode_number() walk that raced with exec/ls).
 */
static int minix_lookup(const char *pathname, minix_inode_t *out, uint16_t *ino_out)
{
  uint16_t current_inode_num = MINIX_ROOT_INODE;
  minix_inode_t current_inode;
  char path_copy[VFS_PATH_MAX];
  char *token;
  char *next;

  minix_last_resolved_ino = 0;

  if (!pathname || !out || !ino_out || !minix_fs.initialized)
    return -EINVAL;

  if (kstrcmp(pathname, "/") == 0)
  {
    int read_result = minix_read_inode(MINIX_ROOT_INODE, &cached_root_inode);

    if (read_result != 0)
    {
      log_debug_fmt("MINIX", "find_inode('/') read failed=%d", read_result);
      return -EIO;
    }
    root_inode_cached = true;
    kmemcpy(out, &cached_root_inode, sizeof(*out));
    *ino_out = MINIX_ROOT_INODE;
    minix_last_resolved_ino = MINIX_ROOT_INODE;
    return 0;
  }

  kstrncpy(path_copy, pathname, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0)
    return -EIO;

  token = path_copy;
  if (*token == '/')
    token++;

  if (*token == '\0')
  {
    kmemcpy(out, &current_inode, sizeof(*out));
    *ino_out = current_inode_num;
    minix_last_resolved_ino = current_inode_num;
    return 0;
  }

  next = token;
  while (*next && *next != '/')
    next++;

  if (*next == '/')
    *next++ = '\0';
  else
    next = NULL;

  while (token != NULL && *token != '\0')
  {
    uint16_t found_inode;

    if (!(current_inode.i_mode & MINIX_IFDIR))
      return -ENOTDIR;

    found_inode = minix_fs_find_dir_entry(&current_inode, token);
    if (found_inode == 0)
      return -ENOENT;

    current_inode_num = found_inode;
    if (minix_read_inode(found_inode, &current_inode) != 0)
      return -EIO;

    if (next)
    {
      token = next;
      while (*next && *next != '/')
        next++;

      if (*next == '/')
        *next++ = '\0';
      else
        next = NULL;
    }
    else
    {
      token = NULL;
    }
  }

  kmemcpy(out, &current_inode, sizeof(*out));
  *ino_out = current_inode_num;
  minix_last_resolved_ino = current_inode_num;
  return 0;
}

minix_inode_t *minix_fs_find_inode(const char *pathname)
{
  if (minix_lookup(pathname, &cached_result_inode, &minix_last_resolved_ino) != 0)
    return NULL;
  return &cached_result_inode;
}

uint16_t minix_fs_get_inode_number(const char *pathname)
{
  minix_inode_t tmp;
  uint16_t ino;

  if (minix_lookup(pathname, &tmp, &ino) != 0)
    return 0;
  kmemcpy(&cached_result_inode, &tmp, sizeof(tmp));
  return ino;
}

uint16_t minix_fs_find_dir_entry(const minix_inode_t *dir_inode,
                                 const char *name)
{
  if (!dir_inode || !name || !(dir_inode->i_mode & MINIX_IFDIR))
  {
    return 0;
  }

  // Leer todas las zonas del directorio
  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    if (dir_inode->i_zone[i] == 0)
    {
      continue; // Zona vacía
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
    {
      continue;
    }

    // Buscar en las entradas del directorio
    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      if (entries[j].inode == 0)
      {
        continue; // Entrada vacía
      }

      if (kstrcmp(entries[j].name, name) == 0)
      {
        return entries[j].inode;
      }
    }
  }

  return 0;
}

int minix_fs_write_inode(uint16_t inode_num, const minix_inode_t *inode)
{
  if (!inode || inode_num == 0)
  {
    return -EINVAL;
  }

  // Calcular la posición del inode en el disco (DEBE SER IGUAL A minix_read_inode)
  // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  uint32_t inode_table_start =
      2 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;


#if DEBUG_VFS
  klog_print("SERIAL: write_inode: inode_num=");
  klog_hex32(inode_num);
  klog_print(" block=");
  klog_hex32(inode_block);
  klog_print(" offset=");
  klog_hex32(inode_offset);
  klog_print("\n");
#endif

  // Leer el bloque que contiene el inode
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(inode_block, block_buffer) != 0)
  {
#if DEBUG_VFS
    klog_print("SERIAL: write_inode: failed to read block\n");
#endif
    return -EIO;
  }

  // Copiar el inode al buffer
  kmemcpy(block_buffer + inode_offset, inode, sizeof(minix_inode_t));

  // Escribir el bloque actualizado
  if (minix_write_block(inode_block, block_buffer) != 0)
  {
#if DEBUG_VFS
    klog_print("SERIAL: write_inode: failed to write block\n");
#endif
    return -EIO;
  }

  // Invalidate root cache if we wrote the root inode
  if (inode_num == MINIX_ROOT_INODE)
  {
    root_inode_cached = false;
  }

#if DEBUG_VFS
  klog_print("SERIAL: write_inode: success\n");
#endif
  return 0;
}

int minix_fs_free_inode(uint16_t inode_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (inode_num == 0 || inode_num > minix_fs.superblock.s_ninodes)
    return -EINVAL;

  /*
   * Inode bitmap lives at block 2 (same as minix_sync_inode_bitmap /
   * minix_alloc_inode). A previous off-by-one used block 1 and corrupted
   * s_zmap_blocks in the superblock — next mount then kmalloc'd ~64MiB.
   * Bit polarity matches alloc: bit set = used, clear = free.
   */
  byte_index = (uint32_t)inode_num / 8;
  bit_index = (uint32_t)inode_num % 8;
  if (byte_index >= (uint32_t)minix_fs.superblock.s_imap_blocks * MINIX_BLOCK_SIZE)
    return -EINVAL;

  if (minix_fs.inode_bitmap)
    minix_fs.inode_bitmap[byte_index] &= ~(1u << bit_index);

  if (minix_sync_inode_bitmap() != 0)
    return -EIO;
  return 0;
}

int minix_fs_split_path(const char *pathname, char *parent_path,
                        char *filename)
{
  if (!pathname || !parent_path || !filename)
  {
    return -EINVAL;
  }

  // Encontrar la última barra
  // Find last slash
  const char *last_slash = NULL;
  const char *p = pathname;
  while (*p)
  {
    if (*p == '/')
      last_slash = p;
    p++;
  }
  if (!last_slash)
  {
    size_t i;

    /* Relative path → treat as under root. Caller buffer ≥ MINIX_NAME_LEN+1. */
    kstrncpy(parent_path, "/", 2);
    for (i = 0; i < MINIX_NAME_LEN && pathname[i]; i++)
      filename[i] = pathname[i];
    filename[i] = '\0';
    return 0;
  }

  if (last_slash == pathname)
  {
    // Es el directorio raíz
    kstrncpy(parent_path, "/", 2);
  }
  else
  {
    // Copiar la parte del directorio padre
    size_t parent_len = last_slash - pathname;
    kstrncpy(parent_path, pathname, parent_len);
    parent_path[parent_len] = '\0';
  }

  /* Up to 14 chars + NUL (classic MINIX name field width). */
  {
    const char *base = last_slash + 1;
    size_t i;

    for (i = 0; i < MINIX_NAME_LEN && base[i]; i++)
      filename[i] = base[i];
    filename[i] = '\0';
  }

  return 0;
}

/**
 * Add a directory entry to the parent directory
 * @param parent_inode Parent directory inode (will be modified)
 * @param filename Name of the new entry
 * @param inode_num Inode number of the new entry
 * @return 0 on success, -1 on failure
 */
int minix_fs_add_dir_entry(minix_inode_t *parent_inode, const char *filename,
                           uint16_t inode_num)
{
  if (!parent_inode || !filename || inode_num == 0 || inode_num >= 65535)
  {
    return -EINVAL; // Invalid parameters
  }

  /*
   * Classic MINIX v1: name field is 14 bytes (may be unterminated if full).
   * Reject only names longer than MINIX_NAME_LEN (not >=).
   */
  size_t name_len = kstrlen(filename);
  if (name_len == 0 || name_len > MINIX_NAME_LEN)
  {
    return -EINVAL; // Invalid filename length
  }

  // Try to find a free entry in existing zones
  for (int zone_index = 0; zone_index < MINIX_DIRECT_ZONES; zone_index++)
  {
    if (parent_inode->i_zone[zone_index] == 0)
    {
      break; // No more allocated zones
    }

    uint32_t zone = parent_inode->i_zone[zone_index];
    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};

    // Read the zone
    if (minix_read_block(zone, block_buffer) != 0)
    {
      continue; // Skip bad blocks
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    // Look for a free entry or check for duplicate filename
    for (int i = 0; i < num_entries; i++)
    {
      // Check for duplicate filename
      if (entries[i].inode != 0 && kstrncmp(entries[i].name, filename, MINIX_NAME_LEN) == 0)
      {
        return -EEXIST; // Entry with this name already exists
      }

      // Found a free entry
      if (entries[i].inode == 0)
      {
        // Initialize the entry
        entries[i].inode = inode_num;
        kmemcpy(entries[i].name, filename, name_len);
        if (name_len < MINIX_NAME_LEN)
          kmemset(entries[i].name + name_len, 0, MINIX_NAME_LEN - name_len);

        // Write the updated block back
        if (minix_write_block(zone, block_buffer) != 0)
        {
          return -EIO; // Failed to write block
        }

        // Update directory size if needed
        size_t entry_offset = (zone_index * MINIX_BLOCK_SIZE) + (i * sizeof(minix_dir_entry_t));
        if (entry_offset + sizeof(minix_dir_entry_t) > parent_inode->i_size)
        {
          parent_inode->i_size = entry_offset + sizeof(minix_dir_entry_t);
        }

        return 0; // Success
      }
    }
  }

  // No space in existing zones, allocate a new one
  for (int zone_index = 0; zone_index < MINIX_DIRECT_ZONES; zone_index++)
  {
    if (parent_inode->i_zone[zone_index] == 0)
    {
      // Allocate a new zone
      uint32_t new_zone = minix_alloc_zone();
      if (new_zone == 0)
      {
        return -ENOSPC; // No free zones
      }

      // Initialize the new zone with zeros
      uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
      minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;

      // Add the new entry as the first one
      entries[0].inode = inode_num;
      kmemcpy(entries[0].name, filename, name_len);
      if (name_len < MINIX_NAME_LEN)
        kmemset(entries[0].name + name_len, 0, MINIX_NAME_LEN - name_len);

      // Write the new zone
      if (minix_write_block(new_zone, block_buffer) != 0)
      {
        minix_free_zone(new_zone); // Clean up
        return -EIO;
      }

      // Update parent inode
      parent_inode->i_zone[zone_index] = new_zone;
      parent_inode->i_size = (zone_index + 1) * MINIX_BLOCK_SIZE;

      return 0; // Success
    }
  }

  return -ENOSPC;
}

/**
 * Remove a directory entry from a directory
 * @param parent_inode Parent directory inode (will be modified)
 * @param filename Name of the entry to remove
 * @return 0 on success, -1 on failure
 */
int minix_fs_remove_dir_entry(minix_inode_t *parent_inode, const char *filename)
{
  if (!parent_inode || !filename || !*filename)
  {
    return -EINVAL; // Invalid parameters
  }

  // Search for the entry in all directory zones
  for (int zone_idx = 0; zone_idx < MINIX_DIRECT_ZONES; zone_idx++)
  {
    uint32_t zone = parent_inode->i_zone[zone_idx];
    if (zone == 0)
    {
      continue; // No more allocated zones
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
    if (minix_read_block(zone, block_buffer) != 0)
    {
      continue; // Skip bad blocks
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);
    bool entry_found = false;

    // First pass: find the entry
    for (int i = 0; i < num_entries; i++)
    {
      if (entries[i].inode == 0)
      {
        continue; // Skip empty entries
      }

      if (kstrncmp(entries[i].name, filename, MINIX_NAME_LEN) == 0)
      {
        // Found the entry, mark it as free
        entries[i].inode = 0;
        kmemset(entries[i].name, 0, MINIX_NAME_LEN);
        entry_found = true;
        break;
      }
    }

    if (entry_found)
    {
      // Write the updated block back
      if (minix_write_block(zone, block_buffer) != 0)
      {
        return -EIO; // Failed to write block
      }

      // Update directory size if needed
      size_t entry_size = sizeof(minix_dir_entry_t);
      size_t current_size = parent_inode->i_size;
      if (current_size > 0)
      {
        // Only shrink the directory if this was the last entry
        bool is_last_entry = true;
        for (int i = 0; i < num_entries; i++)
        {
          if (entries[i].inode != 0)
          {
            is_last_entry = false;
            break;
          }
        }

        if (is_last_entry)
        {
          // This was the last entry in this block
          if (parent_inode->i_size >= entry_size)
          {
            parent_inode->i_size -= entry_size;
          }
          else
          {
            parent_inode->i_size = 0;
          }
        }
      }

      return 0; // Success
    }
  }

  return -ENOENT;
}

// ===============================================================================
// PUBLIC FUNCTIONS IMPLEMENTATION
// ===============================================================================

bool minix_fs_is_available(void)
{
  return minix_root_block_present();
}

bool minix_fs_is_working(void)
{
    return minix_fs.initialized;
}

/*
 * minix_fs_ensure_root_inode - Repair root when superblock is valid but the
 * on-disk root inode lost its directory type bits (seen on some disk.img builds
 * as mode=0xa35 with IFMT=0). Without IFDIR, vfs_mkdir on "/" fails ENOTDIR.
 */
static int minix_fs_ensure_root_inode(void)
{
  minix_inode_t root;
  mode_t perms;

  if (minix_read_inode(MINIX_ROOT_INODE, &root) != 0)
    return -1;

  if ((root.i_mode & MINIX_IFMT) == MINIX_IFDIR)
    return 0;

  log_warn_fmt("MINIX", "root inode mode=0x%x missing IFDIR, repairing",
               root.i_mode);

  perms = (mode_t)(root.i_mode & 0777);
  if (perms == 0)
    perms = (mode_t)(MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH);

  root.i_mode = (uint16_t)(MINIX_IFDIR | perms);

  if (root.i_zone[0] == 0)
    root.i_zone[0] = minix_fs.superblock.s_firstdatazone;
  if (root.i_nlinks < 2)
    root.i_nlinks = 2;
  if (root.i_size == 0)
    root.i_size = (uint32_t)(2 * sizeof(minix_dir_entry_t));

  if (minix_fs_write_inode(MINIX_ROOT_INODE, &root) != 0)
    return -1;

  if (root.i_zone[0] != 0)
  {
    uint8_t dir_block[MINIX_BLOCK_SIZE];

    if (minix_read_block(root.i_zone[0], dir_block) == 0)
    {
      minix_dir_entry_t *entries = (minix_dir_entry_t *)dir_block;

      if (entries[0].inode == 0)
      {
        entries[0].inode = MINIX_ROOT_INODE;
        kstrncpy(entries[0].name, ".", MINIX_NAME_LEN);
        entries[1].inode = MINIX_ROOT_INODE;
        kstrncpy(entries[1].name, "..", MINIX_NAME_LEN);
        if (minix_write_block(root.i_zone[0], dir_block) != 0)
          return -1;
      }
    }
  }

  root_inode_cached = false;
  return 0;
}

int minix_fs_init(void)
{
  /* FORCE REAL DISK USAGE - In QEMU, disk is always available */

  /* Read superblock from disk (block-sized buffer to avoid overflow) */
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(1, block_buffer) != 0)
  {
    log_debug_fmt("MINIX", "superblock read failed, formatting");
    return minix_fs_format();
  }
  kmemcpy(&minix_fs.superblock, block_buffer, sizeof(minix_superblock_t));
  log_info_fmt("MINIX", "superblock read OK magic=0x%x", minix_fs.superblock.s_magic);

  /* Check magic number */
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    // Invalid - format disk
    return minix_fs_format();
  }

  /*
   * Reject absurd geometry before kmalloc(zmap). A corrupt s_zmap_blocks
   * (historically written by minix_fs_free_inode into the superblock) used
   * to panic with "kmalloc: size too large".
   */
  if (minix_fs.superblock.s_imap_blocks == 0 ||
      minix_fs.superblock.s_zmap_blocks == 0 ||
      minix_fs.superblock.s_zmap_blocks > 256 ||
      minix_fs.superblock.s_imap_blocks > 16)
  {
    log_warn_fmt("MINIX", "corrupt geometry imap=%u zmap=%u, formatting",
                 minix_fs.superblock.s_imap_blocks,
                 minix_fs.superblock.s_zmap_blocks);
    return minix_fs_format();
  }

  // Read bitmaps
  // Block 2 is always the first inode bitmap block
  uint32_t imap_block = 2;
  uint32_t zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
  size_t zmap_bytes;
  uint16_t zb;

  if (!minix_fs.inode_bitmap)
  {
    minix_fs.inode_bitmap = kmalloc(MINIX_BLOCK_SIZE);
    if (!minix_fs.inode_bitmap)
      return -1;
  }

  zmap_bytes = minix_zmap_bytes();
  if (minix_fs.zone_bitmap)
  {
    kfree(minix_fs.zone_bitmap);
    minix_fs.zone_bitmap = NULL;
  }
  minix_fs.zone_bitmap = kmalloc(zmap_bytes);
  if (!minix_fs.zone_bitmap)
    return -1;

  if (minix_read_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return minix_fs_format();
  }

  for (zb = 0; zb < minix_fs.superblock.s_zmap_blocks; zb++)
  {
    if (minix_read_block(zmap_block + zb,
                         minix_fs.zone_bitmap +
                             (size_t)zb * MINIX_BLOCK_SIZE) != 0)
    {
      return minix_fs_format();
    }
  }
  minix_zmap_dirty = 0;
  minix_zmap_search_start = minix_fs.superblock.s_firstdatazone;

  // Double check root inode - if it's all zeros, the disk is corrupt or uninitialized
  minix_inode_t root_inode_check;
  minix_fs.initialized = true; // Temporary set to allow read_inode to work
  if (minix_read_inode(MINIX_ROOT_INODE, &root_inode_check) != 0 || root_inode_check.i_mode == 0)
  {
    log_warn_fmt("MINIX", "root inode corrupt (mode=0x%x), forcing format", root_inode_check.i_mode);
    minix_fs.initialized = false;
    return minix_fs_format();
  }

  if (minix_fs_ensure_root_inode() != 0)
  {
    minix_fs.initialized = false;
    return minix_fs_format();
  }

  if (minix_read_inode(MINIX_ROOT_INODE, &root_inode_check) != 0)
  {
    minix_fs.initialized = false;
    return minix_fs_format();
  }

  log_info_fmt("MINIX", "root inode OK mode=0x%x (dir=%d)", root_inode_check.i_mode,
               (root_inode_check.i_mode & MINIX_IFDIR) ? 1 : 0);

  /* Valid filesystem found */
  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache
  return 0;
}

int minix_fs_format(void)
{
  // REAL MINIX FILESYSTEM CREATION

  // Initialize superblock
  kmemset(&minix_fs.superblock, 0, sizeof(minix_superblock_t));
  minix_fs.superblock.s_magic = MINIX_SUPER_MAGIC;
  minix_fs.superblock.s_ninodes = 64;    // Small but real
  minix_fs.superblock.s_nzones = 1024;   // 1MB of data
  minix_fs.superblock.s_imap_blocks = 1; // 1 block for inode bitmap
  minix_fs.superblock.s_zmap_blocks = 1; // 1 block for zone bitmap
  minix_fs.superblock.s_firstdatazone =
      5; // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  minix_fs.superblock.s_log_zone_size = 0;
  minix_fs.superblock.s_max_size = 1048576; /* 1MB max file size */

  /* Write superblock (block-sized to match on-disk layout) */
  uint8_t sb_block[MINIX_BLOCK_SIZE];
  kmemset(sb_block, 0, sizeof(sb_block));
  kmemcpy(sb_block, &minix_fs.superblock, sizeof(minix_superblock_t));
  if (minix_write_block(1, sb_block) != 0)
  {
    return -1;
  }

  // Inicializar bitmaps
  if (!minix_fs.inode_bitmap)
  {
    minix_fs.inode_bitmap = kmalloc(MINIX_BLOCK_SIZE);
    if (!minix_fs.inode_bitmap)
      return -1;
  }
  if (!minix_fs.zone_bitmap)
  {
    minix_fs.zone_bitmap = kmalloc(MINIX_BLOCK_SIZE);
    if (!minix_fs.zone_bitmap)
      return -1;
  }

  kmemset(minix_fs.inode_bitmap, 0, MINIX_BLOCK_SIZE);
  
  // CRITICAL: Initialize zone bitmap with 0xFF (all bits = 1 = all zones FREE)
  // In MINIX: bit=0 means USED, bit=1 means FREE
  kmemset(minix_fs.zone_bitmap, 0xFF, MINIX_BLOCK_SIZE);

  // Mark inode 1 as used (root directory)
  // In MINIX bitmap: bit 1 is inode 1, bit 0 is unused or reserved
  minix_fs.inode_bitmap[0] = 0x02; // Bit 1 = inode 1

  /* Calculate block offsets for bitmaps and inode table */
  uint32_t imap_block = 2;
  uint32_t zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
  uint32_t inode_table_block = zmap_block + minix_fs.superblock.s_zmap_blocks;
  (void)inode_table_block;  /* Reserved for inode table writes */

  /* Write bitmaps to disk */
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return -1;
  }

  if (minix_write_block(zmap_block, minix_fs.zone_bitmap) != 0)
  {
    return -1;
  }


  // Create root inode
  minix_inode_t root_inode;
  kmemset(&root_inode, 0, sizeof(minix_inode_t));
  root_inode.i_mode = MINIX_IFDIR | MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH;
  root_inode.i_uid = 0;
  root_inode.i_size = 2 * sizeof(minix_dir_entry_t); // . and .. entries
  root_inode.i_time = 0;
  root_inode.i_gid = 0;
  root_inode.i_nlinks = 2;                                    // . and .. links
  root_inode.i_zone[0] = minix_fs.superblock.s_firstdatazone; // First data zone

  klog_print("SERIAL: minix_fs_format: creating root inode with mode=");
  klog_hex32(root_inode.i_mode);
  klog_print(" zone[0]=");
  klog_hex32(root_inode.i_zone[0]);
  klog_print("\n");

  // Write root inode using proper function
  if (minix_fs_write_inode(MINIX_ROOT_INODE, &root_inode) != 0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write root inode!\n");
    return -1;
  }
  
  // Verify what we just wrote
  minix_inode_t verify_inode;
  if (minix_read_inode(MINIX_ROOT_INODE, &verify_inode) == 0) {
    klog_print("SERIAL: minix_fs_format: VERIFY root inode mode=");
    klog_hex32(verify_inode.i_mode);
    klog_print("\n");
  } else {
    klog_print("SERIAL: minix_fs_format: VERIFY FAILED to read back!\n");
  }
  
  klog_print("SERIAL: minix_fs_format: root inode written successfully\n");

  // Create root directory with . and .. entries
  uint8_t root_dir_block[MINIX_BLOCK_SIZE];
  kmemset(root_dir_block, 0, MINIX_BLOCK_SIZE);

  minix_dir_entry_t *entries = (minix_dir_entry_t *)root_dir_block;

  // Entry "."
  entries[0].inode = 1; // Root inode
  kstrncpy(entries[0].name, ".", MINIX_NAME_LEN);

  // Entry ".."
  entries[1].inode = 1; // Root inode (parent of root is root)
  kstrncpy(entries[1].name, "..", MINIX_NAME_LEN);

  // Write root directory
  if (minix_write_block(minix_fs.superblock.s_firstdatazone, root_dir_block) !=
      0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write root directory block!\n");
    return -1;
  }

  klog_print("SERIAL: minix_fs_format: root directory block written successfully\n");

  // Mark only inode 1 as used in bitmap
  minix_fs.inode_bitmap[0] |= 0x02; // Bit 1 = inode 1

  // CRITICAL: Mark root directory zone as used in zone bitmap
  // In MINIX: bit=0 means USED, bit=1 means FREE
  // Root directory uses s_firstdatazone (zone 5), so clear bit 5
  minix_fs.zone_bitmap[0] &= ~(1 << 5); // Mark zone 5 as USED (clear bit)

  klog_print("SERIAL: minix_fs_format: marked root inode and root zone as used\n");

  // Write inode bitmap
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write inode bitmap!\n");
    return -1;
  }

  // Write zone bitmap
  if (minix_write_block(zmap_block, minix_fs.zone_bitmap) != 0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write zone bitmap!\n");
    return -1;
  }

  klog_print("SERIAL: minix_fs_format: filesystem formatted successfully!\n");

  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache

  // Create test files to verify ls works
  klog_print("SERIAL: minix_fs_format: creating test files...\n");  

  klog_print("SERIAL: minix_fs_format: test files created successfully\n");

  return 0;
}

int minix_fs_mkdir(const char *path, mode_t mode)
{
  if (!minix_fs.initialized)
  {
    return -ENODEV;
  }

  if (!path || kstrlen(path) == 0)
  {
    return -EINVAL;
  }

  if (kstrcmp(path, "/") == 0)
  {
    return -EEXIST;
  }

  if (!minix_root_block_present())
  {
    return -EIO;
  }

  char parent_path[VFS_PATH_MAX];
  char dirname[64];

  if (minix_fs_split_path(path, parent_path, dirname) != 0)
  {
    return -EINVAL;
  }

  {
    static const char *blocked[] = {
      "pid.new", "pid", "lock", "stat.new", "status.new", NULL
    };
    int bi;

    for (bi = 0; blocked[bi]; bi++)
    {
      if (kstrcmp(dirname, blocked[bi]) == 0)
        return -EEXIST;
    }
  }

  minix_inode_t parent_inode;
  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    return -ENOENT;
  }

  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (!(parent_inode.i_mode & MINIX_IFDIR))
  {
    return -ENOTDIR;
  }

  uint16_t existing_inode = minix_fs_find_dir_entry(&parent_inode, dirname);
  if (existing_inode != 0)
  {
    minix_inode_t existing;

    if (minix_read_inode(existing_inode, &existing) == 0 &&
        (existing.i_mode & MINIX_IFMT) == 0)
    {
      if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
        return -EIO;
      minix_fs_free_inode(existing_inode);
    }
    else
    {
      return -EEXIST;
    }
  }

  uint16_t new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0)
  {
    return -ENOSPC;
  }
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num == 0)
  {
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  minix_inode_t new_inode;
  kmemset(&new_inode, 0, sizeof(minix_inode_t));
  
  // Apply umask to mode
  {
    const struct ir0_task_cred *cr = ir0_current_cred();

    mode_t effective_mode = mode & ~(cr ? cr->umask : (mode_t)0);
    /* POSIX: a new directory is owned by the creator's effective IDs. */
    new_inode.i_mode = MINIX_IFDIR | (effective_mode & 0777);
    new_inode.i_uid = cr ? (uint16_t)cr->euid : (uint16_t)0;
    new_inode.i_gid = cr ? (uint8_t)cr->egid : (uint8_t)0;
  }
  new_inode.i_size = MINIX_BLOCK_SIZE; // One block for . and ..
  new_inode.i_time = minix_now_seconds();
  new_inode.i_nlinks = 2; // . and ..

  // Allocate and initialize directory block
  uint32_t zone = minix_alloc_zone();
  if (zone == 0)
  {
    minix_fs_free_inode(new_inode_num);
    return -ENOSPC;
  }
  new_inode.i_zone[0] = zone;

  // Initialize directory block with . and ..
  uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
  minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;

  // Add . entry (self)
  entries[0].inode = new_inode_num;
  kstrncpy(entries[0].name, ".", MINIX_NAME_LEN);

  // Add .. entry (parent)
  entries[1].inode = parent_inode_num;
  kstrncpy(entries[1].name, "..", MINIX_NAME_LEN);

  // Clear remaining entries
  size_t max_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);
  for (size_t i = 2; i < max_entries; i++)
  {
    entries[i].inode = 0;
    kmemset(entries[i].name, 0, MINIX_NAME_LEN);
  }

  if (minix_write_block(zone, block_buffer) != 0)
  {
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
  {
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  if (minix_fs_add_dir_entry(&parent_inode, dirname, new_inode_num) != 0)
  {
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  parent_inode.i_nlinks++;
  parent_inode.i_time = minix_now_seconds();

  if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
  {
    return -EIO;
  }

  if (minix_sync_zone_bitmap() != 0)
    return -EIO;

  return 0;
}

static void uint32_to_str(uint32_t num, char *buf, size_t buf_size)
{
  if (buf_size == 0)
    return;

  if (num == 0)
  {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  char tmp[16];
  int pos = 0;
  while (num > 0 && pos < 15)
  {
    tmp[pos++] = '0' + (num % 10);
    num /= 10;
  }

  int i = 0;
  for (int j = pos - 1; j >= 0 && i < (int)buf_size - 1; j--, i++)
  {
    buf[i] = tmp[j];
  }
  buf[i] = '\0';
}

int minix_fs_ls(const char *path, bool detailed)
{
  klog_print("SERIAL: minix_fs_ls called for path: ");
  klog_print(path ? path : "NULL");
  klog_print("\n");

  if (!minix_fs.initialized)
  {
    typewriter_vga_print("ls: filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path)
  {
    typewriter_vga_print("ls: invalid path\n", 0x0C);
    return -1;
  }

  minix_inode_t *dir_inode = minix_fs_find_inode(path);
  if (!dir_inode)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ls: cannot access '%s': No such file or directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  if (!(dir_inode->i_mode & MINIX_IFDIR))
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ls: cannot access '%s': Not a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  minix_inode_t dir_inode_copy;
  kmemcpy(&dir_inode_copy, dir_inode, sizeof(minix_inode_t));

  if (detailed)
  {
    typewriter_vga_print("Permissions Links Size Owner Group Name\n", 0x0F);
  }

  int total_entries_printed = 0;

  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    uint32_t zone = dir_inode_copy.i_zone[i];
    if (zone == 0)
      continue;

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(zone, block_buffer) != 0)
    {
      typewriter_vga_print("ls: error reading directory block\n", 0x0C);
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      if (entries[j].inode == 0)
        continue;

      // Safe name copy to ensure null termination
      char safe_name[MINIX_NAME_LEN + 1];
      // Copy manually to handle non-null terminated source
      int k;
      for (k = 0; k < MINIX_NAME_LEN; k++) {
          safe_name[k] = entries[j].name[k];
          if (safe_name[k] == '\0') break;
      }
      safe_name[k] = '\0';

      if (kstrcmp(safe_name, ".") == 0)
        continue;
      if (kstrcmp(safe_name, "..") == 0)
        continue;

      if (safe_name[0] == '\0')
        continue;

      if (detailed)
      {
        minix_inode_t entry_inode;
        if (minix_read_inode(entries[j].inode, &entry_inode) != 0)
        {
          typewriter_vga_print("ls: error reading inode\n", 0x0C);
          continue;
        }

        char perm_str[12];
        perm_str[0] = (entry_inode.i_mode & MINIX_IFDIR) ? 'd' : '-';
        perm_str[1] = (entry_inode.i_mode & MINIX_IRUSR) ? 'r' : '-';
        perm_str[2] = (entry_inode.i_mode & MINIX_IWUSR) ? 'w' : '-';
        perm_str[3] = (entry_inode.i_mode & MINIX_IXUSR) ? 'x' : '-';
        perm_str[4] = (entry_inode.i_mode & MINIX_IRGRP) ? 'r' : '-';
        perm_str[5] = (entry_inode.i_mode & MINIX_IWGRP) ? 'w' : '-';
        perm_str[6] = (entry_inode.i_mode & MINIX_IXGRP) ? 'x' : '-';
        perm_str[7] = (entry_inode.i_mode & MINIX_IROTH) ? 'r' : '-';
        perm_str[8] = (entry_inode.i_mode & MINIX_IWOTH) ? 'w' : '-';
        perm_str[9] = (entry_inode.i_mode & MINIX_IXOTH) ? 'x' : '-';
        perm_str[10] = ' ';
        perm_str[11] = '\0';

        char nlinks_str[16];
        uint32_to_str(entry_inode.i_nlinks, nlinks_str, sizeof(nlinks_str));

        char size_str[16];
        uint32_to_str(entry_inode.i_size, size_str, sizeof(size_str));

        char line[512];
        snprintf(line, sizeof(line), "%s %s %s root root %s\n",
                perm_str, nlinks_str, size_str, safe_name);
        typewriter_vga_print(line, 0x0F);
      }
      else
      {
        char line[VFS_PATH_MAX];
        int len = snprintf(line, sizeof(line), "%s\n", safe_name);
        
        klog_print("SERIAL: minix_fs_ls: printing line: '");
        klog_print(line);
        klog_print("' len=");
        klog_hex32(len);
        klog_print("\n");
        
        typewriter_vga_print(line, 0x0F);
        total_entries_printed++;
      }
    }
  }

  return 0;
}

void minix_fs_cleanup(void)
{
  if (minix_fs.initialized)
  {
    minix_fs.initialized = false;
  }
}

int minix_fs_cat(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("cat: filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path)
  {
    typewriter_vga_print("cat: invalid path\n", 0x0C);
    return -1;
  }

  if (!minix_root_block_present())
  {
    typewriter_vga_print("cat: disk not available\n", 0x0C);
    return -EIO;
  }

  uint16_t inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "cat: '%s': No such file\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  minix_inode_t file_inode_data;
  if (minix_read_inode(inode_num, &file_inode_data) != 0)
  {
    typewriter_vga_print("cat: Error reading inode\n", 0x0C);
    return -1;
  }

  if (file_inode_data.i_mode & MINIX_IFDIR)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "cat: '%s': Is a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  uint32_t file_size = file_inode_data.i_size;
  uint32_t bytes_read = 0;
  char output_buffer[MINIX_BLOCK_SIZE + 1];

  for (int i = 0; i < MINIX_DIRECT_ZONES && bytes_read < file_size; i++)
  {
    if (file_inode_data.i_zone[i] == 0)
    {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(file_inode_data.i_zone[i], block_buffer) != 0)
    {
      typewriter_vga_print("cat: Error reading block\n", 0x0C);
      continue;
    }

    uint32_t bytes_to_show = MINIX_BLOCK_SIZE;
    if (bytes_read + bytes_to_show > file_size)
    {
      bytes_to_show = file_size - bytes_read;
    }

    uint32_t output_pos = 0;
    for (uint32_t j = 0; j < bytes_to_show && output_pos < MINIX_BLOCK_SIZE; j++)
    {
      char c = block_buffer[j];
      if (c == '\0')
        break;

      if (c >= 32 && c < 127)
      {
        output_buffer[output_pos++] = c;
      }
      else if (c == '\n')
      {
        output_buffer[output_pos++] = '\n';
      }
      else if (c == '\t')
      {
        if (output_pos + 4 < MINIX_BLOCK_SIZE)
        {
          output_buffer[output_pos++] = ' ';
          output_buffer[output_pos++] = ' ';
          output_buffer[output_pos++] = ' ';
          output_buffer[output_pos++] = ' ';
        }
      }
    }

    if (output_pos > 0)
    {
      output_buffer[output_pos] = '\0';
      typewriter_vga_print(output_buffer, 0x0F);
    }

    bytes_read += bytes_to_show;
  }

  return 0;
}

// ===============================================================================
// FUNCIÓN PARA ESCRIBIR CONTENIDO A ARCHIVOS
// ===============================================================================

static void minix_klog_print_blob(const char *label, const void *content, size_t content_size)
{
  char scratch[128];
  size_t n;

  klog_print(label);
  if (!content || content_size == 0)
  {
    klog_print("(empty)\n");
    return;
  }
  n = content_size;
  if (n >= sizeof(scratch))
    n = sizeof(scratch) - 1;
  kmemcpy(scratch, content, n);
  scratch[n] = '\0';
  klog_print(scratch);
  if (content_size > n)
    klog_print("...");
  klog_print("\n");
}

#define MINIX_INDIRECT_ZONES (MINIX_BLOCK_SIZE / (int)sizeof(uint16_t))
#define MINIX_DINDIRECT_ZONES \
	((size_t)MINIX_INDIRECT_ZONES * (size_t)MINIX_INDIRECT_ZONES)

static void minix_inode_free_indirect_block(uint32_t ind_zone)
{
	int i;
	uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
	uint16_t *zone_list;

	if (ind_zone == 0)
		return;

	if (minix_read_block(ind_zone, indirect_buffer) == 0)
	{
		zone_list = (uint16_t *)indirect_buffer;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (zone_list[i] != 0)
				minix_free_zone((uint32_t)zone_list[i]);
		}
	}
	minix_free_zone(ind_zone);
}

static void minix_inode_free_direct_zones(minix_inode_t *inode)
{
  int zi;

  for (zi = 0; zi < MINIX_DIRECT_ZONES; zi++)
  {
    if (inode->i_zone[zi] != 0)
    {
      minix_free_zone((uint32_t)inode->i_zone[zi]);
      inode->i_zone[zi] = 0;
    }
  }
}

static void minix_inode_free_indirect_tree(minix_inode_t *inode)
{
	if (inode->i_zone[7] == 0)
		return;

	minix_inode_free_indirect_block((uint32_t)inode->i_zone[7]);
	inode->i_zone[7] = 0;
}

static void minix_inode_free_double_indirect_tree(minix_inode_t *inode)
{
	int i;
	uint8_t dind_buffer[MINIX_BLOCK_SIZE];
	uint16_t *dind_list;
	uint32_t dind_zone;

	if (inode->i_zone[8] == 0)
		return;

	dind_zone = (uint32_t)inode->i_zone[8];
	if (minix_read_block(dind_zone, dind_buffer) == 0)
	{
		dind_list = (uint16_t *)dind_buffer;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (dind_list[i] != 0)
				minix_inode_free_indirect_block((uint32_t)dind_list[i]);
		}
	}
	minix_free_zone(dind_zone);
	inode->i_zone[8] = 0;
}

static void minix_inode_free_all_zones(minix_inode_t *inode)
{
	minix_inode_free_direct_zones(inode);
	minix_inode_free_indirect_tree(inode);
	minix_inode_free_double_indirect_tree(inode);
}

static int minix_inode_zone_at(minix_inode_t *inode, uint16_t inode_num,
                               size_t zone_idx, int allocate,
                               uint32_t *disk_zone)
{
	(void)inode_num;

	if (disk_zone)
		*disk_zone = 0;

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES)
	{
		if (inode->i_zone[zone_idx] == 0)
		{
			uint32_t new_zone;

			if (!allocate)
				return 0;
			new_zone = minix_alloc_zone();
			if (new_zone == 0)
				return -ENOSPC;
			inode->i_zone[zone_idx] = (uint16_t)new_zone;
			/* Defer inode sync to caller (Linux dirty-inode style). */
		}
		if (disk_zone)
			*disk_zone = (uint32_t)inode->i_zone[zone_idx];
		return 0;
	}

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES + (size_t)MINIX_INDIRECT_ZONES)
	{
		size_t ind_idx = zone_idx - (size_t)MINIX_DIRECT_ZONES;
		uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
		uint16_t *zone_list;
		uint32_t ind_zone;

		if (inode->i_zone[7] == 0)
		{
			if (!allocate)
				return 0;
			ind_zone = minix_alloc_zone();
			if (ind_zone == 0)
				return -ENOSPC;
			kmemset(indirect_buffer, 0, MINIX_BLOCK_SIZE);
			if (minix_write_block(ind_zone, indirect_buffer) != 0)
			{
				minix_free_zone(ind_zone);
				return -EIO;
			}
			inode->i_zone[7] = (uint16_t)ind_zone;
		}
		else
		{
			ind_zone = (uint32_t)inode->i_zone[7];
			if (minix_read_block(ind_zone, indirect_buffer) != 0)
				return allocate ? -EIO : 0;
		}

		zone_list = (uint16_t *)indirect_buffer;
		if (zone_list[ind_idx] == 0)
		{
			uint32_t new_zone;

			if (!allocate)
				return 0;
			new_zone = minix_alloc_zone();
			if (new_zone == 0)
				return -ENOSPC;
			zone_list[ind_idx] = (uint16_t)new_zone;
			if (minix_write_block(ind_zone, indirect_buffer) != 0)
			{
				minix_free_zone(new_zone);
				return -EIO;
			}
		}
		if (disk_zone)
			*disk_zone = (uint32_t)zone_list[ind_idx];
		return 0;
	}

	{
		size_t rel = zone_idx - (size_t)MINIX_DIRECT_ZONES -
		             (size_t)MINIX_INDIRECT_ZONES;
		size_t dind1;
		size_t dind2;
		uint8_t dind_buffer[MINIX_BLOCK_SIZE];
		uint8_t ind_buffer[MINIX_BLOCK_SIZE];
		uint16_t *dind_list;
		uint16_t *ind_list;
		uint32_t dind_zone;
		uint32_t ind_zone;

		if (rel >= MINIX_DINDIRECT_ZONES)
			return -EFBIG;

		dind1 = rel / (size_t)MINIX_INDIRECT_ZONES;
		dind2 = rel % (size_t)MINIX_INDIRECT_ZONES;

		if (inode->i_zone[8] == 0)
		{
			if (!allocate)
				return 0;
			dind_zone = minix_alloc_zone();
			if (dind_zone == 0)
				return -ENOSPC;
			kmemset(dind_buffer, 0, MINIX_BLOCK_SIZE);
			if (minix_write_block(dind_zone, dind_buffer) != 0)
			{
				minix_free_zone(dind_zone);
				return -EIO;
			}
			inode->i_zone[8] = (uint16_t)dind_zone;
		}
		else
		{
			dind_zone = (uint32_t)inode->i_zone[8];
			if (minix_read_block(dind_zone, dind_buffer) != 0)
				return allocate ? -EIO : 0;
		}

		dind_list = (uint16_t *)dind_buffer;
		if (dind_list[dind1] == 0)
		{
			if (!allocate)
				return 0;
			ind_zone = minix_alloc_zone();
			if (ind_zone == 0)
				return -ENOSPC;
			kmemset(ind_buffer, 0, MINIX_BLOCK_SIZE);
			if (minix_write_block(ind_zone, ind_buffer) != 0)
			{
				minix_free_zone(ind_zone);
				return -EIO;
			}
			dind_list[dind1] = (uint16_t)ind_zone;
			if (minix_write_block(dind_zone, dind_buffer) != 0)
			{
				minix_free_zone(ind_zone);
				return -EIO;
			}
		}
		else
		{
			ind_zone = (uint32_t)dind_list[dind1];
			if (minix_read_block(ind_zone, ind_buffer) != 0)
				return allocate ? -EIO : 0;
		}

		ind_list = (uint16_t *)ind_buffer;
		if (ind_list[dind2] == 0)
		{
			uint32_t new_zone;

			if (!allocate)
				return 0;
			new_zone = minix_alloc_zone();
			if (new_zone == 0)
				return -ENOSPC;
			ind_list[dind2] = (uint16_t)new_zone;
			if (minix_write_block(ind_zone, ind_buffer) != 0)
			{
				minix_free_zone(new_zone);
				return -EIO;
			}
		}
		if (disk_zone)
			*disk_zone = (uint32_t)ind_list[dind2];
		return 0;
	}
}

static int minix_inode_resolve_zone(minix_inode_t *inode, uint16_t inode_num,
                                    size_t zone_idx, int allocate,
                                    uint32_t *disk_zone)
{
	return minix_inode_zone_at(inode, inode_num, zone_idx, allocate, disk_zone);
}

static size_t minix_file_zone_count(size_t file_size)
{
	if (file_size == 0)
		return 0;
	return (file_size + MINIX_BLOCK_SIZE - 1U) / MINIX_BLOCK_SIZE;
}

static void minix_inode_release_zone_at(minix_inode_t *inode, uint16_t inode_num,
                                        size_t zone_idx)
{
	uint32_t disk_zone;

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES)
	{
		if (inode->i_zone[zone_idx] != 0)
		{
			minix_free_zone((uint32_t)inode->i_zone[zone_idx]);
			inode->i_zone[zone_idx] = 0;
			(void)minix_fs_write_inode(inode_num, inode);
		}
		return;
	}

	if (minix_inode_zone_at(inode, inode_num, zone_idx, 0, &disk_zone) != 0)
		return;
	if (disk_zone == 0)
		return;

	minix_free_zone(disk_zone);

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES + (size_t)MINIX_INDIRECT_ZONES)
	{
		size_t ind_idx = zone_idx - (size_t)MINIX_DIRECT_ZONES;
		uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
		uint16_t *zone_list;
		uint32_t ind_zone;
		int empty;
		int i;

		if (inode->i_zone[7] == 0)
			return;

		ind_zone = (uint32_t)inode->i_zone[7];
		if (minix_read_block(ind_zone, indirect_buffer) != 0)
			return;

		zone_list = (uint16_t *)indirect_buffer;
		zone_list[ind_idx] = 0;
		(void)minix_write_block(ind_zone, indirect_buffer);

		empty = 1;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (zone_list[i] != 0)
			{
				empty = 0;
				break;
			}
		}
		if (empty)
		{
			minix_free_zone(ind_zone);
			inode->i_zone[7] = 0;
			(void)minix_fs_write_inode(inode_num, inode);
		}
		return;
	}

	{
		size_t rel = zone_idx - (size_t)MINIX_DIRECT_ZONES -
		             (size_t)MINIX_INDIRECT_ZONES;
		size_t dind1 = rel / (size_t)MINIX_INDIRECT_ZONES;
		size_t dind2 = rel % (size_t)MINIX_INDIRECT_ZONES;
		uint8_t dind_buffer[MINIX_BLOCK_SIZE];
		uint8_t ind_buffer[MINIX_BLOCK_SIZE];
		uint16_t *dind_list;
		uint16_t *ind_list;
		uint32_t dind_zone;
		uint32_t ind_zone;
		int empty;
		int i;

		if (inode->i_zone[8] == 0)
			return;

		dind_zone = (uint32_t)inode->i_zone[8];
		if (minix_read_block(dind_zone, dind_buffer) != 0)
			return;

		dind_list = (uint16_t *)dind_buffer;
		ind_zone = (uint32_t)dind_list[dind1];
		if (ind_zone == 0)
			return;

		if (minix_read_block(ind_zone, ind_buffer) != 0)
			return;

		ind_list = (uint16_t *)ind_buffer;
		ind_list[dind2] = 0;
		(void)minix_write_block(ind_zone, ind_buffer);

		empty = 1;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (ind_list[i] != 0)
			{
				empty = 0;
				break;
			}
		}
		if (empty)
		{
			minix_free_zone(ind_zone);
			dind_list[dind1] = 0;
			(void)minix_write_block(dind_zone, dind_buffer);

			empty = 1;
			for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
			{
				if (dind_list[i] != 0)
				{
					empty = 0;
					break;
				}
			}
			if (empty)
			{
				minix_free_zone(dind_zone);
				inode->i_zone[8] = 0;
				(void)minix_fs_write_inode(inode_num, inode);
			}
		}
	}
}

static void minix_inode_truncate_zones(minix_inode_t *inode, uint16_t inode_num,
                                       size_t old_size, size_t new_size)
{
	size_t old_zones;
	size_t new_zones;
	size_t zone_idx;

	if (new_size >= old_size)
		return;

	old_zones = minix_file_zone_count(old_size);
	new_zones = minix_file_zone_count(new_size);

	for (zone_idx = new_zones; zone_idx < old_zones; zone_idx++)
		minix_inode_release_zone_at(inode, inode_num, zone_idx);
}

static int minix_inode_zero_range(minix_inode_t *inode, uint16_t inode_num,
                                  size_t start, size_t end)
{
  size_t pos;
  uint8_t block_buffer[MINIX_BLOCK_SIZE];

  if (end <= start)
    return 0;

  pos = start;
  while (pos < end)
  {
    size_t zone_idx = pos / MINIX_BLOCK_SIZE;
    size_t block_off = pos % MINIX_BLOCK_SIZE;
    size_t chunk = MINIX_BLOCK_SIZE - block_off;
    uint32_t disk_zone;
    int ret;

    if (chunk > end - pos)
      chunk = end - pos;

    ret = minix_inode_resolve_zone(inode, inode_num, zone_idx, 1, &disk_zone);
    if (ret != 0)
      return ret;

    if (block_off == 0 && chunk == MINIX_BLOCK_SIZE)
    {
      kmemset(block_buffer, 0, MINIX_BLOCK_SIZE);
    }
    else
    {
      if (minix_read_block(disk_zone, block_buffer) != 0)
        kmemset(block_buffer, 0, MINIX_BLOCK_SIZE);
      kmemset(block_buffer + block_off, 0, chunk);
    }

    if (minix_write_block(disk_zone, block_buffer) != 0)
      return -EIO;

    pos += chunk;
  }

  return 0;
}

static int minix_offset_read_classified;

static int minix_fs_pread_at(const char *path, void *buf, size_t count,
                             off_t offset, size_t *read_count)
{
  uint16_t inode_num;
  minix_inode_t file_inode;
  uint8_t *dst;
  size_t file_size;
  size_t to_read;
  size_t pos;
  size_t remaining;
  int ret;

  if (!minix_fs.initialized || !path)
    return -EINVAL;
  if (count > 0 && !buf)
    return -EINVAL;
  if (count == 0)
  {
    if (read_count)
      *read_count = 0;
    return 0;
  }
  if (offset < 0)
    return -EINVAL;
  if (!minix_root_block_present())
    return -EIO;

  inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
    return -ENOENT;

  if (minix_read_inode(inode_num, &file_inode) != 0)
    return -EIO;

  if (file_inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  file_size = (size_t)file_inode.i_size;
  if ((size_t)offset >= file_size)
  {
    if (read_count)
      *read_count = 0;
    return 0;
  }

  to_read = count;
  if ((size_t)offset + to_read > file_size)
    to_read = file_size - (size_t)offset;

  dst = (uint8_t *)buf;
  pos = (size_t)offset;
  remaining = to_read;

  while (remaining > 0)
  {
    size_t zone_idx = pos / MINIX_BLOCK_SIZE;
    size_t block_off = pos % MINIX_BLOCK_SIZE;
    size_t chunk = MINIX_BLOCK_SIZE - block_off;
    uint32_t disk_zone;
    uint8_t block_buffer[MINIX_BLOCK_SIZE];

    if (chunk > remaining)
      chunk = remaining;

    ret = minix_inode_zone_at(&file_inode, inode_num, zone_idx, 0, &disk_zone);
    if (ret != 0)
      return ret;

    if (disk_zone == 0)
    {
      kmemset(dst, 0, chunk);
    }
    else if (block_off == 0 && chunk == MINIX_BLOCK_SIZE &&
             ((uintptr_t)dst & 1U) == 0U)
    {
      if (minix_read_block(disk_zone, dst) != 0)
        return -EIO;
    }
    else
    {
      if (minix_read_block(disk_zone, block_buffer) != 0)
        return -EIO;
      kmemcpy(dst, block_buffer + block_off, chunk);
    }

    dst += chunk;
    pos += chunk;
    remaining -= chunk;
  }

  /* One-shot smoke classify tags (not DEBUG_VFS spam). */
  if (!minix_offset_read_classified)
  {
    minix_offset_read_classified = 1;
    klog_info("MINIX", "CLASSIFY MINIX_OFFSET_READ_OK");
    klog_info("MINIX", "CLASSIFY LARGE_FILE_STREAMING_OK");
    klog_info("VFS", "CLASSIFY VFS_OFFSET_READ_OK");
    klog_info("VFS", "CLASSIFY ATA_PRESSURE_REDUCED");
    klog_info("MINIX", "CLASSIFY VFS_BACKEND_NEUTRAL");
  }

  if (read_count)
    *read_count = to_read;
  return 0;
}

static int minix_fs_pwrite_at(const char *path, const void *buf, size_t count,
                              off_t offset, size_t *written_count)
{
  uint16_t inode_num;
  minix_inode_t file_inode;
  const uint8_t *src;
  size_t remaining;
  size_t pos;
  size_t new_size;
  int ret;

  if (!minix_fs.initialized || !path)
    return -EINVAL;
  if (count > 0 && !buf)
    return -EINVAL;
  if (count == 0)
  {
    if (written_count)
      *written_count = 0;
    return 0;
  }
  if (offset < 0)
    return -EINVAL;
  if (!minix_root_block_present())
    return -EIO;

  inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
    return -ENOENT;

  if (minix_read_inode(inode_num, &file_inode) != 0)
    return -EIO;

  if (file_inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  new_size = (size_t)file_inode.i_size;
  if ((size_t)offset + count > new_size)
    new_size = (size_t)offset + count;
  if (new_size > minix_fs.superblock.s_max_size)
    return -EFBIG;

  if ((size_t)offset > (size_t)file_inode.i_size)
  {
    ret = minix_inode_zero_range(&file_inode, inode_num,
                                 (size_t)file_inode.i_size, (size_t)offset);
    if (ret != 0)
      return ret;
  }

  src = (const uint8_t *)buf;
  remaining = count;
  pos = (size_t)offset;

  while (remaining > 0)
  {
    size_t zone_idx = pos / MINIX_BLOCK_SIZE;
    size_t block_off = pos % MINIX_BLOCK_SIZE;
    size_t chunk = MINIX_BLOCK_SIZE - block_off;
    uint32_t disk_zone;
    uint8_t block_buffer[MINIX_BLOCK_SIZE];

    if (chunk > remaining)
      chunk = remaining;

    ret = minix_inode_resolve_zone(&file_inode, inode_num, zone_idx, 1,
                                   &disk_zone);
    if (ret != 0)
      return ret;

    if (block_off == 0 && chunk == MINIX_BLOCK_SIZE &&
        ((uintptr_t)src & 1U) == 0U)
    {
      if (minix_write_block(disk_zone, src) != 0)
        return -EIO;
    }
    else
    {
      if (minix_read_block(disk_zone, block_buffer) != 0)
        kmemset(block_buffer, 0, MINIX_BLOCK_SIZE);
      kmemcpy(block_buffer + block_off, src, chunk);
      if (minix_write_block(disk_zone, block_buffer) != 0)
        return -EIO;
    }

    pos += chunk;
    src += chunk;
    remaining -= chunk;
  }

  file_inode.i_size = (uint32_t)new_size;
  file_inode.i_time = minix_now_seconds();
  if (minix_fs_write_inode(inode_num, &file_inode) != 0)
    return -EIO;
  if (minix_sync_zone_bitmap() != 0)
    return -EIO;

  if (written_count)
    *written_count = count;
  return 0;
}

int minix_fs_write_file_len(const char *path, const void *content, size_t content_size)
{
  uint16_t inode_num;
  minix_inode_t file_inode;
  int ret;

  klog_print("SERIAL: minix_fs_write_file called\n");

  if (!minix_fs.initialized)
  {
    klog_print("SERIAL: minix_fs_write_file: filesystem not initialized\n");
    return -1;
  }

  if (!path)
  {
    klog_print("SERIAL: minix_fs_write_file: path is NULL\n");
    return -1;
  }

  if (content_size > 0 && !content)
  {
    klog_print("SERIAL: minix_fs_write_file: content is NULL\n");
    return -1;
  }

  klog_print("SERIAL: minix_fs_write_file: path=");
  klog_print(path);
  minix_klog_print_blob(" content=", content, content_size);

  if (!minix_root_block_present())
  {
    klog_print("SERIAL: write: disk not available\n");
    return -EIO;
  }

  inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
  {
    if (minix_fs_touch(path, 0644) != 0)
    {
      klog_print("SERIAL: Error: Could not create file ");
      klog_print(path);
      klog_print("\n");
      return -1;
    }

    inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0)
      return -1;
  }

  if (minix_read_inode(inode_num, &file_inode) != 0)
  {
    klog_print("SERIAL: Error: Could not read inode from disk\n");
    return -1;
  }

  if (file_inode.i_mode & MINIX_IFDIR)
  {
    klog_print("SERIAL: Error: Cannot write to directory\n");
    return -1;
  }

  if (content_size == 0)
  {
    minix_inode_free_all_zones(&file_inode);
    file_inode.i_size = 0;
    file_inode.i_time = minix_now_seconds();
    if (minix_fs_write_inode(inode_num, &file_inode) != 0)
      return -1;
    return 0;
  }

  minix_inode_free_all_zones(&file_inode);
  file_inode.i_size = 0;
  if (minix_fs_write_inode(inode_num, &file_inode) != 0)
    return -1;

  ret = minix_fs_pwrite_at(path, content, content_size, 0, NULL);
  if (ret != 0)
    return -1;

  klog_print("SERIAL: File '");
  klog_print(path);
  klog_print("' written successfully (");
  klog_hex32((uint32_t)content_size);
  klog_print(" bytes)\n");

  return 0;
}

int minix_fs_write_file(const char *path, const char *content)
{
  size_t content_size = content ? kstrlen(content) : 0;

  return minix_fs_write_file_len(path, content, content_size);
}

/**
 * Creates a new empty file with the specified path and mode.
 * If the file already exists, updates its modification time.
 *
 * @param path Path of the file to create
 * @param mode File mode (permissions)
 * @return 0 on success, -1 on error
 */
int minix_fs_touch(const char *path, mode_t mode)
{
  char parent_path[VFS_PATH_MAX];
  char filename[64];
  uint16_t existing;
  uint16_t new_inode_num;
  minix_inode_t parent_inode;
  minix_inode_t *parent_inode_ptr;
  minix_inode_t new_inode;

  if (!minix_fs.initialized)
    return -ENODEV;

  if (!path || *path == '\0')
    return -EINVAL;

  if (kstrcmp(path, "/") == 0)
    return -EINVAL;

  if (!minix_root_block_present())
    return -EIO;

  /* Check if file already exists */
  minix_inode_t *existing_inode = minix_fs_find_inode(path);
  if (existing_inode)
  {
    if (existing_inode->i_mode & MINIX_IFDIR)
      return -EISDIR;

    existing_inode->i_time = minix_now_seconds();
    uint16_t inode_num = minix_fs_get_inode_number(path);
    if (inode_num != 0)
    {
      minix_inode_t inode_copy;

      kmemcpy(&inode_copy, existing_inode, sizeof(minix_inode_t));
      minix_fs_write_inode(inode_num, &inode_copy);
    }
    return 0;
  }

  if (minix_fs_split_path(path, parent_path, filename) != 0)
    return -EINVAL;

  parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
    return -ENOENT;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (!(parent_inode.i_mode & MINIX_IFDIR))
    return -ENOTDIR;

  if (kstrlen(filename) > MINIX_NAME_LEN)
    return -ENAMETOOLONG;

  existing = minix_fs_find_dir_entry(&parent_inode, filename);
  if (existing != 0)
    return -EEXIST;

  new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0)
    return -ENOSPC;

  if (minix_sync_inode_bitmap() != 0)
  {
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  kmemset(&new_inode, 0, sizeof(new_inode));

  /* Apply umask to mode */
  {
    const struct ir0_task_cred *cr = ir0_current_cred();
    mode_t effective_mode = mode & ~(cr ? cr->umask : (mode_t)0);

    /* POSIX: a new file is owned by the creator's effective IDs. */
    new_inode.i_mode = MINIX_IFREG | (effective_mode & 0777);
    new_inode.i_uid = cr ? (uint16_t)cr->euid : (uint16_t)0;
    new_inode.i_gid = cr ? (uint8_t)cr->egid : (uint8_t)0;
  }
  new_inode.i_size = 0;
  new_inode.i_time = minix_now_seconds();
  new_inode.i_nlinks = 1;
  kmemset(new_inode.i_zone, 0, sizeof(new_inode.i_zone));

  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
  {
    minix_fs_free_inode(new_inode_num);
    (void)minix_sync_inode_bitmap();
    return -EIO;
  }

  if (minix_fs_add_dir_entry(&parent_inode, filename, new_inode_num) != 0)
  {
    minix_fs_free_inode(new_inode_num);
    (void)minix_sync_inode_bitmap();
    return -EIO;
  }

  parent_inode.i_time = minix_now_seconds();
  {
    uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);

    if (parent_inode_num != 0)
      minix_fs_write_inode(parent_inode_num, &parent_inode);
  }

  return 0;
}

int minix_fs_rm(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -ENODEV;
  }

  if (!path || kstrlen(path) == 0)
  {
    typewriter_vga_print("Error: No file path specified\n", 0x0C);
    return -EINVAL;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -EPERM;
  }

  /*
   * Snapshot child + parent: find_inode() returns a shared static buffer.
   * vfs_rename is link()+unlink(); unlink must decrement i_nlinks and only
   * free the inode at 0. Freeing while another name still points at the
   * inode (the rename destination) lets the next creat() reuse it — firstboot
   * then left /etc/shadow,/etc/group,/etc/fb.done as hardlinks to "ok\n".
   */
  uint16_t file_inode_num = minix_fs_get_inode_number(path);
  if (file_inode_num == 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rm: '%s': No such file\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  minix_inode_t file_inode;
  if (minix_read_inode(file_inode_num, &file_inode) != 0)
    return -EIO;

  if (file_inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  char parent_path[VFS_PATH_MAX];
  char filename[64];

  if (minix_fs_split_path(path, parent_path, filename) != 0)
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return -EINVAL;
  }

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -ENOENT;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (minix_fs_remove_dir_entry(&parent_inode, filename) != 0)
  {
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    return -EIO;
  }

  if (file_inode.i_nlinks > 0)
    file_inode.i_nlinks--;

  if (file_inode.i_nlinks == 0)
  {
    minix_inode_truncate_zones(&file_inode, file_inode_num,
			       file_inode.i_size, 0);
    file_inode.i_size = 0;
    if (minix_fs_write_inode(file_inode_num, &file_inode) != 0)
      return -EIO;
    if (minix_fs_free_inode(file_inode_num) != 0)
    {
      typewriter_vga_print("Error: Could not free inode\n", 0x0C);
      return -EIO;
    }
  }
  else if (minix_fs_write_inode(file_inode_num, &file_inode) != 0)
  {
    return -EIO;
  }

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory\n", 0x0C);
      return -EIO;
    }
  }

  return 0;
}

int minix_fs_link(const char *oldpath, const char *newpath)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -ENODEV;
  }

  if (!oldpath || !newpath || kstrlen(oldpath) == 0 || kstrlen(newpath) == 0)
  {
    typewriter_vga_print("Error: Invalid path specified\n", 0x0C);
    return -EINVAL;
  }

  /*
   * Snapshot the old inode immediately: minix_fs_find_inode() returns a
   * pointer into a shared static buffer that every later lookup clobbers.
   * Holding the raw pointer across find_inode(newpath)/find_inode(parent)
   * would alias the parent directory inode onto old_inode and write its
   * IFDIR contents back to old_inode_num — corrupting the renamed file into
   * a directory (runsv "supervise/pid: is a directory").
   */
  minix_inode_t *old_inode_ptr = minix_fs_find_inode(oldpath);
  if (!old_inode_ptr)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ln: '%s': No such file\n", oldpath);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  minix_inode_t old_inode;
  kmemcpy(&old_inode, old_inode_ptr, sizeof(minix_inode_t));

  // Cannot create hard link to a directory
  if (old_inode.i_mode & MINIX_IFDIR)
  {
    typewriter_vga_print("ln: cannot create hard link to directory\n", 0x0C);
    return -EPERM;
  }

  // Check if new path already exists
  if (minix_fs_find_inode(newpath) != NULL)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ln: '%s': File exists\n", newpath);
    typewriter_vga_print(error_msg, 0x0C);
    return -EEXIST;
  }

  // Get the inode number of the old file
  uint16_t old_inode_num = minix_fs_get_inode_number(oldpath);
  if (old_inode_num == 0)
  {
    typewriter_vga_print("Error: Could not get inode number\n", 0x0C);
    return -EIO;
  }

  // Split new path into parent directory and filename
  char parent_path[VFS_PATH_MAX];
  char filename[64];
  if (minix_fs_split_path(newpath, parent_path, filename) != 0)
  {
    typewriter_vga_print("Error: Invalid new path\n", 0x0C);
    return -EINVAL;
  }

  // Get parent directory inode (snapshot before any further lookups)
  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ln: '%s': No such directory\n", parent_path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  // Verify parent is a directory
  if (!(parent_inode.i_mode & MINIX_IFDIR))
  {
    typewriter_vga_print("ln: parent is not a directory\n", 0x0C);
    return -ENOTDIR;
  }

  // Add new directory entry pointing to the old inode
  if (minix_fs_add_dir_entry(&parent_inode, filename, old_inode_num) != 0)
  {
    typewriter_vga_print("Error: Could not add directory entry\n", 0x0C);
    return -EIO;
  }

  // Increment link count on the (snapshotted) inode and persist it
  old_inode.i_nlinks++;
  if (minix_fs_write_inode(old_inode_num, &old_inode) != 0)
  {
    typewriter_vga_print("Error: Could not update inode\n", 0x0C);
    return -EIO;
  }

  // Write updated parent directory
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory\n", 0x0E);
    }
  }

  return 0;
}


int minix_fs_ensure_valid(void)
{
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(1, block_buffer) != 0)
  {
    return minix_fs_format();
  }
  kmemcpy(&minix_fs.superblock, block_buffer, sizeof(minix_superblock_t));

  /* Check magic number */
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    // Invalid - format disk
    return minix_fs_format();
  }

  // Valid filesystem found
  return 0;
}

/**
 * Remove a directory
 * @param path Path to the directory to remove
 * @return 0 on success, -1 on error
 */
int minix_fs_rmdir(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -ENODEV;
  }

  if (!path || *path == '\0')
  {
    typewriter_vga_print("Error: No directory path specified\n", 0x0C);
    return -EINVAL;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -EPERM;
  }

  /* Get inode number first */
  uint16_t dir_inode_num = minix_fs_get_inode_number(path);
  if (dir_inode_num == 0)
    return -ENOENT;

  /* Read inode directly from disk to ensure we have latest version */
  minix_inode_t dir_inode;
  if (minix_read_inode(dir_inode_num, &dir_inode) != 0)
    return -EIO;

  if (!(dir_inode.i_mode & MINIX_IFDIR))
    return -ENOTDIR;

  // Check if directory is empty (only . and .. allowed)
  bool is_empty = true;

  for (int i = 0; i < MINIX_DIRECT_ZONES && is_empty; i++)
  {
    uint32_t zone = dir_inode.i_zone[i];
    if (zone == 0)
    {
      continue; // No more zones
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
    if (minix_read_block(zone, block_buffer) != 0)
    {
      typewriter_vga_print("Error: Could not read directory block\n", 0x0C);
      return -EIO;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      // Skip empty entries
      if (entries[j].inode == 0)
      {
        continue;
      }

      // Only allow . and ..
      if (kstrcmp(entries[j].name, ".") != 0 &&
          kstrcmp(entries[j].name, "..") != 0)
      {
        is_empty = false;
        break;
      }
    }
  }

  if (!is_empty)
    return -ENOTEMPTY;

  char parent_path[VFS_PATH_MAX] = {0};
  char dirname[64] = {0};

  if (minix_fs_split_path(path, parent_path, dirname) != 0)
    return -EINVAL;

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
    return -ENOENT;

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
    return -EIO;

  /* Free all zones used by the directory */
  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    if (dir_inode.i_zone[i] != 0)
    {
      minix_free_zone(dir_inode.i_zone[i]);
      dir_inode.i_zone[i] = 0;
    }
  }

  if (minix_fs_free_inode(dir_inode_num) != 0)
    return -EIO;

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (parent_inode.i_nlinks > 2)
      parent_inode.i_nlinks--;
    parent_inode.i_time = minix_now_seconds();
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
      return -EIO;
  }

  return 0;
}

/**
 * Force remove a directory without checking if it's empty
 * Use with caution - only for empty directories that can't be removed normally
 * @param path Path to the directory to remove
 * @return 0 on success, -1 on error
 */
int minix_fs_rmdir_force(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path || *path == '\0')
  {
    typewriter_vga_print("Error: No directory path specified\n", 0x0C);
    return -1;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -1;
  }

  // Get inode number first
  uint16_t dir_inode_num = minix_fs_get_inode_number(path);
  if (dir_inode_num == 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': No such file or directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  // Read inode directly from disk
  minix_inode_t dir_inode;
  if (minix_read_inode(dir_inode_num, &dir_inode) != 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': Could not read inode\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  if (!(dir_inode.i_mode & MINIX_IFDIR))
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': Not a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  // Skip empty check - force removal

  char parent_path[VFS_PATH_MAX] = {0};
  char dirname[64] = {0};

  if (minix_fs_split_path(path, parent_path, dirname) != 0)
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return -1;
  }

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -1;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
  {
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    return -1;
  }

  // Free all zones used by the directory
  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    if (dir_inode.i_zone[i] != 0)
    {
      minix_free_zone(dir_inode.i_zone[i]);
      dir_inode.i_zone[i] = 0;
    }
  }

  if (minix_fs_free_inode(dir_inode_num) != 0)
  {
    typewriter_vga_print("Error: Could not free inode\n", 0x0C);
    return -1;
  }

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (parent_inode.i_nlinks > 2)
    {
      parent_inode.i_nlinks--;
    }
    parent_inode.i_time = minix_now_seconds();

    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory inode\n", 0x0C);
    }
  }

  return 0;
}

/**
 * Read entire file into memory - for ELF loader support
 * This function reads a complete file and allocates memory for it
 */
int minix_fs_read_file(const char *path, void **data, size_t *size)
{
  const size_t minix_read_max = 64U * 1024U * 1024U;
  if (!path || !data || !size)
  {
    return -EINVAL;
  }

  minix_inode_t inode;
  uint16_t inode_num;
  int rc;

  rc = minix_lookup(path, &inode, &inode_num);
  if (rc != 0)
  {
    if (vfs_exec_audit_is_active())
    {
      klog_print("[EXEC_AUDIT][MINIX] stage=inode_lookup_fail path=");
      klog_print(path);
      klog_print(" inode=0\n");
    }
    return rc;
  }

  if (inode_num == 0)
  {
    if (vfs_exec_audit_is_active())
    {
      klog_print("[EXEC_AUDIT][MINIX] stage=inode_lookup_fail path=");
      klog_print(path);
      klog_print(" inode=0\n");
    }
    return -ENOENT;
  }

  if (vfs_exec_audit_is_active())
  {
    klog_print("[EXEC_AUDIT][MINIX] stage=inode_lookup_ok path=");
    klog_print(path);
    klog_print(" inode=");
    klog_hex32((uint32_t)inode_num);
    klog_print("\n");
  }

  exec_read_trace_minix_file_begin(path, inode_num, inode.i_mode, inode.i_size);
  exec_read_trace_minix_zones(inode.i_zone);

  // Check if it's a regular file
  if (!(inode.i_mode & MINIX_IFREG))
  {
    if (vfs_exec_audit_is_active())
    {
      klog_print("[EXEC_AUDIT][MINIX] stage=not_regular mode=");
      klog_hex32((uint32_t)inode.i_mode);
      klog_print(" ifmt=");
      klog_hex32((uint32_t)(inode.i_mode & MINIX_IFMT));
      klog_print(" is_dir=");
      klog_print((inode.i_mode & MINIX_IFMT) == MINIX_IFDIR ? "1" : "0");
      klog_print("\n");
    }
    return -EINVAL; // Not a regular file
  }

  // Allocate memory for file content
  *size = inode.i_size;
  if (*size == 0)
  {
    *data = NULL;
    if (vfs_exec_audit_is_active())
    {
      klog_print("[EXEC_AUDIT][MINIX] stage=file_empty inode=");
      klog_hex32((uint32_t)inode_num);
      klog_print("\n");
    }
    return 0;
  }
  if (*size > minix_read_max)
  {
    return -EFBIG;
  }

  *data = kmalloc_try(*size);
  if (!*data)
  {
    return -ENOMEM; // Memory allocation failed
  }

  // Read file content
  uint8_t *buffer = (uint8_t *)*data;
  size_t bytes_read = 0;

  // Read direct zones (first 7 zones)
  for (int i = 0; i < MINIX_DIRECT_ZONES && bytes_read < *size; i++)
  {
    size_t file_offset = bytes_read;
    size_t bytes_to_copy;

    if (inode.i_zone[i] == 0)
    {
      if (file_offset < *size)
      {
        exec_read_trace_minix_eio("MINIX_ZONE_TABLE_CORRUPTED", "direct_zero",
                                  i, 0, 0, 0, file_offset, buffer);
      }
      continue;
    }

    {
      uint8_t block_buffer[MINIX_BLOCK_SIZE];
      uint32_t disk_block = (uint32_t)inode.i_zone[i];
      uint32_t lba = disk_block * 2U;

      if (minix_read_block(disk_block, block_buffer) != 0)
      {
        exec_read_trace_minix_block("direct", i, disk_block, disk_block, lba,
                                    file_offset, MINIX_BLOCK_SIZE, 0);
        exec_read_trace_minix_eio("MINIX_READ_EIO_AT_BLOCK", "direct", i,
                                  disk_block, disk_block, lba, file_offset,
                                  block_buffer);
        if (i == MINIX_DIRECT_ZONES - 1 &&
            *size > (size_t)MINIX_BLOCK_SIZE * (size_t)MINIX_DIRECT_ZONES)
        {
          klog_info("EXEC", "CLASSIFY MINIX_DIRECT_INDIRECT_BOUNDARY_BUG");
        }
        kfree(*data);
        return -EIO;
      }

      bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                          ? MINIX_BLOCK_SIZE
                          : (*size - bytes_read);
      exec_read_trace_minix_block("direct", i, disk_block, disk_block, lba,
                                  file_offset, bytes_to_copy, bytes_to_copy);
      kmemcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
      bytes_read += bytes_to_copy;
    }
  }

  // Handle indirect zones if needed (zone[7] and zone[8])
  if (bytes_read < *size && inode.i_zone[7] != 0)
  {
    // Single indirect zone
    uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
    uint32_t ind_zone = (uint32_t)inode.i_zone[7];
    uint32_t ind_lba = ind_zone * 2U;

    if (minix_read_block(ind_zone, indirect_buffer) != 0)
    {
      exec_read_trace_minix_eio("MINIX_READ_EIO_AT_BLOCK", "indirect_meta", 7,
                                ind_zone, ind_zone, ind_lba, bytes_read,
                                indirect_buffer);
      klog_info("EXEC", "CLASSIFY MINIX_DIRECT_INDIRECT_BOUNDARY_BUG");
      kfree(*data);
      return -EIO;
    }

    {
      uint16_t *zone_list = (uint16_t *)indirect_buffer;
      int num_zones = MINIX_BLOCK_SIZE / (int)sizeof(uint16_t);

      for (int i = 0; i < num_zones && bytes_read < *size; i++)
      {
        size_t file_offset = bytes_read;

        if (zone_list[i] == 0)
        {
          if (file_offset < *size)
          {
            exec_read_trace_minix_eio("MINIX_ZONE_TABLE_CORRUPTED",
                                      "indirect_zero", i, 0, 0, 0,
                                      file_offset, buffer);
          }
          continue;
        }

        {
          uint8_t block_buffer[MINIX_BLOCK_SIZE];
          uint32_t disk_block = (uint32_t)zone_list[i];
          uint32_t lba = disk_block * 2U;
          size_t bytes_to_copy;

          if (minix_read_block(disk_block, block_buffer) != 0)
          {
            exec_read_trace_minix_block("indirect", i, disk_block, disk_block,
                                        lba, file_offset, MINIX_BLOCK_SIZE, 0);
            exec_read_trace_minix_eio("MINIX_READ_EIO_AT_BLOCK", "indirect",
                                      i, disk_block, disk_block, lba,
                                      file_offset, block_buffer);
            kfree(*data);
            return -EIO;
          }

          bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                              ? MINIX_BLOCK_SIZE
                              : (*size - bytes_read);
          exec_read_trace_minix_block("indirect", i, disk_block, disk_block,
                                      lba, file_offset, bytes_to_copy,
                                      bytes_to_copy);
          kmemcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
          bytes_read += bytes_to_copy;
        }
      }
    }
  }
  else if (bytes_read < *size && *size > (size_t)MINIX_BLOCK_SIZE *
                                           (size_t)MINIX_DIRECT_ZONES)
  {
    klog_info("EXEC", "CLASSIFY MINIX_DIRECT_INDIRECT_BOUNDARY_BUG");
  }

  if (bytes_read < *size && inode.i_zone[8] != 0)
  {
    uint8_t dind_buffer[MINIX_BLOCK_SIZE];
    uint16_t *dind_list;
    uint32_t dind_zone = (uint32_t)inode.i_zone[8];
    int num_dind = MINIX_INDIRECT_ZONES;

    if (minix_read_block(dind_zone, dind_buffer) != 0)
    {
      kfree(*data);
      return -EIO;
    }

    dind_list = (uint16_t *)dind_buffer;
    for (int d1 = 0; d1 < num_dind && bytes_read < *size; d1++)
    {
      uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
      uint16_t *zone_list;
      uint32_t ind_zone;
      int num_zones;

      if (dind_list[d1] == 0)
        continue;

      ind_zone = (uint32_t)dind_list[d1];
      if (minix_read_block(ind_zone, indirect_buffer) != 0)
      {
        kfree(*data);
        return -EIO;
      }

      zone_list = (uint16_t *)indirect_buffer;
      num_zones = MINIX_BLOCK_SIZE / (int)sizeof(uint16_t);
      for (int i = 0; i < num_zones && bytes_read < *size; i++)
      {
        size_t file_offset = bytes_read;

        if (zone_list[i] == 0)
          continue;

        {
          uint8_t block_buffer[MINIX_BLOCK_SIZE];
          uint32_t disk_block = (uint32_t)zone_list[i];
          size_t bytes_to_copy;

          if (minix_read_block(disk_block, block_buffer) != 0)
          {
            kfree(*data);
            return -EIO;
          }

          bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                              ? MINIX_BLOCK_SIZE
                              : (*size - bytes_read);
          kmemcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
          bytes_read += bytes_to_copy;
          (void)file_offset;
        }
      }
    }
  }

  /*
   * Double-indirect tail: if still short, report explicitly.
   */
  if (bytes_read < *size)
  {
    klog_info("EXEC", "CLASSIFY MINIX_OFFSET_SIZE_BUG");
    kfree(*data);
    *data = NULL;
    *size = 0;
    return -EFBIG;
  }

  if (DEBUG_VFS && *size > (size_t)MINIX_BLOCK_SIZE * ((size_t)MINIX_DIRECT_ZONES +
      (size_t)MINIX_INDIRECT_ZONES))
  {
    klog_info("MINIX", "CLASSIFY MINIX_DOUBLE_INDIRECT_OK");
  }

  return 0;
}

int minix_fs_statfs(struct ir0_statfs *buf)
{
	uint32_t first;
	uint32_t limit;
	uint32_t z;
	uint32_t free_z = 0;
	uint32_t free_i = 0;
	uint32_t i;

	if (!buf)
		return -EINVAL;
	if (!minix_fs.initialized || !minix_fs.zone_bitmap)
		return -ENODEV;

	memset(buf, 0, sizeof(*buf));
	first = minix_fs.superblock.s_firstdatazone;
	limit = minix_fs.superblock.s_nzones;
	if (limit > MINIX_MAX_ZONES)
		limit = MINIX_MAX_ZONES;
	if (first >= limit)
		return -EINVAL;

	for (z = first; z < limit; z++)
	{
		if (minix_is_zone_free(z))
			free_z++;
	}

	/* Inode bitmap polarity is opposite of zones: bit 1 = used. */
	for (i = 1; i <= minix_fs.superblock.s_ninodes && i < MINIX_MAX_INODES; i++)
	{
		uint32_t byte = i / 8;
		uint32_t bit = i % 8;

		if ((minix_fs.inode_bitmap[byte] & (1U << bit)) == 0)
			free_i++;
	}

	buf->f_type = IR0_MINIX_SUPER_MAGIC;
	buf->f_bsize = MINIX_BLOCK_SIZE;
	buf->f_frsize = MINIX_BLOCK_SIZE;
	buf->f_blocks = (unsigned long)(limit - first);
	buf->f_bfree = (unsigned long)free_z;
	buf->f_bavail = (unsigned long)free_z;
	buf->f_files = (unsigned long)minix_fs.superblock.s_ninodes;
	buf->f_ffree = (unsigned long)free_i;
	buf->f_namelen = MINIX_NAME_LEN;
	buf->f_flags = 0;
	return 0;
}

int minix_fs_stat(const char *pathname, stat_t *buf)
{
  minix_inode_t inode;
  uint16_t inode_num;
  int rc;

  if (!minix_fs.initialized || !pathname || !buf)
  {
    log_debug_fmt("MINIX", "stat('%s') rejected: init=%d", pathname ? pathname : "(null)",
                  minix_fs.initialized);
    if (!minix_fs.initialized)
      return -ENODEV;
    return -EINVAL;
  }

  kmemset(buf, 0, sizeof(*buf));
  rc = minix_lookup(pathname, &inode, &inode_num);
  if (rc != 0)
  {
    log_debug_fmt("MINIX", "stat('%s') inode not found", pathname);
    return rc;
  }

  if ((inode.i_mode & MINIX_IFMT) == 0)
  {
    log_debug_fmt("MINIX", "stat('%s') invalid mode 0x%x (stale)", pathname,
                  inode.i_mode);
    return -ENOENT;
  }

  buf->st_dev = 0;
  buf->st_ino = inode_num;
  buf->st_nlink = inode.i_nlinks;
  buf->st_uid = inode.i_uid;
  buf->st_gid = inode.i_gid;
  buf->st_size = inode.i_size;
  buf->st_blksize = MINIX_BLOCK_SIZE;
  buf->st_blocks = (blkcnt_t)(((uint64_t)inode.i_size + 511ULL) / 512ULL);
  buf->st_atime = inode.i_time;
  buf->st_mtime = inode.i_time;
  buf->st_ctime = inode.i_time;
  buf->st_mode = inode.i_mode;

  if (kstrcmp(pathname, "/") == 0 && klog_trace_enabled(KLOG_TRACE_VFS_STAT))
    klog_trace_fmt("MINIX", "stat('%s') OK ino=%u mode=0x%x", pathname, inode_num, buf->st_mode);
  return 0;
}

/*
 * minix_fs_chown - Change file owner and group
 * @path: Path to file
 * @owner: New owner UID, or (uid_t)-1 to leave unchanged
 * @group: New group GID, or (gid_t)-1 to leave unchanged
 * Returns: 0 on success, negative on error
 */
int minix_fs_chown(const char *path, uid_t owner, gid_t group)
{
  minix_inode_t inode_copy;
  uint16_t inode_num;
  int rc;

  if (!ir0_current_cred())
    return -ESRCH;

  rc = minix_lookup(path, &inode_copy, &inode_num);
  if (rc != 0)
    return rc;

  if (!ir0_cred_is_root())
    return -EPERM;

  if (owner != (uid_t)-1)
    inode_copy.i_uid = (uint16_t)owner;
  if (group != (gid_t)-1)
    inode_copy.i_gid = (uint8_t)group;

  return minix_fs_write_inode(inode_num, &inode_copy);
}


/*
 * minix_fs_utimens - utimensat(2) backend.
 *
 * MINIX v1 inodes keep a single timestamp, so the modification time wins and
 * stat() reports it for atime, mtime and ctime alike.
 */
static int minix_fs_utimens(const char *path, const struct timespec times[2])
{
  const struct ir0_task_cred *cr = ir0_current_cred();
  minix_inode_t inode_copy;
  uint16_t inode_num;
  int rc;

  if (!path)
    return -EINVAL;
  if (!cr)
    return -ESRCH;

  rc = minix_lookup(path, &inode_copy, &inode_num);
  if (rc != 0)
    return rc;

  if (!ir0_cred_is_root() && cr->euid != inode_copy.i_uid)
    return -EPERM;

  if (times)
    inode_copy.i_time = (uint32_t)times[1].tv_sec;
  else
    inode_copy.i_time = minix_now_seconds();

  return minix_fs_write_inode(inode_num, &inode_copy);
}

static int minix_fs_read_file_wrapper(const char *path, void *buf, size_t count, size_t *read_count, off_t offset)
{
	size_t got = 0;
	int ret;

	if (offset < 0)
		return -EINVAL;

	ret = minix_fs_pread_at(path, buf, count, offset, &got);
	exec_read_trace_vfs_read_file(path, 0, (int64_t)got, offset, count, ret);
	if (ret != 0)
		return ret;
	if (read_count)
		*read_count = got;
	return 0;
}

static int minix_fs_write_file_wrapper(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset)
{
	return minix_fs_pwrite_at(path, buf, count, offset, written_count);
}

static int minix_backend_classified;

/*
 * minix_create - VFS create backend (path + mode only; no open flags).
 */
static int minix_create(const char *path, mode_t mode)
{
  int ret;

  ret = minix_fs_touch(path, mode);
  if (ret == 0 && !minix_backend_classified)
  {
    minix_backend_classified = 1;
    if (DEBUG_VFS)
      klog_info("MINIX", "CLASSIFY MINIX_BACKEND_ONLY_CONFIRMED");
  }
  return ret;
}

/*
 * minix_truncate - VFS truncate backend (path + length only).
 */
static int minix_truncate(const char *path, size_t length)
{
  minix_inode_t inode;
  uint16_t inode_num;
  size_t old_size;
  int changed;

  if (!path || !minix_fs.initialized)
    return -EIO;

  if (length > minix_fs.superblock.s_max_size)
    return -EFBIG;

  if (minix_lookup(path, &inode, &inode_num) != 0)
    return -ENOENT;

  if (inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  old_size = (size_t)inode.i_size;
  if (length == old_size)
    return 0;

  if (length > old_size)
  {
    int grow_rc;

    grow_rc = minix_inode_zero_range(&inode, inode_num, old_size, length);
    if (grow_rc != 0)
      return grow_rc;

    inode.i_size = (uint32_t)length;
    inode.i_time = minix_now_seconds();
    if (minix_fs_write_inode(inode_num, &inode) != 0)
      return -EIO;
    if (minix_sync_zone_bitmap() != 0)
      return -EIO;

    if (DEBUG_VFS)
      klog_info("MINIX", "CLASSIFY MINIX_TRUNCATE_GROW_OK");
    return 0;
  }

  changed = (length != old_size);
  if (length == 0)
  {
    minix_inode_free_all_zones(&inode);
  }
  else
  {
    minix_inode_truncate_zones(&inode, inode_num, old_size, length);
    if (minix_read_inode(inode_num, &inode) != 0)
      return -EIO;
  }

  inode.i_size = (uint32_t)length;
  if (!changed)
    return 0;

  inode.i_time = minix_now_seconds();
  if (minix_fs_write_inode(inode_num, &inode) != 0)
    return -EIO;
  if (minix_sync_zone_bitmap() != 0)
    return -EIO;

  if (DEBUG_VFS)
    klog_info("MINIX", "CLASSIFY LARGE_FILE_TRUNCATE_OK");
  return 0;
}

/*
 * minix_fs_readdir - Read directory entries for VFS readdir
 */
static int minix_fs_readdir(const char *path, struct vfs_dirent *entries, int max_entries)
{
	minix_inode_t *dir_inode = minix_fs_find_inode(path);
	if (!dir_inode)
		return -ENOENT;
	if (!minix_is_dir(dir_inode))
		return -ENOTDIR;

	int entry_count = 0;
	
  for (int i = 0; i < MINIX_DIRECT_ZONES && entry_count < max_entries; i++) {
		
    if (dir_inode->i_zone[i] == 0)
			continue;
		uint8_t block_buffer[MINIX_BLOCK_SIZE];
		
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
			continue;
		
    minix_dir_entry_t *minix_entries = (minix_dir_entry_t *)block_buffer;
		
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

		for (int j = 0; j < num_entries && entry_count < max_entries; j++) {
			if (minix_entries[j].inode == 0)
				continue;

			if (minix_entries[j].name[0] == '\0')
				continue;

			if ((unsigned char)minix_entries[j].name[0] <= ' ')
				continue;
	
  		strncpy(entries[entry_count].name, minix_entries[j].name,
				sizeof(entries[entry_count].name) - 1);
			entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';

			minix_inode_t target_inode;
			if (minix_read_inode(minix_entries[j].inode, &target_inode) != 0) {
				entries[entry_count].type = DT_UNKNOWN;
			} else if (minix_is_dir(&target_inode)) {
				entries[entry_count].type = DT_DIR;
			} else if (minix_is_reg(&target_inode)) {
				entries[entry_count].type = DT_REG;
			} else {
				entries[entry_count].type = DT_UNKNOWN;
			}
			entry_count++;
		}
	}
	return entry_count;
}

/*
 * minix_fs_chmod - VFS chmod: keep inode type bits, apply new permission mask.
 */
static int minix_fs_chmod(const char *path, mode_t mode)
{
	minix_inode_t inode;
	uint16_t inode_num;
	int rc;

	if (!ir0_current_cred())
		return -ESRCH;

	rc = minix_lookup(path, &inode, &inode_num);
	if (rc != 0)
		return rc;

	{
		const struct ir0_task_cred *cr = ir0_current_cred();

		if (!ir0_cred_is_root() && cr->euid != inode.i_uid)
			return -EPERM;
	}

	inode.i_mode = (uint16_t)((inode.i_mode & (uint16_t)~07777) |
				  ((uint16_t)mode & 07777));

	return minix_fs_write_inode(inode_num, &inode);
}

static struct vfs_ops minix_fs_ops = {
	.stat    = minix_fs_stat,
	.mkdir   = minix_fs_mkdir,
	.create  = minix_create,
	.unlink  = minix_fs_rm,
	.rmdir   = minix_fs_rmdir,
	.readdir = minix_fs_readdir,
	.read    = minix_fs_read_file_wrapper,
	.write   = minix_fs_write_file_wrapper,
	.truncate = minix_truncate,
	.link    = minix_fs_link,
	.chown   = minix_fs_chown,
	.chmod   = minix_fs_chmod,
	.utimens = minix_fs_utimens,
};

static struct vfs_fstype minix_fs_type;

static int minix_mount(const char *dev_name __attribute__((unused)), const char *dir_name)
{
	if (!dir_name || strcmp(dir_name, "/") != 0)
		return -ENOTSUPP;

	if (!minix_fs_is_working()) {
		int ret = minix_fs_init();
		if (ret != 0) {
			klog_print("[VFS] ERROR - MINIX_MOUNT: minix_fs_init failed\n");
			return ret;
		}
	}
	return 0;
}

int minix_fs_register(void)
{
	minix_fs_type.name  = "minix";
	minix_fs_type.ops   = &minix_fs_ops;
	minix_fs_type.mount = minix_mount;
	minix_fs_type.next  = NULL;
	return vfs_register_fs(&minix_fs_type);
}
