/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_stream.c
 * Description: AF_UNIX pathname + TCP loopback + guest-net (10.0.2.x) stream MVP.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_stream.h>
#include <ir0/socket.h>
#include <ir0/types.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/ktm/fault.h>
#include <config.h>
#include <string.h>

#if CONFIG_ENABLE_NETWORKING
#include "tcp.h"
#include <ir0/net.h>
#endif

#define SS_BUF 4096
#define SS_MAX 16
#define SS_PATH 108

enum ss_state
{
	SS_IDLE = 0,
	SS_BOUND,
	SS_LISTEN,
	SS_CONNECTING,
	SS_CONNECTED,
};

struct sock_stream
{
	int in_use;
	int fd_refs; /* close/fork: free slot only when last fd drops */
	int family;
	enum ss_state state;
	char path[SS_PATH];
	size_t path_len;
	int is_abstract;
	uint16_t port;
	struct sock_stream *peer;
	struct sock_stream *listener;
	char buf[SS_BUF];
	unsigned head;
	unsigned tail;
	unsigned count;
	uint8_t wire_tcp;
	uint8_t shut_rd;
	uint8_t shut_wr;
	uint8_t reuseaddr;
	uint16_t wire_local_port;
	uint32_t wire_peer_ip;
	uint16_t wire_peer_port;
	uint32_t wire_seq;
	uint32_t wire_ack;
	int so_error; /* positive errno for SO_ERROR */
	uint64_t rcv_timeout_ms; /* 0 = block forever */
	uint64_t snd_timeout_ms;
	uint8_t magic;
	uint8_t rights_n;
	uint8_t rights[SOCK_STREAM_RIGHTS_MAX][SOCK_STREAM_RIGHTS_ENTRY_SIZE];
};

#define SS_MAGIC 0xA5

static struct sock_stream g_socks[SS_MAX];
static void (*g_rights_dtor)(void *entry, size_t sz);

extern void poll_wake_check(void);

void sock_stream_set_rights_dtor(void (*dtor)(void *entry, size_t sz))
{
	g_rights_dtor = dtor;
}

static void sock_stream_rights_clear(struct sock_stream *s)
{
	int i;

	if (!s)
		return;
	for (i = 0; i < s->rights_n; i++)
	{
		if (g_rights_dtor)
			g_rights_dtor(s->rights[i], SOCK_STREAM_RIGHTS_ENTRY_SIZE);
	}
	s->rights_n = 0;
}

int sock_stream_rights_push(struct sock_stream *recv_side, const void *entry, size_t sz)
{
	if (!recv_side || !entry || sz == 0 || sz > SOCK_STREAM_RIGHTS_ENTRY_SIZE)
		return -EINVAL;
	if (recv_side->rights_n >= SOCK_STREAM_RIGHTS_MAX)
		return -ENOBUFS;
	memset(recv_side->rights[recv_side->rights_n], 0, SOCK_STREAM_RIGHTS_ENTRY_SIZE);
	memcpy(recv_side->rights[recv_side->rights_n], entry, sz);
	recv_side->rights_n++;
	poll_wake_check();
	return 0;
}

int sock_stream_rights_pop(struct sock_stream *s, void *entry, size_t sz)
{
	if (!s || !entry || sz == 0 || sz > SOCK_STREAM_RIGHTS_ENTRY_SIZE)
		return -EINVAL;
	if (s->rights_n == 0)
		return -EAGAIN;
	memcpy(entry, s->rights[0], sz);
	s->rights_n--;
	if (s->rights_n > 0)
	{
		memmove(s->rights[0], s->rights[1],
			(size_t)s->rights_n * SOCK_STREAM_RIGHTS_ENTRY_SIZE);
	}
	return 0;
}

int sock_stream_rights_count(const struct sock_stream *s)
{
	return s ? (int)s->rights_n : 0;
}

int sock_stream_family(const struct sock_stream *s)
{
	return s ? s->family : 0;
}

int sock_stream_buf_count(const struct sock_stream *s)
{
	return s ? (int)s->count : 0;
}

int sock_stream_buf_space(const struct sock_stream *peer_of_sender)
{
	if (!peer_of_sender)
		return 0;
	return (int)(SS_BUF - peer_of_sender->count);
}

int sock_stream_is_recv_shutdown(const struct sock_stream *s)
{
	return s && s->shut_rd;
}

struct sock_stream *sock_stream_get_peer(struct sock_stream *s)
{
	return s ? s->peer : NULL;
}

static int sock_stream_wire_progress(struct sock_stream *s)
{
	uint32_t seq;
	uint32_t ack;
	int ret;

	if (!s || !s->wire_tcp || s->state != SS_CONNECTING)
		return 0;

	ret = tcp_wire_connect_poll((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
				    s->wire_local_port, &seq, &ack);
	if (ret == -EAGAIN)
		return 0;
	if (ret < 0)
	{
		s->so_error = -ret;
		s->state = SS_BOUND;
		poll_wake_check();
		return ret;
	}
	s->wire_seq = seq;
	s->wire_ack = ack;
	s->state = SS_CONNECTED;
	s->so_error = 0;
	poll_wake_check();
	return 1;
}

int sock_stream_poll_readable(const struct sock_stream *s)
{
	struct sock_stream *mut = (struct sock_stream *)s;

	if (!s)
		return 0;
	/* Pending AF_UNIX/TCP-loopback accept queue (single slot via listener->peer). */
	if (s->state == SS_LISTEN && s->peer && s->peer->state == SS_CONNECTED)
		return 1;
	if (s->state == SS_CONNECTING)
	{
		(void)sock_stream_wire_progress(mut);
		return 0;
	}
	if (s->state != SS_CONNECTED)
		return 0;
	if (s->rights_n > 0)
		return 1;
	if (s->count > 0)
		return 1;
	if (s->shut_rd)
		return 1;
#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp)
		return tcp_wire_poll_readable((ip4_addr_t)s->wire_peer_ip,
					      s->wire_peer_port,
					      s->wire_local_port);
#endif
	if (!s->peer || s->peer->shut_wr)
		return 1;
	return 0;
}

int sock_stream_poll_writable(const struct sock_stream *s)
{
	struct sock_stream *mut = (struct sock_stream *)s;

	if (!s)
		return 0;
	if (s->state == SS_CONNECTING)
	{
		(void)sock_stream_wire_progress(mut);
		/* Linux: POLLOUT when connect finished (ok or error). */
		if (s->state == SS_CONNECTED || s->so_error)
			return 1;
		return 0;
	}
	if (s->state != SS_CONNECTED)
		return 0;
	if (s->so_error)
		return 1;
	if (s->shut_wr)
		return 0;
#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp)
		return tcp_wire_poll_writable((ip4_addr_t)s->wire_peer_ip,
					      s->wire_peer_port,
					      s->wire_local_port);
#endif
	if (!s->peer)
		return 0;
	if (s->peer->shut_rd)
		return 0;
	return s->peer->count < SS_BUF;
}

int sock_stream_is(const void *ptr)
{
	const struct sock_stream *s = ptr;
	uintptr_t base = (uintptr_t)&g_socks[0];
	uintptr_t end = (uintptr_t)&g_socks[SS_MAX];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_socks[0])) != 0)
		return 0;
	return s && s->magic == SS_MAGIC && s->in_use;
}

int sock_stream_is_slot(const void *ptr)
{
	uintptr_t base = (uintptr_t)&g_socks[0];
	uintptr_t end = (uintptr_t)&g_socks[SS_MAX];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_socks[0])) != 0)
		return 0;
	return 1;
}

void sock_stream_acquire(struct sock_stream *s)
{
	if (!s || !s->in_use || s->magic != SS_MAGIC)
		return;
	s->fd_refs++;
}

struct sock_stream *sock_stream_create(int family)
{
	int i;

	if (KTM_FAULT_HIT("sock.create"))
		return NULL;

	for (i = 0; i < SS_MAX; i++)
	{
		if (!g_socks[i].in_use)
		{
			memset(&g_socks[i], 0, sizeof(g_socks[i]));
			g_socks[i].in_use = 1;
			g_socks[i].fd_refs = 1;
			g_socks[i].family = family;
			g_socks[i].magic = SS_MAGIC;
			g_socks[i].state = SS_IDLE;
			return &g_socks[i];
		}
	}
	return NULL;
}

void sock_stream_release(struct sock_stream *s)
{
	if (!s || !s->in_use)
		return;
	if (s->fd_refs > 1)
	{
		s->fd_refs--;
		return;
	}
	s->fd_refs = 0;
#if CONFIG_ENABLE_NETWORKING
	if (s->family == IR0_AF_INET && s->state == SS_LISTEN && s->port != 0)
		tcp_wire_listen_unregister(s->port);
	if (s->wire_tcp)
	{
		uint32_t ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
						 s->wire_peer_port,
						 s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		tcp_wire_close((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
			       s->wire_local_port, s->wire_seq, s->wire_ack);
	}
#endif
	if (s->peer && s->peer->peer == s)
		s->peer->peer = NULL;
	sock_stream_rights_clear(s);
	memset(s, 0, sizeof(*s));
	poll_wake_check();
}

static int unix_name_equal(const struct sock_stream *a, const char *path,
			   size_t path_len, int is_abstract)
{
	if (!a || a->family != IR0_AF_UNIX)
		return 0;
	if (a->is_abstract != is_abstract)
		return 0;
	if (a->path_len != path_len)
		return 0;
	return memcmp(a->path, path, path_len) == 0;
}

int sock_stream_bind_unix_n(struct sock_stream *s, const char *path, size_t path_len,
			    int is_abstract)
{
	int i;

	if (!s || !path || path_len == 0 || path_len >= SS_PATH)
		return -EINVAL;
	if (!is_abstract && path[0] == '\0')
		return -EINVAL;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_UNIX &&
		    g_socks[i].state != SS_IDLE &&
		    unix_name_equal(&g_socks[i], path, path_len, is_abstract))
		{
			/*
			 * SO_REUSEADDR MVP: steal name from a peer that is only
			 * BOUND (not LISTEN/CONNECTED).
			 */
			if (s->reuseaddr && g_socks[i].state == SS_BOUND &&
			    &g_socks[i] != s)
			{
				g_socks[i].path_len = 0;
				g_socks[i].path[0] = '\0';
				g_socks[i].is_abstract = 0;
				continue;
			}
			return -EADDRINUSE;
		}
	}
	memcpy(s->path, path, path_len);
	s->path[path_len] = '\0';
	s->path_len = path_len;
	s->is_abstract = is_abstract ? 1 : 0;
	s->state = SS_BOUND;
	return 0;
}

int sock_stream_bind_unix(struct sock_stream *s, const char *path)
{
	if (!path)
		return -EINVAL;
	return sock_stream_bind_unix_n(s, path, strlen(path), 0);
}

int sock_stream_get_unix_name(const struct sock_stream *s, char *path_out, size_t path_cap,
			      size_t *path_len_out, int *is_abstract_out)
{
	if (!s || s->family != IR0_AF_UNIX)
		return -EINVAL;
	if (path_len_out)
		*path_len_out = s->path_len;
	if (is_abstract_out)
		*is_abstract_out = s->is_abstract;
	if (path_out && path_cap > 0)
	{
		size_t n = s->path_len;

		if (n >= path_cap)
			n = path_cap - 1;
		memcpy(path_out, s->path, n);
		path_out[n] = '\0';
	}
	return 0;
}

int sock_stream_shutdown(struct sock_stream *s, int how)
{
	if (!s || s->state != SS_CONNECTED)
		return -ENOTCONN;
	/* SHUT_RD=0 SHUT_WR=1 SHUT_RDWR=2 */
	if (how < 0 || how > 2)
		return -EINVAL;
	if (how == 0 || how == 2)
		s->shut_rd = 1;
	if (how == 1 || how == 2)
		s->shut_wr = 1;
	poll_wake_check();
	return 0;
}

int sock_stream_listen(struct sock_stream *s, int backlog)
{
	(void)backlog;
	if (!s || s->state != SS_BOUND)
		return -EINVAL;
	s->state = SS_LISTEN;
#if CONFIG_ENABLE_NETWORKING
	if (s->family == IR0_AF_INET && s->port != 0)
	{
		int ret = tcp_wire_listen_register(s->port);

		if (ret < 0)
		{
			s->state = SS_BOUND;
			return ret;
		}
	}
#endif
	return 0;
}

int sock_stream_socketpair(struct sock_stream **a_out, struct sock_stream **b_out)
{
	struct sock_stream *a;
	struct sock_stream *b;

	if (!a_out || !b_out)
		return -EINVAL;

	a = sock_stream_create(IR0_AF_UNIX);
	if (!a)
		return -ENOMEM;
	b = sock_stream_create(IR0_AF_UNIX);
	if (!b)
	{
		sock_stream_release(a);
		return -ENOMEM;
	}

	a->state = SS_CONNECTED;
	b->state = SS_CONNECTED;
	a->peer = b;
	b->peer = a;
	*a_out = a;
	*b_out = b;
	return 0;
}

int sock_stream_connect_unix_n(struct sock_stream *s, const char *path, size_t path_len,
			       int is_abstract)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;

	if (!s || !path || path_len == 0 || path_len >= SS_PATH)
		return -EINVAL;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].state == SS_LISTEN &&
		    unix_name_equal(&g_socks[i], path, path_len, is_abstract))
		{
			lst = &g_socks[i];
			break;
		}
	}
	if (!lst)
		return -ECONNREFUSED;
	acc = sock_stream_create(IR0_AF_UNIX);
	if (!acc)
		return -ENOMEM;
	/* Accepted endpoint inherits listener bind name (Linux getsockname/peer). */
	if (lst->path_len > 0 && lst->path_len < SS_PATH)
	{
		memcpy(acc->path, lst->path, lst->path_len);
		acc->path[lst->path_len] = '\0';
		acc->path_len = lst->path_len;
		acc->is_abstract = lst->is_abstract;
	}
	acc->state = SS_CONNECTED;
	acc->peer = s;
	s->state = SS_CONNECTED;
	s->peer = acc;
	s->listener = lst;
	lst->peer = acc;
	poll_wake_check();
	return 0;
}

int sock_stream_connect_unix(struct sock_stream *s, const char *path)
{
	if (!path)
		return -EINVAL;
	return sock_stream_connect_unix_n(s, path, strlen(path), 0);
}

struct sock_stream *sock_stream_accept(struct sock_stream *s)
{
	struct sock_stream *child;

	if (!s || s->state != SS_LISTEN)
		return NULL;
	child = s->peer;
	if (child && child->state == SS_CONNECTED)
	{
		s->peer = NULL;
		return child;
	}
#if CONFIG_ENABLE_NETWORKING
	if (s->family == IR0_AF_INET && s->port != 0)
	{
		ip4_addr_t peer_ip;
		uint16_t peer_port;
		uint16_t local_port;
		uint32_t seq;
		uint32_t ack;
		int ret;

		net_stack_poll();
		ret = tcp_wire_accept_take(s->port, &peer_ip, &peer_port,
					   &local_port, &seq, &ack);
		if (ret < 0)
			return NULL;
		child = sock_stream_create(IR0_AF_INET);
		if (!child)
			return NULL;
		child->state = SS_CONNECTED;
		child->wire_tcp = 1;
		child->wire_peer_ip = (uint32_t)peer_ip;
		child->wire_peer_port = peer_port;
		child->wire_local_port = local_port;
		child->wire_seq = seq;
		child->wire_ack = ack;
		child->port = local_port;
		child->listener = s;
		return child;
	}
#endif
	return NULL;
}

int sock_stream_bind_inet(struct sock_stream *s, uint16_t port)
{
	int i;

	if (!s)
		return -EINVAL;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state != SS_IDLE && g_socks[i].port == port)
			return -EADDRINUSE;
	}
	s->port = port;
	s->state = SS_BOUND;
	return 0;
}

static int sock_stream_inet_addr_allowed(uint32_t addr)
{
	uint8_t o0;
	uint8_t o1;
	uint8_t o2;

	if (addr == 0 || addr == 0x0100007fu || addr == 0x7f000001u)
		return 1;

	o0 = (uint8_t)(addr & 0xffu);
	o1 = (uint8_t)((addr >> 8) & 0xffu);
	o2 = (uint8_t)((addr >> 16) & 0xffu);
	if (o0 == 10 && o1 == 0 && o2 == 2)
		return 1;

	o0 = (uint8_t)((addr >> 24) & 0xffu);
	o1 = (uint8_t)((addr >> 16) & 0xffu);
	o2 = (uint8_t)((addr >> 8) & 0xffu);
	if (o0 == 10 && o1 == 0 && o2 == 2)
		return 1;

	return 0;
}

static int sock_stream_is_local_listener(uint16_t port)
{
	int i;

	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state == SS_LISTEN && g_socks[i].port == port)
			return 1;
	}
	return 0;
}

int sock_stream_connect_inet(struct sock_stream *s, uint32_t addr, uint16_t port)
{
	return sock_stream_connect_inet_flags(s, addr, port, 0);
}

int sock_stream_connect_inet_flags(struct sock_stream *s, uint32_t addr,
				   uint16_t port, int nonblock)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;

	if (!s)
		return -EINVAL;
	if (!sock_stream_inet_addr_allowed(addr))
		return -ECONNREFUSED;
	if (s->state == SS_CONNECTING)
	{
		int pr = sock_stream_wire_progress(s);

		if (s->state == SS_CONNECTED)
			return 0;
		if (s->so_error)
			return -s->so_error;
		if (pr == 0)
			return -EALREADY;
		return pr < 0 ? pr : 0;
	}
	if (s->state == SS_CONNECTED)
		return -EISCONN;

	if (!sock_stream_is_local_listener(port))
	{
#if CONFIG_ENABLE_NETWORKING
		uint16_t lport;
		uint32_t seq;
		uint32_t ack;
		int ret;

		if (nonblock)
		{
			ret = tcp_wire_connect_start((ip4_addr_t)addr, port, &lport);
			if (ret < 0)
				return ret;
			s->wire_tcp = 1;
			s->wire_local_port = lport;
			s->wire_peer_ip = addr;
			s->wire_peer_port = port;
			s->port = lport;
			s->peer = NULL;
			s->so_error = 0;
			s->state = SS_CONNECTING;
			return -EINPROGRESS;
		}

		ret = tcp_wire_connect((ip4_addr_t)addr, port, &lport, &seq, &ack);
		if (ret < 0)
			return ret;
		s->wire_tcp = 1;
		s->wire_local_port = lport;
		s->wire_peer_ip = addr;
		s->wire_peer_port = port;
		s->wire_seq = seq;
		s->wire_ack = ack;
		s->port = lport;
		s->state = SS_CONNECTED;
		s->peer = NULL;
		s->so_error = 0;
		return 0;
#else
		(void)nonblock;
		return -ECONNREFUSED;
#endif
	}

	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state == SS_LISTEN && g_socks[i].port == port)
		{
			lst = &g_socks[i];
			break;
		}
	}
	if (!lst)
		return -ECONNREFUSED;
	acc = sock_stream_create(IR0_AF_INET);
	if (!acc)
		return -ENOMEM;
	acc->state = SS_CONNECTED;
	acc->peer = s;
	acc->port = port;
	s->state = SS_CONNECTED;
	s->peer = acc;
	lst->peer = acc;
	poll_wake_check();
	return 0;
}

ssize_t sock_stream_send(struct sock_stream *s, const void *buf, size_t len)
{
	struct sock_stream *peer;
	size_t i;
	const char *src = buf;

	if (!s || (s->state != SS_CONNECTED && s->state != SS_CONNECTING) || !buf)
		return -EINVAL;

#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp && s->state == SS_CONNECTING)
	{
		int pr = sock_stream_wire_progress(s);

		if (s->so_error)
			return -s->so_error;
		if (s->state != SS_CONNECTED)
			return pr < 0 ? pr : -EAGAIN;
	}
	if (s->wire_tcp)
	{
		uint32_t ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
						 s->wire_peer_port,
						 s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		int ret = tcp_wire_send((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
					s->wire_local_port, &s->wire_seq, s->wire_ack,
					buf, len);
		return (ret < 0) ? ret : (ssize_t)ret;
	}
#endif

	if (s->state != SS_CONNECTED)
		return -EINVAL;
	if (s->shut_wr)
		return -EPIPE;
	peer = s->peer;
	if (!peer)
		return -EPIPE;
	if (peer->shut_rd)
		return -EPIPE;
	for (i = 0; i < len; i++)
	{
		if (peer->count >= SS_BUF)
			break;
		peer->buf[peer->head] = src[i];
		peer->head = (peer->head + 1) % SS_BUF;
		peer->count++;
	}
	if (i > 0)
		poll_wake_check();
	return (ssize_t)i;
}

ssize_t sock_stream_recv_flags(struct sock_stream *s, void *buf, size_t len, int flags)
{
	size_t i;
	char *dst = buf;
	int peek = (flags & MSG_PEEK) != 0;
	unsigned tail;
	unsigned count;

	if (!s || (s->state != SS_CONNECTED && s->state != SS_CONNECTING) || !buf)
		return -EINVAL;

#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp && s->state == SS_CONNECTING)
	{
		(void)sock_stream_wire_progress(s);
		if (s->so_error)
			return -s->so_error;
		if (s->state != SS_CONNECTED)
			return -EAGAIN;
	}
	if (s->wire_tcp)
	{
		int ret;
		uint32_t ack;

		(void)peek;
		net_stack_poll();
		ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
					s->wire_peer_port, s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		ret = tcp_wire_recv((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
				   s->wire_local_port, buf, len);
		if (ret < 0)
			return ret;
		return (ssize_t)ret;
	}
#endif

	if (s->shut_rd)
		return 0;
	tail = s->tail;
	count = s->count;
	for (i = 0; i < len; i++)
	{
		if (count == 0)
			break;
		dst[i] = s->buf[tail];
		tail = (tail + 1) % SS_BUF;
		count--;
	}
	if (i == 0 && (!s->peer || s->peer->shut_wr))
		return 0;
	if (i == 0)
		return -EAGAIN; /* caller may block; MSG_DONTWAIT keeps -EAGAIN */
	if (!peek)
	{
		s->tail = tail;
		s->count = count;
		if (i > 0)
			poll_wake_check();
	}
	return (ssize_t)i;
}

ssize_t sock_stream_recv(struct sock_stream *s, void *buf, size_t len)
{
	return sock_stream_recv_flags(s, buf, len, 0);
}

int sock_stream_set_reuseaddr(struct sock_stream *s, int on)
{
	if (!s)
		return -EINVAL;
	s->reuseaddr = on ? 1 : 0;
	return 0;
}

int sock_stream_get_reuseaddr(const struct sock_stream *s)
{
	return s ? (int)s->reuseaddr : 0;
}

int sock_stream_take_so_error(struct sock_stream *s)
{
	int err;

	if (!s)
		return 0;
	if (s->state == SS_CONNECTING)
		(void)sock_stream_wire_progress(s);
	err = s->so_error;
	s->so_error = 0;
	return err;
}

int sock_stream_set_timeout_ms(struct sock_stream *s, int is_rcv, uint64_t ms)
{
	if (!s)
		return -EINVAL;
	if (is_rcv)
		s->rcv_timeout_ms = ms;
	else
		s->snd_timeout_ms = ms;
	return 0;
}

uint64_t sock_stream_get_timeout_ms(const struct sock_stream *s, int is_rcv)
{
	if (!s)
		return 0;
	return is_rcv ? s->rcv_timeout_ms : s->snd_timeout_ms;
}

static uint8_t sock_stream_linux_st(enum ss_state st)
{
	switch (st)
	{
	case SS_CONNECTED:
		return 0x01; /* TCP_ESTABLISHED */
	case SS_CONNECTING:
		return 0x02; /* TCP_SYN_SENT */
	case SS_LISTEN:
		return 0x0A; /* TCP_LISTEN */
	case SS_BOUND:
		return 0x07; /* TCP_CLOSE */
	default:
		return 0x07;
	}
}

int sock_stream_inet_walk(int (*cb)(const struct sock_stream_inet_snap *s,
				    void *ctx),
			  void *ctx)
{
	int i;
	struct sock_stream_inet_snap snap;

	if (!cb)
		return -EINVAL;

	for (i = 0; i < SS_MAX; i++)
	{
		struct sock_stream *s = &g_socks[i];

		if (!s->in_use || s->magic != SS_MAGIC)
			continue;
		if (s->family != IR0_AF_INET || s->state == SS_IDLE)
			continue;

		memset(&snap, 0, sizeof(snap));
#if CONFIG_ENABLE_NETWORKING
		snap.local_ip = (uint32_t)ip_local_addr;
#else
		snap.local_ip = 0;
#endif
	snap.local_port = s->wire_local_port ? s->wire_local_port : s->port;
		if (s->state == SS_CONNECTED || s->state == SS_CONNECTING)
		{
			snap.rem_ip = s->wire_peer_ip;
			snap.rem_port = s->wire_peer_port;
		}
		snap.st = sock_stream_linux_st(s->state);
		snap.inode = (unsigned long)(uintptr_t)s;
		if (cb(&snap, ctx) != 0)
			return -1;
	}
	return 0;
}

int sock_stream_unix_walk(int (*cb)(const struct sock_stream_unix_snap *s,
				    void *ctx),
			  void *ctx)
{
	int i;
	struct sock_stream_unix_snap snap;

	if (!cb)
		return -EINVAL;

	for (i = 0; i < SS_MAX; i++)
	{
		struct sock_stream *s = &g_socks[i];

		if (!s->in_use || s->magic != SS_MAGIC)
			continue;
		if (s->family != IR0_AF_UNIX || s->state == SS_IDLE)
			continue;

		memset(&snap, 0, sizeof(snap));
		snap.inode = (unsigned long)(uintptr_t)s;
		snap.refcnt = (unsigned)(s->fd_refs > 0 ? s->fd_refs : 1);
		snap.type = 1; /* SOCK_STREAM */
		snap.st = sock_stream_linux_st(s->state);
		snap.path_len = s->path_len;
		snap.is_abstract = s->is_abstract;
		if (s->path_len > 0 && s->path_len < sizeof(snap.path))
			memcpy(snap.path, s->path, s->path_len);
		if (cb(&snap, ctx) != 0)
			return -1;
	}
	return 0;
}
