/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ip.h
 * Description: IPv4 protocol header and API definitions
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* IPv4 Header Structure (RFC 791) */
struct ip_header
{
    uint8_t version_ihl;      /* Version (4 bits) + IHL (4 bits) */
    uint8_t tos;              /* Type of Service */
    uint16_t total_len;       /* Total Length */
    uint16_t id;              /* Identification */
    uint16_t flags_frag_off;  /* Flags (3 bits) + Fragment Offset (13 bits) */
    uint8_t ttl;              /* Time To Live */
    uint8_t protocol;         /* Next header (IANA): ICMP=1, UDP=17 in IR0 */
    uint16_t checksum;        /* Header Checksum */
    uint32_t src_addr;        /* Source Address */
    uint32_t dest_addr;       /* Destination Address */
} __attribute__((packed));

/* IP Header Macros */
#define IP_VERSION(ip) ((ip->version_ihl >> 4) & 0x0F)
#define IP_IHL(ip) (ip->version_ihl & 0x0F)
#define IP_HEADER_LEN(ip) (IP_IHL(ip) * 4)
#define IP_FLAGS(ip) ((ntohs(ip->flags_frag_off) >> 13) & 0x07)
#define IP_FRAG_OFFSET(ip) (ntohs(ip->flags_frag_off) & 0x1FFF)

/* IP Flags */
#define IP_FLAG_MF (1 << 0)  /* More Fragments */
#define IP_FLAG_DF (1 << 1)  /* Don't Fragment */

/* IP Address Configuration */
extern ip4_addr_t ip_local_addr;
extern ip4_addr_t ip_netmask;
extern ip4_addr_t ip_gateway;
struct ip_rx_context
{
    ip4_addr_t src_addr;
    ip4_addr_t dest_addr;
    uint8_t ttl;
};

/* IP Protocol API */
int ip_init(void);
int ip_send(struct net_device *dev, ip4_addr_t dest_ip, uint8_t protocol, 
            const void *payload, size_t len);
void ip_receive_handler(struct net_device *dev, const void *data, 
                        size_t len, void *priv);

/* Routing API */
int ip_route_add(ip4_addr_t dest_network, ip4_addr_t netmask, ip4_addr_t gateway);
int ip_route_del(ip4_addr_t dest_network, ip4_addr_t netmask);
int ip_route_walk(int (*cb)(ip4_addr_t dest, ip4_addr_t mask, ip4_addr_t gw,
			     void *ctx),
		  void *ctx);
int ip_routes_seed_from_globals(void);

/* IP Address Utilities */
static inline ip4_addr_t ip_make_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return htonl((a << 24) | (b << 16) | (c << 8) | d);
}

static inline void ip_format_addr(ip4_addr_t ip, char *buf, size_t buf_len)
{
    /* Format IP address as "XXX.XXX.XXX.XXX" string
     * @ip: IP address in network byte order
     * @buf: Output buffer for formatted string
     * @buf_len: Size of output buffer (must be at least 16 bytes)
     */
    if (!buf || buf_len < 16)
        return;
    
    /* Extract bytes from IP address (network byte order) */
    uint32_t host = ntohl(ip);
    uint8_t b1 = (host >> 24) & 0xFF;
    uint8_t b2 = (host >> 16) & 0xFF;
    uint8_t b3 = (host >> 8) & 0xFF;
    uint8_t b4 = host & 0xFF;
    
    /* Format as "XXX.XXX.XXX.XXX" */
    /* Note: snprintf is declared in <string.h> */
    snprintf(buf, buf_len, "%d.%d.%d.%d", b1, b2, b3, b4);
}

/* IP Checksum Calculation */
uint16_t ip_checksum(const void *data, size_t len);

/* Macros for IP address formatting (matching ARP) */
#define IP4_FMT "%d.%d.%d.%d"
#define IP4_ARGS(ip) \
    (int)((ip >> 24) & 0xFF), \
    (int)((ip >> 16) & 0xFF), \
    (int)((ip >> 8) & 0xFF), \
    (int)(ip & 0xFF)

