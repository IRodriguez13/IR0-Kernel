/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net.h
 * Description: Common networking types and macros for IR0 networking implementation.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Byte order conversion macros (Big Endian <-> Little Endian) */
/* IR0 is x86-64 (Little Endian) */

#define htons(n) (((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8))
#define ntohs(n) htons(n)

#define htonl(n) (((((uint32_t)(n) & 0x000000FF)) << 24) | \
                  ((((uint32_t)(n) & 0x0000FF00)) << 8) |  \
                  ((((uint32_t)(n) & 0x00FF0000)) >> 8) |  \
                  ((((uint32_t)(n) & 0xFF000000)) >> 24))
#define ntohl(n) htonl(n)

/* Common Ethernet Types */
#define ETHERTYPE_IP 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IPV6 0x86DD

/* Common IP Protocol Numbers */
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* Ethernet header (14 bytes) */
struct eth_header
{
    uint8_t dest[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

/* IPv4 Address type */
typedef uint32_t ip4_addr_t;

/* MAC Address type */
typedef uint8_t mac_addr_t[6];

/* Function to create an IP address frcat om 4 octets (network byte order) */
static inline ip4_addr_t make_ip4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    /* Match ip_make_addr(): host-order octet quad, then htonl for wire order */
    return htonl(((uint32_t)a << 24) | ((uint32_t)b << 16) |
                 ((uint32_t)c << 8) | (uint32_t)d);
}

/* --- Networking Abstraction Layer --- */

/*
 * Linux uapi netdevice flags (include/uapi/linux/if.h).
 * BusyBox ifconfig/route interpret these bits; IR0 must match.
 */
#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS  0x20
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MULTICAST   0x1000

struct net_device
{
    const char *name;
    mac_addr_t mac;
    uint32_t flags;
    size_t mtu;
    void *priv; /* Driver private data */

    /* Driver operations */
    int (*send)(struct net_device *dev, void *data, size_t len);
    void (*poll)(struct net_device *dev);
    int (*get_irq_line)(struct net_device *dev);
    int (*handle_irq)(struct net_device *dev, uint8_t irq);
    void (*get_stats)(struct net_device *dev, uint64_t *rx_pkts, uint64_t *tx_pkts,
                      uint64_t *rx_errs, uint64_t *tx_errs);
    /* Optional; NULL → sysfs reports 0 bytes (packets may still be non-zero). */
    void (*get_byte_stats)(struct net_device *dev, uint64_t *rx_bytes,
			   uint64_t *tx_bytes);

    struct net_device *next;
};

/* --- Protocol Registration System --- */

/**
 * Protocol handler function type
 * @dev: Network device that received the packet
 * @data: Pointer to protocol payload (after Ethernet/IP headers)
 * @len: Length of protocol payload
 * @priv: Private data passed during registration
 */
typedef void (*net_protocol_handler_t)(struct net_device *dev, const void *data, size_t len, void *priv);

/**
 * Network protocol registration structure
 */
struct net_protocol
{
    const char *name;              /* Protocol name (e.g., "ARP", "IP", "ICMP") */
    uint16_t ethertype;            /* Ethernet type for Layer 2 protocols (ARP, IP) */
    uint8_t ipproto;               /* IP protocol number for Layer 3+ protocols (ICMP, TCP, UDP) */
    net_protocol_handler_t handler; /* Handler function */
    void *priv;                    /* Private data passed to handler */
    
    struct net_protocol *next;
};

/* Core Networking API */
int net_register_device(struct net_device *dev);
void net_unregister_device(struct net_device *dev);
struct net_device *net_get_devices(void);

/* Protocol Registration API */
int net_register_protocol(struct net_protocol *proto);
void net_unregister_protocol(struct net_protocol *proto);
struct net_protocol *net_find_protocol_by_ethertype(uint16_t ethertype);
struct net_protocol *net_find_protocol_by_ipproto(uint8_t ipproto);

/* Protocol to Driver */
int net_send(struct net_device *dev, uint16_t ethertype, const uint8_t *dest_mac, const void *payload, size_t len);

/* Driver to Protocol */
void net_receive(struct net_device *dev, const void *data, size_t len);

/* Network Stack Initialization */
int init_net_stack(void);
int net_stack_post_irq_init(void);

/* Network Polling (for receiving packets when not actively waiting) */
void net_poll(void);
void net_stack_poll(void);

/* Driver-agnostic IRQ and stats helpers for kernel core callsites */
int net_stack_get_irq_line(void);
int net_stack_handle_irq(uint8_t irq);
void net_stack_get_stats(uint64_t *rx_pkts, uint64_t *tx_pkts,
                         uint64_t *rx_errs, uint64_t *tx_errs);

/* Ping result structure for syscalls */
struct ping_result {
    int success;          /* 1 if ping succeeded, 0 if failed */
    uint16_t seq;         /* ICMP sequence number */
    uint64_t rtt;         /* Round-trip time in milliseconds */
    uint8_t ttl;          /* TTL from reply */
    size_t payload_bytes; /* Payload size in reply */
    ip4_addr_t reply_ip;  /* IP address that replied */
};

/* Shared IPv4 runtime configuration exposed to networking control paths. */
extern ip4_addr_t ip_local_addr;
extern ip4_addr_t ip_netmask;
extern ip4_addr_t ip_gateway;

/* Soft route table walk (BusyBox /proc/net/route). */
int ip_route_walk(int (*cb)(ip4_addr_t dest, ip4_addr_t mask, ip4_addr_t gw,
			     void *ctx),
		  void *ctx);
int ip_route_add(ip4_addr_t dest_network, ip4_addr_t netmask, ip4_addr_t gateway);
int ip_route_del(ip4_addr_t dest_network, ip4_addr_t netmask);
int ip_routes_seed_from_globals(void);

/* ICMP helpers used by /dev/net control plane. */
bool icmp_get_next_echo_result(uint16_t id, uint16_t *seq_out, uint64_t *rtt_out,
                               uint8_t *ttl_out, size_t *payload_bytes_out,
                               ip4_addr_t *reply_ip_out);
bool icmp_has_ready_echo_result(uint16_t id);
uint16_t icmp_allocate_echo_seq(void);
int icmp_send_echo_request(struct net_device *dev, ip4_addr_t dest_ip,
                           uint16_t id, uint16_t seq, const void *data, size_t len);

/* DNS resolver used by /dev/net ping hostname path. */
ip4_addr_t dns_resolve(const char *domain_name, ip4_addr_t dns_server_ip);
void dns_set_default_server(ip4_addr_t dns_server_ip);
ip4_addr_t dns_get_default_server(void);

/* ARP IP synchronization helpers used by NET_SET_CONFIG. */
void arp_set_my_ip(ip4_addr_t ip);
int arp_set_interface_ip(struct net_device *dev, ip4_addr_t ip);
