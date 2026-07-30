/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_stream.h
 * Description: Minimal AF_UNIX + TCP loopback stream sockets.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>

#define IR0_AF_UNIX 1
#define IR0_AF_INET 2
#define IR0_SOCK_STREAM 1

#define SOCK_STREAM_RIGHTS_MAX 4
#define SOCK_STREAM_RIGHTS_ENTRY_SIZE 320

struct sock_stream;

struct sock_stream *sock_stream_create(int family);
void sock_stream_release(struct sock_stream *s);
int sock_stream_bind_unix(struct sock_stream *s, const char *path);
int sock_stream_bind_unix_n(struct sock_stream *s, const char *path, size_t path_len,
			    int is_abstract);
int sock_stream_listen(struct sock_stream *s, int backlog);
int sock_stream_connect_unix(struct sock_stream *s, const char *path);
int sock_stream_connect_unix_n(struct sock_stream *s, const char *path, size_t path_len,
			       int is_abstract);
struct sock_stream *sock_stream_accept(struct sock_stream *s);
int sock_stream_bind_inet(struct sock_stream *s, uint16_t port);
int sock_stream_connect_inet(struct sock_stream *s, uint32_t addr, uint16_t port);
ssize_t sock_stream_send(struct sock_stream *s, const void *buf, size_t len);
ssize_t sock_stream_recv(struct sock_stream *s, void *buf, size_t len);
ssize_t sock_stream_recv_flags(struct sock_stream *s, void *buf, size_t len, int flags);
int sock_stream_set_reuseaddr(struct sock_stream *s, int on);
int sock_stream_get_reuseaddr(const struct sock_stream *s);
int sock_stream_is(const void *ptr);
/* Address in g_socks[] even if already released (magic cleared). */
int sock_stream_is_slot(const void *ptr);
void sock_stream_acquire(struct sock_stream *s);
int sock_stream_socketpair(struct sock_stream **a_out, struct sock_stream **b_out);

int sock_stream_rights_push(struct sock_stream *recv_side, const void *entry, size_t sz);
int sock_stream_rights_pop(struct sock_stream *s, void *entry, size_t sz);
int sock_stream_rights_count(const struct sock_stream *s);
void sock_stream_set_rights_dtor(void (*dtor)(void *entry, size_t sz));

int sock_stream_shutdown(struct sock_stream *s, int how);
int sock_stream_get_unix_name(const struct sock_stream *s, char *path_out, size_t path_cap,
			      size_t *path_len_out, int *is_abstract_out);
int sock_stream_poll_readable(const struct sock_stream *s);
int sock_stream_poll_writable(const struct sock_stream *s);
int sock_stream_family(const struct sock_stream *s);
int sock_stream_buf_space(const struct sock_stream *peer_of_sender);
int sock_stream_buf_count(const struct sock_stream *s);
int sock_stream_is_recv_shutdown(const struct sock_stream *s);
struct sock_stream *sock_stream_get_peer(struct sock_stream *s);

/* /proc/net/{tcp,unix} inventory (BusyBox netstat). */
struct sock_stream_inet_snap
{
	uint32_t local_ip;
	uint16_t local_port;
	uint32_t rem_ip;
	uint16_t rem_port;
	uint8_t st; /* Linux tcp_state hex */
	unsigned long inode;
};

struct sock_stream_unix_snap
{
	unsigned long inode;
	unsigned refcnt;
	uint8_t type; /* SOCK_STREAM = 1 */
	uint8_t st;   /* 01 connected, 0A listen, 07 unbound-ish */
	char path[108];
	size_t path_len;
	int is_abstract;
};

int sock_stream_inet_walk(int (*cb)(const struct sock_stream_inet_snap *s,
				    void *ctx),
			  void *ctx);
int sock_stream_unix_walk(int (*cb)(const struct sock_stream_unix_snap *s,
				    void *ctx),
			  void *ctx);
