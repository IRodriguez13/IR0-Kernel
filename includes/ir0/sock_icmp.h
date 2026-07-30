/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_icmp.h
 * Description: AF_INET SOCK_RAW IPPROTO_ICMP socket object API
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/types.h>

struct sock_icmp;

struct sock_icmp *sock_icmp_create(void);
void sock_icmp_acquire(struct sock_icmp *sock);
void sock_icmp_release(struct sock_icmp *sock);

int sock_icmp_is(const void *ptr);

int sock_icmp_poll_readable(struct sock_icmp *sock);

int sock_icmp_sendto(struct sock_icmp *sock, uint32_t dest_ip_be,
		     const void *data, size_t len);
ssize_t sock_icmp_recvfrom(struct sock_icmp *sock, void *buf, size_t len,
			   int flags, uint32_t *src_ip_be_out);

/*
 * Deliver one ICMP datagram to all open SOCK_RAW IPPROTO_ICMP sockets.
 * Payload to userspace is IP header + ICMP (Linux SOCK_RAW ABI).
 */
void sock_icmp_rx_deliver(uint32_t src_ip_be, uint32_t dst_ip_be, uint8_t ttl,
			  const void *icmp_data, size_t icmp_len);

struct sock_icmp_snap
{
	uint16_t proto; /* IPPROTO_ICMP = 1 as "port" */
	unsigned long inode;
	unsigned refcnt;
};

int sock_icmp_walk(int (*cb)(const struct sock_icmp_snap *s, void *ctx),
		   void *ctx);
