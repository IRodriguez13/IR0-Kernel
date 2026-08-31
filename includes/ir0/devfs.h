/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: devfs.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <ir0/types.h>
#include <ir0/stat.h>

// Virtual Device Filesystem - /dev
// Implements Unix "everything is a file" for devices

struct vfs_dirent;

typedef struct {
    const char *name;
    uint32_t mode;        // File permissions
    uint32_t device_id;   // Device identifier
    /*
     * Linux (major,minor) for nodes that have a canonical upstream number,
     * encoded with IR0_MKDEV; 0 when the node is IR0-specific. Distinct from
     * device_id, which is an internal handle with no upstream meaning: this
     * is what lets `mknod path c 1 3` resolve to /dev/null.
     */
    uint32_t rdev;
    void *driver_data;    // Driver-specific data
} devfs_entry_t;

// Device operations - polymorphic interface
typedef struct {
    int64_t (*read)(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
    int64_t (*write)(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
    int64_t (*ioctl)(devfs_entry_t *entry, uint64_t request, void *arg);
    int64_t (*open)(devfs_entry_t *entry, int flags);
    int64_t (*close)(devfs_entry_t *entry);
    int (*can_read)(devfs_entry_t *entry, pid_t pid);
    int (*can_write)(devfs_entry_t *entry, pid_t pid);
} devfs_ops_t;

// Device node structure
typedef struct {
    devfs_entry_t entry;
    const devfs_ops_t *ops;
    uint64_t ref_count;
} devfs_node_t;

// Standard device nodes
extern devfs_node_t dev_null;
extern devfs_node_t dev_zero;
extern devfs_node_t dev_console;
extern devfs_node_t dev_tty;
extern devfs_node_t dev_audio;
extern devfs_node_t dev_mouse;
extern devfs_node_t dev_net;
extern devfs_node_t dev_disk;
extern devfs_node_t dev_kmsg;

/*
 * Finite text snapshots for /dev/net and /dev/kmsg (per-open, refcounted).
 * Captured at open; read honors offset and returns 0 at EOF.
 */
typedef struct devfs_text_snap
{
	char *buf;
	size_t len;
	int refs;
} devfs_text_snap_t;

#define DEVFS_ID_KMSG 5u
#define DEVFS_ID_NET  8u

int devfs_node_wants_text_snap(uint32_t device_id);
devfs_text_snap_t *devfs_text_snap_capture(uint32_t device_id);
void devfs_text_snap_acquire(devfs_text_snap_t *snap);
void devfs_text_snap_release(devfs_text_snap_t *snap);
int64_t devfs_text_snap_read(const devfs_text_snap_t *snap, void *buf,
			     size_t count, off_t offset);

/* Device filesystem management */
int devfs_init(void);
devfs_node_t *devfs_find_node(const char *path);
devfs_node_t *devfs_find_node_by_id(uint32_t device_id);
/* Node carrying a given Linux (major,minor); NULL when none claims it. */
devfs_node_t *devfs_find_node_by_rdev(uint32_t rdev);
int devfs_register_node(devfs_node_t *node);
int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode);
int devfs_unregister_device(const char *name);
int devfs_fd_can_read(uint32_t device_id, pid_t pid);
int devfs_fd_can_write(uint32_t device_id, pid_t pid);
int64_t devfs_open_node(devfs_node_t *node, int flags);
int64_t devfs_close_node(devfs_node_t *node);
int devfs_stat_path(const char *path, stat_t *buf);
int devfs_is_virtual_subdir(const char *path);
int devfs_readdir_root(struct vfs_dirent *entries, int max_entries);
int devfs_readdir_subdir(const char *path, struct vfs_dirent *entries,
                         int max_entries);
/* Per-syscall: O_NONBLOCK for shared console/tty read ops. */
void devfs_set_read_nonblock(int nonblock);

// Standard device implementations
int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

int64_t dev_console_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

// Audio device operations
int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// Mouse device operations  
int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// Network device operations
int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// Disk device operations
int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// IOCTL requests for different devices
#define AUDIO_SET_VOLUME    0x1001
#define AUDIO_GET_VOLUME    0x1002
#define AUDIO_PLAY          0x1003
#define AUDIO_STOP          0x1004
#define AUDIO_SET_FORMAT    0x1005
#define AUDIO_GET_FORMAT    0x1006

/* PCM format for Doom compatibility (11025 Hz, 8-bit mono default) */
struct audio_format {
    uint32_t sample_rate;   /* 4000-45454 Hz, Doom uses 11025 */
    uint8_t channels;      /* 1=mono, 2=stereo */
    uint8_t bits_per_sample; /* 8 or 16 */
};

#define MOUSE_GET_STATE     0x2001
#define MOUSE_SET_SENSITIVITY 0x2002

#define NET_SEND_PING       0x3001
#define NET_GET_CONFIG      0x3002
#define NET_SET_CONFIG      0x3003
#define NET_GET_PING_RESULT 0x3004

#define DISK_READ_SECTOR    0x4001
#define DISK_WRITE_SECTOR   0x4002
#define DISK_GET_GEOMETRY   0x4003

#define IPC_CREATE_CHANNEL  0x5001
#define IPC_DESTROY_CHANNEL 0x5002
#define IPC_GET_CHANNEL_ID  0x5003

/* Framebuffer (OSDev /dev/fb0, Linux-compatible ioctl) */
#define FBIOGET_VSCREENINFO 0x4600  /* Linux uapi/fb.h */
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOGETCMAP         0x4604
#define FBIOPUTCMAP         0x4605
#define FBIOPAN_DISPLAY     0x4606
#define FBIOBLANK           0x4611

/* Linux uapi/linux/fb.h */
struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres;           /* visible resolution */
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

/* Linux fb_fix_screeninfo (for mmap: line_length, smem_len) */
struct fb_fix_screeninfo {
    char id[16];
    unsigned long smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    unsigned long mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR 2
#define FB_ACCEL_NONE 0

/* Minimal fb info for IR0 (subset of above) */
struct fb_info_min {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
};
