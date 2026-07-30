/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp.h
 * Description: Minimal wire TCP (client + listen/accept) for F8 slices.
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stddef.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

struct tcp_header
{
	uint16_t src_port;
	uint16_t dest_port;
	uint32_t seq_num;
	uint32_t ack_num;
	uint8_t data_offset;
	uint8_t flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_ptr;
} __attribute__((packed));

#define TCP_HDR_LEN 20

int tcp_init(void);
int tcp_wire_connect(ip4_addr_t peer_ip, uint16_t peer_port,
		   uint16_t *local_port_out, uint32_t *seq_out, uint32_t *ack_out);
/* Nonblocking connect: SYN sent; complete via tcp_wire_connect_poll. */
int tcp_wire_connect_start(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t *local_port_out);
/* 0=established, -EAGAIN=still SYN_SENT, other negative = fail. */
int tcp_wire_connect_poll(ip4_addr_t peer_ip, uint16_t peer_port,
			  uint16_t local_port, uint32_t *seq_out,
			  uint32_t *ack_out);
int tcp_wire_poll_readable(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port);
int tcp_wire_poll_writable(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port);
int tcp_wire_send(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  uint32_t *seq_io, uint32_t ack_peer, const void *data, size_t len);
void tcp_wire_close(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		    uint32_t seq, uint32_t ack_peer);

/* Host→guest listen path (QEMU user-net hostfwd). */
int tcp_wire_listen_register(uint16_t port);
void tcp_wire_listen_unregister(uint16_t port);
int tcp_wire_accept_take(uint16_t listen_port, ip4_addr_t *peer_ip,
			 uint16_t *peer_port, uint16_t *local_port,
			 uint32_t *seq_out, uint32_t *ack_out);
int tcp_wire_recv(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  void *buf, size_t len);
/** Latest peer_ack for an inbound wire conn (0 if none). */
uint32_t tcp_wire_peer_ack(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port);

void tcp_receive_handler(struct net_device *dev, const void *data, size_t len,
			 void *priv);
