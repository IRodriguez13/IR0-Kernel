/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_icmp.c
 * Description: AF_INET SOCK_RAW IPPROTO_ICMP sockets (RX queue + ip_send)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_icmp.h>
#include <ir0/sock_stream.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/clock.h>
#include <ir0/net.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <string.h>

#if CONFIG_ENABLE_NETWORKING
#include "ip.h"
#endif

#define SOCK_ICMP_MAGIC     0xC1
#define SOCK_ICMP_RX_MAX    32
#define SOCK_ICMP_PKT_MAX   1500
#define SOCK_ICMP_RECV_MS   30000
#define MSG_DONTWAIT        0x40

struct sock_icmp_rx_pkt
{
	struct sock_icmp_rx_pkt *next;
	ip4_addr_t src_ip;
	uint16_t len;
	uint8_t data[SOCK_ICMP_PKT_MAX];
};

struct sock_icmp
{
	uint8_t magic;
	int refcount;
	struct sock_icmp *list_next;
	struct sock_icmp_rx_pkt *rx_head;
	struct sock_icmp_rx_pkt *rx_tail;
	size_t rx_count;
};

static struct sock_icmp *sock_icmp_open_list;

static inline uint64_t sock_icmp_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void sock_icmp_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

int sock_icmp_is(const void *ptr)
{
	const struct sock_icmp *s = (const struct sock_icmp *)ptr;

	return s && s->magic == SOCK_ICMP_MAGIC;
}

int sock_icmp_poll_readable(struct sock_icmp *sock)
{
	uint64_t flags;
	int ready;

	if (!sock || !sock_icmp_is(sock))
		return 0;
	flags = sock_icmp_irq_save();
	ready = sock->rx_head != NULL;
	sock_icmp_irq_restore(flags);
	return ready;
}

static void sock_icmp_rx_enqueue_locked(struct sock_icmp *sock, ip4_addr_t src_ip,
					const void *data, size_t len)
{
	struct sock_icmp_rx_pkt *pkt;

	if (!sock || !data || len == 0 || len > SOCK_ICMP_PKT_MAX)
		return;

	pkt = kmalloc(sizeof(*pkt));
	if (!pkt)
		return;

	pkt->src_ip = src_ip;
	pkt->len = (uint16_t)len;
	memcpy(pkt->data, data, len);
	pkt->next = NULL;

	while (sock->rx_count >= SOCK_ICMP_RX_MAX && sock->rx_head)
	{
		struct sock_icmp_rx_pkt *drop = sock->rx_head;

		sock->rx_head = drop->next;
		if (!sock->rx_head)
			sock->rx_tail = NULL;
		sock->rx_count--;
		kfree(drop);
	}
	if (!sock->rx_tail)
	{
		sock->rx_head = pkt;
		sock->rx_tail = pkt;
	}
	else
	{
		sock->rx_tail->next = pkt;
		sock->rx_tail = pkt;
	}
	sock->rx_count++;
}

static void sock_icmp_list_add(struct sock_icmp *sock)
{
	uint64_t flags;

	flags = sock_icmp_irq_save();
	sock->list_next = sock_icmp_open_list;
	sock_icmp_open_list = sock;
	sock_icmp_irq_restore(flags);
}

static void sock_icmp_list_remove(struct sock_icmp *sock)
{
	struct sock_icmp **pp;
	uint64_t flags;

	flags = sock_icmp_irq_save();
	for (pp = &sock_icmp_open_list; *pp; pp = &(*pp)->list_next)
	{
		if (*pp == sock)
		{
			*pp = sock->list_next;
			break;
		}
	}
	sock->list_next = NULL;
	sock_icmp_irq_restore(flags);
}

void sock_icmp_rx_deliver(uint32_t src_ip_be, uint32_t dst_ip_be, uint8_t ttl,
			  const void *icmp_data, size_t icmp_len)
{
	struct sock_icmp *s;
	uint64_t flags;
	ip4_addr_t src_ip = src_ip_be;
	uint8_t wire[SOCK_ICMP_PKT_MAX];
	struct ip_header *ip;
	size_t total;

#if !CONFIG_ENABLE_NETWORKING
	(void)src_ip_be;
	(void)dst_ip_be;
	(void)ttl;
	(void)icmp_data;
	(void)icmp_len;
	return;
#else
	/*
	 * Linux SOCK_RAW IPPROTO_ICMP delivers IP header + ICMP to
	 * userspace (BusyBox unpack4 reads ihl/ttl before ICMP).
	 */
	if (!icmp_data || icmp_len == 0)
		return;
	total = sizeof(struct ip_header) + icmp_len;
	if (total > SOCK_ICMP_PKT_MAX)
		return;

	memset(wire, 0, sizeof(struct ip_header));
	ip = (struct ip_header *)wire;
	ip->version_ihl = 0x45;
	ip->total_len = htons((uint16_t)total);
	ip->ttl = ttl ? ttl : 64;
	ip->protocol = IPPROTO_ICMP;
	ip->src_addr = src_ip_be;
	ip->dest_addr = dst_ip_be;
	ip->checksum = ip_checksum(ip, sizeof(struct ip_header));
	memcpy(wire + sizeof(struct ip_header), icmp_data, icmp_len);

	flags = sock_icmp_irq_save();
	for (s = sock_icmp_open_list; s; s = s->list_next)
		sock_icmp_rx_enqueue_locked(s, src_ip, wire, total);
	sock_icmp_irq_restore(flags);
#endif
}

struct sock_icmp *sock_icmp_create(void)
{
#if !CONFIG_ENABLE_NETWORKING
	return NULL;
#else
	struct sock_icmp *sock;

	sock = kmalloc(sizeof(*sock));
	if (!sock)
		return NULL;
	memset(sock, 0, sizeof(*sock));
	sock->magic = SOCK_ICMP_MAGIC;
	sock->refcount = 1;
	sock_icmp_list_add(sock);
	return sock;
#endif
}

void sock_icmp_acquire(struct sock_icmp *sock)
{
	uint64_t flags;

	if (!sock)
		return;
	flags = sock_icmp_irq_save();
	sock->refcount++;
	sock_icmp_irq_restore(flags);
}

static void sock_icmp_free_rx(struct sock_icmp *sock)
{
	struct sock_icmp_rx_pkt *pkt;

	while (sock->rx_head)
	{
		pkt = sock->rx_head;
		sock->rx_head = pkt->next;
		kfree(pkt);
	}
	sock->rx_tail = NULL;
	sock->rx_count = 0;
}

void sock_icmp_release(struct sock_icmp *sock)
{
	int refs;
	uint64_t flags;

	if (!sock)
		return;
	if (sock_stream_is_slot(sock))
		return;
	if (!sock_icmp_is(sock))
		return;

	flags = sock_icmp_irq_save();
	refs = --sock->refcount;
	if (refs > 0)
	{
		sock_icmp_irq_restore(flags);
		return;
	}
	sock->magic = 0;
	sock_icmp_irq_restore(flags);

	sock_icmp_list_remove(sock);
	sock_icmp_free_rx(sock);
	kfree(sock);
}

int sock_icmp_sendto(struct sock_icmp *sock, uint32_t dest_ip_be,
		     const void *data, size_t len)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)dest_ip_be;
	(void)data;
	(void)len;
	return -ENOSYS;
#else
	struct net_device *dev;
	ip4_addr_t dest_ip = dest_ip_be;
	int ret;

	if (!sock || !data)
		return -EINVAL;
	if (len == 0)
		return 0;
	if (len > SOCK_ICMP_PKT_MAX)
		return -EMSGSIZE;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	ret = ip_send(dev, dest_ip, IPPROTO_ICMP, data, len);
	if (ret != 0)
		return -EIO;
	return (int)len;
#endif
}

ssize_t sock_icmp_recvfrom(struct sock_icmp *sock, void *buf, size_t len,
			   int flags, uint32_t *src_ip_be_out)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)buf;
	(void)len;
	(void)flags;
	(void)src_ip_be_out;
	return -ENOSYS;
#else
	struct sock_icmp_rx_pkt *pkt = NULL;
	uint64_t flags_irq;
	uint64_t start;
	bool nonblock = (flags & MSG_DONTWAIT) != 0;
	size_t copy_len;

	if (!sock || !buf)
		return -EINVAL;
	if (len == 0)
		return 0;

	start = clock_get_uptime_milliseconds();
	for (;;)
	{
		flags_irq = sock_icmp_irq_save();
		if (sock->rx_head)
		{
			pkt = sock->rx_head;
			sock->rx_head = pkt->next;
			if (!sock->rx_head)
				sock->rx_tail = NULL;
			sock->rx_count--;
		}
		sock_icmp_irq_restore(flags_irq);

		if (pkt)
			break;

		if (nonblock)
			return -EAGAIN;

		if (clock_get_uptime_milliseconds() - start >= SOCK_ICMP_RECV_MS)
			return -EAGAIN;

		net_stack_poll();
	}

	copy_len = pkt->len;
	if (copy_len > len)
		copy_len = len;
	memcpy(buf, pkt->data, copy_len);
	if (src_ip_be_out)
		*src_ip_be_out = (uint32_t)pkt->src_ip;
	kfree(pkt);
	return (ssize_t)copy_len;
#endif
}

int sock_icmp_walk(int (*cb)(const struct sock_icmp_snap *s, void *ctx),
		   void *ctx)
{
	struct sock_icmp *s;
	struct sock_icmp_snap snap;
	uint64_t flags;

	if (!cb)
		return -EINVAL;

	flags = sock_icmp_irq_save();
	for (s = sock_icmp_open_list; s; s = s->list_next)
	{
		if (!sock_icmp_is(s))
			continue;
		memset(&snap, 0, sizeof(snap));
		snap.proto = 1; /* IPPROTO_ICMP */
		snap.inode = (unsigned long)(uintptr_t)s;
		snap.refcnt = (unsigned)(s->refcount > 0 ? s->refcount : 1);
		if (cb(&snap, ctx) != 0)
		{
			sock_icmp_irq_restore(flags);
			return -1;
		}
	}
	sock_icmp_irq_restore(flags);
	return 0;
}
