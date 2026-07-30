/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: socket.h
 * Description: POSIX socket types facade (Linux uapi subset)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/uio.h>

#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_INET     2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3
#define SOCK_CLOEXEC   0x80000
#define SOCK_NONBLOCK  0x800
#define SOCK_TYPE_MASK 0xf

#define SOL_SOCKET  1
#define SO_ERROR    4
#define SO_TYPE     3
#define SO_REUSEADDR 2
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SCM_RIGHTS  1

#define MSG_PEEK     0x2
#define MSG_DONTWAIT 0x40

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

struct sockaddr
{
	uint16_t sa_family;
	char sa_data[14];
};

struct sockaddr_in
{
	uint16_t sin_family;
	uint16_t sin_port;
	uint32_t sin_addr;
	uint8_t sin_zero[8];
};

struct sockaddr_un
{
	uint16_t sun_family;
	char sun_path[108];
};

struct msghdr
{
	void *msg_name;
	uint32_t msg_namelen;
	struct iovec *msg_iov;
	size_t msg_iovlen;
	void *msg_control;
	size_t msg_controllen;
	int msg_flags;
};

struct cmsghdr
{
	size_t cmsg_len;
	int cmsg_level;
	int cmsg_type;
};

#define IR0_CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define IR0_CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + IR0_CMSG_ALIGN(sizeof(struct cmsghdr)))
#define IR0_CMSG_SPACE(len) (IR0_CMSG_ALIGN(sizeof(struct cmsghdr)) + IR0_CMSG_ALIGN(len))
#define IR0_CMSG_LEN(len) (IR0_CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))

typedef uint32_t socklen_t;
