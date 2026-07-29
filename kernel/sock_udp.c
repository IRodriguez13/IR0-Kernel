/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_udp.c
 * Description: UDP socket objects (RX queue + bind/ephemeral ports)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/clock.h>
#include <ir0/net.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <string.h>
#include "udp.h"

#define SOCK_UDP_RX_MAX     32
#define SOCK_UDP_PKT_MAX    1472
#define SOCK_UDP_RECV_MS    30000
#define MSG_DONTWAIT        0x40

struct sock_udp_rx_pkt
{
	struct sock_udp_rx_pkt *next;
	ip4_addr_t src_ip;
	uint16_t src_port;
	uint16_t len;
	uint8_t data[SOCK_UDP_PKT_MAX];
};

struct sock_udp
{
	int refcount;
	uint16_t local_port;
	bool bound;
	bool connected;
	uint32_t peer_ip_be;
	uint16_t peer_port;
	struct sock_udp *bound_next;
	struct sock_udp_rx_pkt *rx_head;
	struct sock_udp_rx_pkt *rx_tail;
	size_t rx_count;
};

static struct sock_udp *sock_bound_list;
static uint16_t sock_next_ephemeral = 32768;

static inline uint64_t sock_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void sock_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

static bool sock_port_in_use(uint16_t port)
{
	struct sock_udp *s;

	for (s = sock_bound_list; s; s = s->bound_next)
	{
		if (s->bound && s->local_port == port)
			return true;
	}
	return false;
}

static uint16_t sock_pick_ephemeral(void)
{
	uint16_t start = sock_next_ephemeral;
	uint16_t port = start;

	do
	{
		if (port >= 32768 && port <= 60999 && !sock_port_in_use(port))
		{
			sock_next_ephemeral = (uint16_t)(port + 1);
			if (sock_next_ephemeral < 32768)
				sock_next_ephemeral = 32768;
			return port;
		}
		port++;
		if (port > 60999)
			port = 32768;
	} while (port != start);

	return 0;
}

static void sock_udp_rx_enqueue(struct sock_udp *sock, ip4_addr_t src_ip,
				uint16_t src_port, const void *data, size_t len)
{
	struct sock_udp_rx_pkt *pkt;
	uint64_t flags;

	if (!sock || !data || len == 0 || len > SOCK_UDP_PKT_MAX)
		return;

	pkt = kmalloc(sizeof(*pkt));
	if (!pkt)
		return;

	pkt->src_ip = src_ip;
	pkt->src_port = src_port;
	pkt->len = (uint16_t)len;
	memcpy(pkt->data, data, len);
	pkt->next = NULL;

	flags = sock_irq_save();
	while (sock->rx_count >= SOCK_UDP_RX_MAX && sock->rx_head)
	{
		struct sock_udp_rx_pkt *drop = sock->rx_head;

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
	sock_irq_restore(flags);
}

#if CONFIG_ENABLE_NETWORKING

static void sock_udp_rx_from_net(struct net_device *dev, ip4_addr_t src_ip,
				 uint16_t src_port, const void *data, size_t len,
				 void *priv)
{
	struct sock_udp *owner = (struct sock_udp *)priv;

	(void)dev;
	if (!owner)
		return;
	sock_udp_rx_enqueue(owner, src_ip, src_port, data, len);
}

static int sock_udp_install_port_handler(struct sock_udp *sock, uint16_t port)
{
	sock->local_port = port;
	sock->bound = true;
	sock->bound_next = sock_bound_list;
	sock_bound_list = sock;
	udp_register_handler(port, sock_udp_rx_from_net, sock);
	return 0;
}

#endif /* CONFIG_ENABLE_NETWORKING */

struct sock_udp *sock_udp_create(void)
{
#if !CONFIG_ENABLE_NETWORKING
	return NULL;
#else
	struct sock_udp *sock;

	sock = kmalloc(sizeof(*sock));
	if (!sock)
		return NULL;
	memset(sock, 0, sizeof(*sock));
	sock->refcount = 1;
	return sock;
#endif
}

void sock_udp_acquire(struct sock_udp *sock)
{
	uint64_t flags;

	if (!sock)
		return;
	flags = sock_irq_save();
	sock->refcount++;
	sock_irq_restore(flags);
}

static void sock_udp_free_rx(struct sock_udp *sock)
{
	struct sock_udp_rx_pkt *pkt;

	while (sock->rx_head)
	{
		pkt = sock->rx_head;
		sock->rx_head = pkt->next;
		kfree(pkt);
	}
	sock->rx_tail = NULL;
	sock->rx_count = 0;
}

static void sock_udp_unbind_locked(struct sock_udp *sock)
{
#if CONFIG_ENABLE_NETWORKING
	struct sock_udp **pp;

	if (!sock->bound)
		return;

	udp_unregister_handler(sock->local_port);
	for (pp = &sock_bound_list; *pp; pp = &(*pp)->bound_next)
	{
		if (*pp == sock)
		{
			*pp = sock->bound_next;
			break;
		}
	}
	sock->bound = false;
	sock->bound_next = NULL;
	sock->local_port = 0;
#else
	(void)sock;
#endif
}

void sock_udp_release(struct sock_udp *sock)
{
	int refs;
	uint64_t flags;

	if (!sock)
		return;
	/*
	 * AF_UNIX/TCP stream sockets live in a static table. After the last
	 * stream release clears magic, a duplicate close used to fall through
	 * here and kfree() the slot → "pointer out of heap range" panic.
	 */
	if (sock_stream_is_slot(sock))
		return;
	if (sock_icmp_is(sock))
		return;

	flags = sock_irq_save();
	refs = --sock->refcount;
	if (refs > 0)
	{
		sock_irq_restore(flags);
		return;
	}

	sock_udp_unbind_locked(sock);
	sock_irq_restore(flags);

	sock_udp_free_rx(sock);
	kfree(sock);
}

int sock_udp_bind(struct sock_udp *sock, uint16_t port_host)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)port_host;
	return -ENOSYS;
#else
	uint64_t flags;
	int ret;

	if (!sock)
		return -EINVAL;
	if (port_host == 0)
		return -EINVAL;
	if (sock_port_in_use(port_host))
		return -EADDRINUSE;

	flags = sock_irq_save();
	if (sock->bound)
	{
		sock_irq_restore(flags);
		return -EINVAL;
	}
	ret = sock_udp_install_port_handler(sock, port_host);
	sock_irq_restore(flags);
	return ret;
#endif
}

static int sock_udp_ensure_port(struct sock_udp *sock)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	return -ENOSYS;
#else
	uint16_t port;
	int ret;
	uint64_t flags;

	if (!sock)
		return -EINVAL;
	if (sock->bound)
		return 0;

	port = sock_pick_ephemeral();
	if (port == 0)
		return -EADDRINUSE;

	flags = sock_irq_save();
	if (sock->bound)
	{
		sock_irq_restore(flags);
		return 0;
	}
	ret = sock_udp_install_port_handler(sock, port);
	sock_irq_restore(flags);
	return ret;
#endif
}

int sock_udp_connect(struct sock_udp *sock, uint32_t peer_ip_be,
		     uint16_t peer_port_host)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)peer_ip_be;
	(void)peer_port_host;
	return -ENOSYS;
#else
	uint64_t flags;

	if (!sock || peer_port_host == 0)
		return -EINVAL;

	flags = sock_irq_save();
	sock->peer_ip_be = peer_ip_be;
	sock->peer_port = peer_port_host;
	sock->connected = true;
	sock_irq_restore(flags);
	return 0;
#endif
}

int sock_udp_sendto(struct sock_udp *sock, uint32_t dest_ip_be, uint16_t dest_port_host,
		    const void *data, size_t len)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)dest_ip_be;
	(void)dest_port_host;
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
	if (len > SOCK_UDP_PKT_MAX)
		return -EMSGSIZE;

	if (sock->connected)
	{
		dest_ip_be = sock->peer_ip_be;
		dest_port_host = sock->peer_port;
	}

	ret = sock_udp_ensure_port(sock);
	if (ret < 0)
		return ret;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	ret = udp_send(dev, dest_ip, sock->local_port, dest_port_host, data, len);
	if (ret < 0)
		return -EIO;
	return (int)len;
#endif
}

ssize_t sock_udp_recvfrom(struct sock_udp *sock, void *buf, size_t len, int flags,
			  uint32_t *src_ip_be_out, uint16_t *src_port_out)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)buf;
	(void)len;
	(void)flags;
	(void)src_ip_be_out;
	(void)src_port_out;
	return -ENOSYS;
#else
	struct sock_udp_rx_pkt *pkt = NULL;
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
		flags_irq = sock_irq_save();
		if (sock->rx_head)
		{
			pkt = sock->rx_head;
			sock->rx_head = pkt->next;
			if (!sock->rx_head)
				sock->rx_tail = NULL;
			sock->rx_count--;
		}
		sock_irq_restore(flags_irq);

		if (pkt)
			break;

		if (nonblock)
			return -EAGAIN;

		if (clock_get_uptime_milliseconds() - start >= SOCK_UDP_RECV_MS)
			return -EAGAIN;

		net_stack_poll();
	}

	copy_len = pkt->len;
	if (copy_len > len)
		copy_len = len;
	memcpy(buf, pkt->data, copy_len);
	if (src_ip_be_out)
		*src_ip_be_out = (uint32_t)pkt->src_ip;
	if (src_port_out)
		*src_port_out = pkt->src_port;
	kfree(pkt);
	return (ssize_t)copy_len;
#endif
}
