/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: types.h
 * Description: IR0 kernel source/header file
 */

#ifndef _IR0_TYPES_H
#define _IR0_TYPES_H

#include <stdint.h> // Para tipos estándar

// Standard types
typedef int32_t pid_t;
typedef int64_t time_t; 
typedef int64_t off_t; // Definición centralizada de off_t
typedef uint32_t mode_t;
typedef uint32_t dev_t;

/*
 * Legacy Linux dev_t encoding (16 bits: major<<8 | minor), which is what
 * mknod(1) passes for the classic nodes IR0 cares about (1:3 null, 5:1
 * console). The 32-bit split Linux uses for large minors is not needed here
 * and would make the numbers in /proc and stat harder to read.
 */
#define IR0_MKDEV(ma, mi) ((dev_t)((((ma) & 0xFFu) << 8) | ((mi) & 0xFFu)))
#define IR0_MAJOR(dev)    ((unsigned)(((dev) >> 8) & 0xFFu))
#define IR0_MINOR(dev)    ((unsigned)((dev) & 0xFFu))
typedef uint32_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;
typedef int64_t ssize_t;

// File type and permission bits (from stat.h)
#define S_IFMT   0170000  // File type mask
#define S_IFREG  0100000  // Regular file
#define S_IFDIR  0040000  // Directory
#define S_IFCHR  0020000  // Character device
#define S_IFBLK  0060000  // Block device
#define S_IFLNK  0120000  // Symbolic link
#define S_IFIFO  0010000  // FIFO/pipe
#define S_IFSOCK 0140000  // Socket

// File mode bits (from stat.h)
#define S_IRWXU 0000700  // User: read, write, execute
#define S_IRUSR 0000400  // User: read
#define S_IWUSR 0000200  // User: write
#define S_IXUSR 0000100  // User: execute

#define S_IRWXG 0000070  // Group: read, write, execute
#define S_IRGRP 0000040  // Group: read
#define S_IWGRP 0000020  // Group: write
#define S_IXGRP 0000010  // Group: execute

#define S_IRWXO 0000007  // Others: read, write, execute
#define S_IROTH 0000004  // Others: read
#define S_IWOTH 0000002  // Others: write
#define S_IXOTH 0000001  // Others: execute

#endif /* _IR0_TYPES_H */
