/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_udp.h
 * Description: UDP socket object API (kernel layer)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/types.h>

struct sock_udp;

struct sock_udp *sock_udp_create(void);
void sock_udp_acquire(struct sock_udp *sock);
void sock_udp_release(struct sock_udp *sock);

int sock_udp_bind(struct sock_udp *sock, uint16_t port_host);
int sock_udp_connect(struct sock_udp *sock, uint32_t peer_ip_be,
		     uint16_t peer_port_host);
int sock_udp_sendto(struct sock_udp *sock, uint32_t dest_ip_be, uint16_t dest_port_host,
		    const void *data, size_t len);
ssize_t sock_udp_recvfrom(struct sock_udp *sock, void *buf, size_t len, int flags,
			  uint32_t *src_ip_be_out, uint16_t *src_port_out);

struct sock_udp_snap
{
	uint32_t local_ip;
	uint16_t local_port;
	uint32_t rem_ip;
	uint16_t rem_port;
	uint8_t st;
	unsigned long inode;
};

int sock_udp_walk(int (*cb)(const struct sock_udp_snap *s, void *ctx), void *ctx);
