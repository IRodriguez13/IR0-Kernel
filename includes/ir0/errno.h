/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: errno.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: errno.h
 * Description: Centralized error number definitions
 */

#ifndef _IR0_ERRNO_H
#define _IR0_ERRNO_H

/* ERROR NUMBER DEFINITIONS (POSIX-compatible)                               */

#define EPERM   1   /* Operation not permitted */
#define ENOENT  2   /* No such file or directory */
#define ESRCH   3   /* No such process */
#define EINTR   4   /* Interrupted system call */
#define EIO     5   /* I/O error */
#define ENXIO   6   /* No such device or address */
#define E2BIG   7   /* Argument list too long */
#define ENOEXEC 8   /* Exec format error */
#define EBADF   9   /* Bad file number */
#define ECHILD  10  /* No child processes */
#define EAGAIN  11  /* Try again */
#define EWOULDBLOCK EAGAIN /* POSIX alias; same value as Linux */
#define ENOMEM  12  /* Out of memory */
#define EACCES  13  /* Permission denied */
#define EFAULT  14  /* Bad address */
#define ENOTBLK 15  /* Block device required */
#define EBUSY   16  /* Device or resource busy */
#define EADDRINUSE 98 /* Address already in use (Linux) */
#define ECONNREFUSED 111 /* Connection refused (Linux) */
#define EEXIST  17  /* File exists */
#define EXDEV   18  /* Cross-device link */
#define ENODEV  19  /* No such device */
#define ENOTDIR 20  /* Not a directory */
#define EISDIR  21  /* Is a directory */
#define EINVAL  22  /* Invalid argument */
#define ENFILE  23  /* File table overflow */
#define EMFILE  24  /* Too many open files */
#define ENOTTY  25  /* Not a typewriter */
#define ETXTBSY 26  /* Text file busy */
#define EFBIG   27  /* File too large */
#define ENOSPC  28  /* No space left on device */
#define ESPIPE  29  /* Illegal seek */
#define EROFS   30  /* Read-only file system */
#define EMLINK  31  /* Too many links */
#define EPIPE   32  /* Broken pipe */
#define EMSGSIZE 90 /* Message too long (Linux) */
#define ENETUNREACH 101 /* Network unreachable (Linux) */
#define ETIMEDOUT   110 /* Connection timed out (Linux) */
#define EDOM    33  /* Math argument out of domain */
#define ERANGE  34  /* Math result not representable */
#define ELOOP   40  /* Too many symbolic links encountered */
#define ENAMETOOLONG 36  /* File name too long */
#define ENOSYS  38  /* Function not implemented */
#define ENOTSUPP   95  /* Operation not supported */
#define EOPNOTSUPP ENOTSUPP  /* POSIX name; same numeric value as Linux */
#define ENOTEMPTY 39 /* Directory not empty (Linux errno 39) */
#define ENOLCK  37  /* No record locks available (Linux errno 37) */
#define ENOTSOCK 88
#define EPROTOTYPE 91
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define EAFNOSUPPORT 97
#define ENOBUFS 105
#define EISCONN 106
#define ENOTCONN 107
#define ESHUTDOWN 108
#define EALREADY 114
#define EINPROGRESS 115

/* STANDARD FILE DESCRIPTORS                                                */

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2



typedef uint32_t mode_t;

/* EXTERNAL DECLARATIONS (for syscalls)                                     */

extern const char* get_fs_type(uint8_t system_id);

#endif /* _IR0_ERRNO_H */
