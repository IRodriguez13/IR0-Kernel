/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: virtio_9p.h
 * Description: Facade for QEMU virtio-9p host directory share (9P2000.L).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define HOSTSHARE_MOUNT_TAG "ir0share"
#define HOSTSHARE_MOUNT_PATH "/mnt/host"
#define HOSTSHARE_PAYLOAD_NAME "ir0_payload"

#define VIRTIO_9P_NAME_MAX 255

typedef struct virtio_9p_dirent
{
	char name[VIRTIO_9P_NAME_MAX + 1];
	uint8_t type; /* DT_DIR=4, DT_REG=8 */
} virtio_9p_dirent_t;

/* Subset of Rstatfs that df needs; fsid is skipped. */
struct virtio_9p_statfs
{
	uint32_t type;
	uint32_t bsize;
	uint64_t blocks;
	uint64_t bfree;
	uint64_t bavail;
	uint64_t files;
	uint64_t ffree;
	uint32_t namelen;
};

int virtio_9p_init(void);
int virtio_9p_statfs(struct virtio_9p_statfs *out);
int virtio_9p_ready(void);
int virtio_9p_stat_file(const char *relpath, uint64_t *size_out, uint32_t *mode_out);
int virtio_9p_write_file(const char *relpath, const void *buf, size_t len);
int virtio_9p_write_at(const char *relpath, const void *buf, size_t len, uint64_t offset);
int virtio_9p_truncate(const char *relpath, uint64_t length);
int virtio_9p_read_file(const char *relpath, void *buf, size_t maxlen);
int virtio_9p_read_at(const char *relpath, void *buf, size_t count, uint64_t offset,
		      size_t *got_out);
int virtio_9p_mkdir(const char *relpath, uint32_t mode);
int virtio_9p_unlink(const char *relpath, int is_dir);
int virtio_9p_rename(const char *oldpath, const char *newpath);
int virtio_9p_link(const char *oldpath, const char *newpath);
int virtio_9p_symlink(const char *linkpath, const char *target);
int virtio_9p_readlink(const char *relpath, char *buf, size_t buflen);
int virtio_9p_readdir(const char *relpath, virtio_9p_dirent_t *entries, int max);
