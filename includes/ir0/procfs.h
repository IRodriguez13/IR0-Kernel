/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: procfs.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Simple /proc filesystem
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.h
 * Description: Minimal /proc filesystem - on-demand, no mounting
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/vfs.h>

/* /proc entry types */
typedef enum {
    PROC_TYPE_FILE,
    PROC_TYPE_DIR
} proc_type_t;

/* /proc entry structure
 *
 * Legacy note: current dispatch in fs/procfs.c uses FD-based open/read/write
 * handlers, not a populated proc_entry tree. New endpoints should register
 * through the internal procfs table helpers in fs/procfs.c until tree dispatch
 * is unified with devfs_ops_t-style registration.
 */
typedef struct proc_entry {
    char name[64];
    proc_type_t type;
    int (*read_func)(char *buf, size_t count);
    int (*write_func)(const char *buf, size_t count);
    struct proc_entry *children;
    int child_count;
} proc_entry_t;

/* /proc filesystem interface */
bool is_proc_path(const char *path);
int proc_open(const char *path, int flags);
int proc_read(int fd, char *buf, size_t count, off_t offset);
int proc_write(int fd, const char *buf, size_t count);
int proc_stat(const char *path, stat_t *st);

/* Offset management for /proc files */
off_t proc_get_offset(int fd);
void proc_set_offset(int fd, off_t offset);

/* /proc/pid directory — legacy fd-based (deprecated; prefer proc_readdir). */
int proc_getdents(int fd, void *dirent_buf, size_t count);

/* Path-based readdir for /proc, /proc/pid, /proc/pid/N (vfs_dirent batch). */
int proc_readdir(const char *path, struct vfs_dirent *entries, int max_entries);

/* /proc entry generators */
int proc_meminfo_read(char *buf, size_t count);
int proc_ps_read(char *buf, size_t count);
int proc_status_read(char *buf, size_t count, pid_t pid);
int proc_cmdline_read(char *buf, size_t count, pid_t pid);
int proc_pid_stat_read(char *buf, size_t count, pid_t pid);
const char *proc_resolve_path(const char *path, pid_t *pid_out);
int proc_is_virtual_subdir(const char *path);
int proc_uptime_read(char *buf, size_t count);
int proc_stat_read(char *buf, size_t count);
int proc_version_read(char *buf, size_t count);
int proc_boot_cmdline_read(char *buf, size_t count);
int proc_cpuinfo_read(char *buf, size_t count);
int proc_blockdevices_read(char *buf, size_t count);
int proc_netinfo_read(char *buf, size_t count);
int proc_drivers_read(char *buf, size_t count);
int proc_loadavg_read(char *buf, size_t count);
int proc_filesystems_read(char *buf, size_t count);
int proc_partitions_read(char *buf, size_t count);
int proc_interrupts_read(char *buf, size_t count);
int proc_iomem_read(char *buf, size_t count);
int proc_ioports_read(char *buf, size_t count);
int proc_modules_read(char *buf, size_t count);
int proc_timer_list_read(char *buf, size_t count);
int proc_net_dev_read(char *buf, size_t count);
int proc_net_route_read(char *buf, size_t count);
int proc_kmsg_read(char *buf, size_t count);
int proc_swaps_read(char *buf, size_t count);
