/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ip.c
 * Description: IPv4 protocol implementation
 */

#include "ip.h"
#include "arp.h"
#include "net.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/errno.h>
#include <ir0/klog.h>
#include <ir0/clock.h>
#include <string.h>

/* IP Configuration (default values) */
ip4_addr_t ip_local_addr = 0;  /* Will be set during initialization */
ip4_addr_t ip_netmask = 0;
ip4_addr_t ip_gateway = 0;     /* Default gateway (for backward compatibility) */

/* Simple routing table entry */
struct ip_route_entry {
    ip4_addr_t dest_network;
    ip4_addr_t netmask;
    ip4_addr_t gateway;        /* 0 means direct (no gateway) */
    struct ip_route_entry *next;
};

/* Routing table: linked list of routes */
#define MAX_ROUTES 16
static struct ip_route_entry *ip_routes = NULL;
static int ip_route_count = 0;

/* IP Protocol registration */
static struct net_protocol ip_proto;

/* Broadcast IP address */
static const ip4_addr_t broadcast_ip = 0xFFFFFFFF; /* 255.255.255.255 */

/* IP Fragmentation: Simple fragmentation counter for unique fragment IDs */
static uint16_t ip_frag_id_counter = 1;

/*
 * Fragmentation policy contract:
 * - RX reassembly is currently unsupported, so fragmented inbound datagrams
 *   are dropped and counted.
 * - TX fragmentation is enabled to keep large datagrams routable.
 */
#define IP_RX_REASSEMBLY_SUPPORTED 0
#define IP_TX_FRAGMENTATION_ENABLED 1

/*
 * Inbound IPv4 fragments are dropped: no reassembly buffer. Count drops and
 * rate-limit serial noise (log every Nth drop).
 */
static uint64_t ip_rx_frag_dropped;
#define IP_RX_FRAG_DROP_LOG_EVERY 64U

static int ip_count_bits(uint32_t value)
{
    int count = 0;
    while (value) {
        count += (value & 1);
        value >>= 1;
    }
    return count;
}

/**
 * ip_checksum - Calculate IP header checksum (RFC 1071)
 *
 * The IP checksum is a 16-bit one's complement sum of all 16-bit words in the
 * IP header. This provides basic error detection for header corruption during
 * transmission. The algorithm:
 *
 *   1. Sum all 16-bit words (convert from network byte order first)
 *   2. Handle odd-length headers by padding with zero byte
 *   3. Fold 32-bit sum to 16 bits (add carry bits)
 *   4. Take one's complement (bitwise NOT)
 *
 * The checksum field in the header is set to zero during calculation, then
 * filled with the computed value. On receive, we verify by recalculating
 * (including the received checksum) - the result should be 0xFFFF if valid.
 *
 * This is the standard Internet checksum algorithm used by IP, ICMP, and UDP.
 *
 * @data: Pointer to IP header (or any data to checksum)
 * @len: Length of data in bytes (must be even for optimal performance)
 * @return: 16-bit checksum in network byte order
 */
uint16_t ip_checksum(const void *data, size_t len)
{
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;
    size_t i;

    /* Sum all 16-bit words. We convert from network byte order (big-endian)
     * to host byte order (little-endian on x86) before summing. The checksum
     * itself is stored in network byte order, but intermediate calculations
     * are done in host byte order for efficiency.
     */
    for (i = 0; i < len / 2; i++)
    {
        sum += ntohs(words[i]);
    }

    /* Handle odd-length headers. IP header is always 20+ bytes (multiple of 4),
     * but this function is generic and might be called for other data. If length
     * is odd, we pad the last byte with zeros in the high byte position.
     */
    if (len & 1)
    {
        sum += ((uint8_t *)data)[len - 1] << 8;
    }

    /* Fold 32-bit sum to 16 bits by adding carry bits. This is equivalent to
     * repeatedly adding the high 16 bits to the low 16 bits until no carry
     * remains. The result fits in 16 bits.
     */
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Return one's complement (bitwise NOT) in network byte order. The one's
     * complement ensures that summing the header words (including checksum)
     * yields 0xFFFF when the header is valid.
     */
    return htons(~sum);
}

/**
 * ip_receive_handler - Handle incoming IP packets
 * @dev: Network device that received the packet
 * @data: Pointer to IP packet data
 * @len: Length of IP packet
 * @priv: Private data (unused, provided for protocol handler signature compatibility)
 */
void ip_receive_handler(struct net_device *dev, const void *data, 
                        size_t len, void *priv)
{
    (void)priv;

    if (len < sizeof(struct ip_header))
    {
        LOG_WARNING("IP", "Packet too short");
        return;
    }

    const struct ip_header *ip = (const struct ip_header *)data;

    /* Validate IP version */
    if (IP_VERSION(ip) != 4)
    {
        LOG_WARNING("IP", "Invalid IP version");
        return;
    }

    /* Validate header length */
    uint8_t ihl = IP_IHL(ip);
    if (ihl < 5)
    {
        LOG_WARNING("IP", "Invalid IP header length");
        return;
    }

    size_t header_len = ihl * 4;
    if (len < header_len)
    {
        LOG_WARNING("IP", "Packet shorter than header length");
        return;
    }

    uint16_t total_len = ntohs(ip->total_len);
    if (total_len > len || total_len < header_len)
    {
        LOG_WARNING("IP", "Total length inconsistent with buffer or header");
        return;
    }

    /* Verify checksum without leaving the RX buffer modified (work on a copy) */
    uint16_t received_checksum = ip->checksum;
    uint8_t ip_hdr_scratch[60]; /* IPv4 header length is at most 15 * 4 bytes */
    if (header_len > sizeof(ip_hdr_scratch))
    {
        LOG_WARNING("IP", "IP header length exceeds maximum");
        return;
    }
    memcpy(ip_hdr_scratch, data, header_len);
    struct ip_header *ip_verify = (struct ip_header *)ip_hdr_scratch;
    ip_verify->checksum = 0;
    uint16_t calculated_checksum = ip_checksum(ip_hdr_scratch, header_len);

    if (received_checksum != calculated_checksum)
    {
        LOG_WARNING("IP", "Checksum mismatch");
        return;
    }

    /* Check if packet is for us */
    ip4_addr_t dest_ip = ip->dest_addr;
    ip4_addr_t src_ip = ip->src_addr;
    
    /* Check interface-specific IPs too */
    bool is_for_us = (dest_ip == ip_local_addr || dest_ip == broadcast_ip);
    if (!is_for_us)
    {
        ip4_addr_t interface_ip;
        if (arp_get_interface_ip(dev, &interface_ip) == 0 && dest_ip == interface_ip)
        {
            is_for_us = true;
        }
    }
    
    if (!is_for_us)
    {
        /* Not for us - check if it's multicast (common spam: mDNS, SSDP, etc.) */
        /* Multicast IPs: 224.0.0.0/4 (224.0.0.0 to 239.255.255.255) */
        uint8_t first_octet = (ntohl(dest_ip) >> 24) & 0xFF;
        bool is_multicast_ip = (first_octet >= 224 && first_octet <= 239);
        
        /* Only log non-multicast drops (multicast spam is normal) */
        if (!is_multicast_ip)
        {
            ip4_addr_t interface_ip = 0;
            arp_get_interface_ip(dev, &interface_ip);
            
            /* CRITICAL: Always log drops of ICMP Echo Replies (ping responses) */
            /* This helps debug why ping to 8.8.8.8 isn't working */
            bool is_icmp_reply = false;
            uint8_t protocol_check = ip->protocol;
            if (protocol_check == IPPROTO_ICMP && len >= header_len + 4)
            {
                const uint8_t *icmp_data = (const uint8_t *)data + header_len;
                uint8_t icmp_type = icmp_data[0];
                is_icmp_reply = (icmp_type == 0); /* ICMP_TYPE_ECHO_REPLY */
            }
            
            if (is_icmp_reply)
            {
                LOG_WARNING_FMT("IP", "DROPPING ICMP Echo Reply: dest=" IP4_FMT " != local=" IP4_FMT " (interface=" IP4_FMT ") - This may explain why ping to external IPs fails!",
                               IP4_ARGS(ntohl(dest_ip)), IP4_ARGS(ntohl(ip_local_addr)), 
                               IP4_ARGS(ntohl(interface_ip)));
            }
            else
            {
                LOG_DEBUG_FMT("IP", "Dropping packet: dest=" IP4_FMT " != local=" IP4_FMT " (interface=" IP4_FMT ")", 
                             IP4_ARGS(ntohl(dest_ip)), IP4_ARGS(ntohl(ip_local_addr)), 
                             IP4_ARGS(ntohl(interface_ip)));
            }
        }
        /* Multicast packets are dropped silently to reduce log spam */
        return;
    }
    
    /* Log that we accepted the packet for debugging */
    LOG_DEBUG_FMT("IP", "Packet accepted: dest=" IP4_FMT " matches local=" IP4_FMT,
                  IP4_ARGS(ntohl(dest_ip)), IP4_ARGS(ntohl(ip_local_addr)));

    /* Check if packet is fragmented */
    uint16_t frag_offset = IP_FRAG_OFFSET(ip);
    uint16_t flags = IP_FLAGS(ip);
    bool is_fragment = (frag_offset != 0) || (flags & IP_FLAG_MF);
    uint16_t frag_id = ntohs(ip->id);
    
    /* Extract protocol number */
    uint8_t protocol = ip->protocol;
    
    if (is_fragment && !IP_RX_REASSEMBLY_SUPPORTED)
    {
        (void)frag_id;
        (void)frag_offset;
        /*
         * Fragment reassembly is not implemented; upper layers only see
         * contiguous payloads after a single IP header.
         */
        ip_rx_frag_dropped++;
        if (ip_rx_frag_dropped == 1U ||
            (ip_rx_frag_dropped % IP_RX_FRAG_DROP_LOG_EVERY) == 0U)
        {
            klog_print("[IP] dropped IPv4 fragment (no reassembly), total drops=0x");
            klog_hex64(ip_rx_frag_dropped);
            klog_print("\n");
        }
        return;
    }

    /* Normal (unfragmented) packet processing */
    LOG_DEBUG_FMT("IP", "RX packet: proto=%d src=" IP4_FMT " dst=" IP4_FMT,
                  (int)protocol, IP4_ARGS(ntohl(src_ip)), IP4_ARGS(ntohl(dest_ip)));

#if KERNEL_DEBUG
    /* Enhanced serial-only logging for ICMP diagnostics. */
    if (protocol == IPPROTO_ICMP && len >= header_len + 4)
    {
        const uint8_t *icmp_data = (const uint8_t *)data + header_len;
        uint8_t icmp_type = icmp_data[0];
        uint8_t icmp_code = icmp_data[1];
        klog_print("[IP] RX ICMP: type=");
        char type_str[16];
        itoa((int)icmp_type, type_str, 10);
        klog_print(type_str);
        klog_print(" code=");
        char code_str[16];
        itoa((int)icmp_code, code_str, 10);
        klog_print(code_str);
        if (icmp_type == 0)
        {
            klog_print(" (ECHO_REPLY)");
            /* Dump ICMP header for Echo Reply */
            if (len >= header_len + 8)
            {
                uint16_t icmp_id = (icmp_data[4] << 8) | icmp_data[5];
                uint16_t icmp_seq = (icmp_data[6] << 8) | icmp_data[7];
                klog_print(" id=");
                char id_str[16];
                itoa((int)icmp_id, id_str, 10);
                klog_print(id_str);
                klog_print(" seq=");
                char seq_str[16];
                itoa((int)icmp_seq, seq_str, 10);
                klog_print(seq_str);
            }
        }
        klog_print("\n");
    }
#endif

    /* Look up protocol handler by IP protocol number */
    struct net_protocol *proto = net_find_protocol_by_ipproto(protocol);
    if (proto && proto->handler)
    {
        /* Extract payload (after IP header) */
        const void *payload = (const uint8_t *)data + header_len;
        size_t payload_len = (size_t)total_len - header_len;
        struct ip_rx_context rx_ctx;

        rx_ctx.src_addr = src_ip;
        rx_ctx.dest_addr = dest_ip;
        rx_ctx.ttl = ip->ttl;

        /* Call protocol handler (ICMP, UDP in this stack) */
        proto->handler(dev, payload, payload_len, &rx_ctx);
    }
    else
    {
        LOG_WARNING_FMT("IP", "No handler registered for IP protocol %d", (int)protocol);
    }
}

/**
 * ip_send - Send an IP packet
 * @dev: Network device to send on
 * @dest_ip: Destination IP address
 * @protocol: IP protocol number (IPPROTO_ICMP, IPPROTO_UDP)
 * @payload: Payload data
 * @len: Payload length
 * @return: 0 on success, -1 on error
 */
/**
 * ip_send_fragment - Send a single IP fragment
 * Helper function for fragmentation
 */
static int ip_send_fragment(struct net_device *dev, ip4_addr_t dest_ip, 
                            uint8_t protocol, const void *payload, size_t len,
                            uint16_t frag_id, uint16_t frag_offset, bool more_fragments,
                            ip4_addr_t next_hop_ip, uint8_t ttl)
{
    size_t ip_header_len = sizeof(struct ip_header);
    size_t fragment_len = ip_header_len + len;
    
    uint8_t *fragment = kmalloc(fragment_len);
    if (!fragment)
        return -1;
    
    struct ip_header *ip = (struct ip_header *)fragment;
    
    /* Fill IP header for fragment */
    ip->version_ihl = (4 << 4) | 5;
    ip->tos = 0;
    ip->total_len = htons(fragment_len);
    ip->id = htons(frag_id);
    
    /* Set fragmentation flags: MF (More Fragments) if not last fragment,
     * and fragment offset (in 8-byte units)
     */
    uint16_t flags_frag = (frag_offset >> 3) & 0x1FFF;
    if (more_fragments)
        flags_frag |= IP_FLAG_MF;
    ip->flags_frag_off = htons(flags_frag);
    
    ip->ttl = ttl ? ttl : 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    /* Source IP: use interface-specific IP if available, else default */
    ip4_addr_t src_ip = ip_local_addr;
    ip4_addr_t interface_ip;
    if (arp_get_interface_ip(dev, &interface_ip) == 0)
    {
        src_ip = interface_ip;
    }
    ip->src_addr = src_ip;
    ip->dest_addr = dest_ip;
    
    /* Calculate checksum */
    ip->checksum = ip_checksum(ip, ip_header_len);
    
    /* Copy fragment payload */
    memcpy(fragment + ip_header_len, payload, len);
    
    /* Resolve MAC and send */
    uint8_t dest_mac[6];
    if (arp_resolve(dev, next_hop_ip, dest_mac) != 0)
    {
        kfree(fragment);
        return -1;
    }
    
    int ret = net_send(dev, ETHERTYPE_IP, dest_mac, fragment, fragment_len);
    kfree(fragment);
    return ret;
}

int ip_send(struct net_device *dev, ip4_addr_t dest_ip, uint8_t protocol,
            const void *payload, size_t len)
{
	return ip_send_ttl(dev, dest_ip, protocol, payload, len, 64);
}

int ip_send_ttl(struct net_device *dev, ip4_addr_t dest_ip, uint8_t protocol,
		const void *payload, size_t len, uint8_t ttl)
{
    if (!dev || !payload)
        return -1;

    if (dev->mtu <= sizeof(struct eth_header) + sizeof(struct ip_header))
    {
        klog_print("IP: ip_send: MTU too small for Ethernet and IP headers\n");
        return -1;
    }

    /* Calculate maximum payload size per fragment.
     * MTU includes Ethernet header (14 bytes), so max IP packet size is
     * MTU - 14. Max payload per fragment is packet size - IP header (20 bytes).
     * We round down to 8-byte boundary (fragmentation requirement).
     */
    size_t max_payload = ((dev->mtu - sizeof(struct eth_header) - sizeof(struct ip_header)) / 8) * 8;
    size_t ip_header_len = sizeof(struct ip_header);
    size_t total_len = ip_header_len + len;
    
    /* Don't send packets to our own IP - this shouldn't happen, but if it does, drop */
    ip4_addr_t src_ip = ip_local_addr;
    ip4_addr_t interface_ip;
    if (arp_get_interface_ip(dev, &interface_ip) == 0)
    {
        src_ip = interface_ip;
    }
    
    if (dest_ip == src_ip || dest_ip == ip_local_addr)
    {
        LOG_WARNING_FMT("IP", "Attempted to send packet to our own IP " IP4_FMT ", dropping", 
                       IP4_ARGS(ntohl(dest_ip)));
        return -1;
    }
    
    /* Routing: Look up route in routing table */
    ip4_addr_t next_hop_ip = dest_ip;
    
    /* Search routing table for matching route (longest prefix match) */
    struct ip_route_entry *best_route = NULL;
    int best_match_bits = -1;
    
    struct ip_route_entry *route = ip_routes;
    while (route)
    {
        ip4_addr_t route_network = dest_ip & route->netmask;
        if (route_network == route->dest_network)
        {
            /* Count matching bits (longest prefix match) */
            int match_bits = 0;
            ip4_addr_t mask = route->netmask;
            while (mask & 0x80000000)
            {
                match_bits++;
                mask <<= 1;
            }
            
            if (match_bits > best_match_bits)
            {
                best_match_bits = match_bits;
                best_route = route;
            }
        }
        route = route->next;
    }
    
    /* If route found, use it; otherwise use default gateway or direct */
    if (best_route)
    {
        if (best_route->gateway != 0)
        {
            next_hop_ip = best_route->gateway;
            LOG_INFO_FMT("IP", "Routing to " IP4_FMT " via route gateway " IP4_FMT,
                        IP4_ARGS(ntohl(dest_ip)), IP4_ARGS(ntohl(best_route->gateway)));
        }
        /* else: gateway is 0, means direct route, use dest_ip */
    }
    else
    {
        /* No route found, fall back to default gateway logic */
        if (ip_netmask != 0)
        {
            ip4_addr_t local_network = ip_local_addr & ip_netmask;
            ip4_addr_t dest_network = dest_ip & ip_netmask;
            
            if (local_network != dest_network && ip_gateway != 0)
            {
                next_hop_ip = ip_gateway;
                LOG_INFO_FMT("IP", "Routing to " IP4_FMT " via default gateway " IP4_FMT,
                            IP4_ARGS(ntohl(dest_ip)), IP4_ARGS(ntohl(ip_gateway)));
            }
        }
    }
    
    /* If packet fits in one fragment, send normally */
    if (total_len <= dev->mtu - sizeof(struct eth_header))
    {
        uint8_t *ip_packet = kmalloc(total_len);
        if (!ip_packet)
            return -1;

        struct ip_header *ip = (struct ip_header *)ip_packet;

        /* Fill IP header fields according to IPv4 specification (RFC 791).
         * Version 4, IHL 5 means 5 * 4 = 20 bytes header (standard size, no options).
         * TTL (Time To Live) is set to 64 hops, a common default that prevents
         * packets from looping forever if routing is misconfigured.
         */
        ip->version_ihl = (4 << 4) | 5;  /* Version 4, IHL 5 (20 bytes) */
        ip->tos = 0;                     /* Type of Service: 0 (normal precedence) */
        ip->total_len = htons(total_len); /* Total packet length (header + payload) */
        uint16_t frag_id = ip_frag_id_counter++;
        ip->id = htons(frag_id);  /* Unique fragment ID */
        ip->flags_frag_off = 0;         /* No fragmentation flags (unfragmented packet) */
        ip->ttl = ttl ? ttl : 64;            /* Time To Live */
        ip->protocol = protocol;         /* Upper-layer protocol (ICMP, UDP) */
        ip->checksum = 0;                /* Zero for checksum calculation */
        /* Source IP: use interface-specific IP if available, else default */
        ip4_addr_t src_ip = ip_local_addr;
        ip4_addr_t interface_ip;
        if (arp_get_interface_ip(dev, &interface_ip) == 0)
        {
            src_ip = interface_ip;
        }
        ip->src_addr = src_ip;
        ip->dest_addr = dest_ip;         /* Destination IP */

        /* Calculate checksum */
        ip->checksum = ip_checksum(ip, ip_header_len);

        /* Copy payload */
        memcpy(ip_packet + ip_header_len, payload, len);
        
        /* Resolve MAC and send */
        uint8_t dest_mac[6];
        if (arp_resolve(dev, next_hop_ip, dest_mac) != 0)
        {
            LOG_ERROR_FMT("IP", "Failed to resolve MAC for " IP4_FMT, IP4_ARGS(ntohl(next_hop_ip)));
            kfree(ip_packet);
            return -1;
        }
        
        LOG_INFO_FMT("IP", "Sending IP packet: dest=" IP4_FMT ", protocol=%d, len=%d",
                     IP4_ARGS(ntohl(dest_ip)), (int)protocol, (int)total_len);
        
        int ret = net_send(dev, ETHERTYPE_IP, dest_mac, ip_packet, total_len);
        kfree(ip_packet);
        
        if (ret == 0)
        {
            LOG_INFO_FMT("IP", "IP packet sent successfully to " IP4_FMT, IP4_ARGS(ntohl(dest_ip)));
        }
        else
        {
            LOG_ERROR_FMT("IP", "Failed to send IP packet to " IP4_FMT, IP4_ARGS(ntohl(dest_ip)));
        }
        
        return ret;
    }
    
    if (!IP_TX_FRAGMENTATION_ENABLED)
    {
        LOG_WARNING("IP", "TX packet exceeds MTU and TX fragmentation is disabled");
        return -EFBIG;
    }

    /* Packet is too large, fragment according to TX policy */
    LOG_INFO_FMT("IP", "Fragmenting packet: total_len=%d, max_payload=%d", 
                 (int)total_len, (int)max_payload);
    
    uint16_t frag_id = ip_frag_id_counter++;
    size_t offset = 0;
    const uint8_t *payload_ptr = (const uint8_t *)payload;
    
    while (offset < len)
    {
        size_t fragment_payload_len = len - offset;
        bool more_fragments = true;
        
        if (fragment_payload_len > max_payload)
        {
            fragment_payload_len = max_payload;
        }
        else
        {
            more_fragments = false; /* This is the last fragment */
        }
        
        /* Send fragment */
        int ret = ip_send_fragment(dev, dest_ip, protocol, 
                                   payload_ptr + offset, fragment_payload_len,
                                   frag_id, offset, more_fragments, next_hop_ip,
				   ttl);
        
        if (ret != 0)
        {
            LOG_ERROR_FMT("IP", "Failed to send fragment at offset %d", (int)offset);
            return -1;
        }
        
        offset += fragment_payload_len;
    }
    
    LOG_INFO_FMT("IP", "Fragmentation complete: %d fragments sent", 
                 (int)((len + max_payload - 1) / max_payload));
    return 0;

}

/**
 * ip_init - Initialize IP protocol
 * @return: 0 on success, -1 on error
 */
int ip_init(void)
{
    
    /* Set default IP configuration based on network mode.
     * 
     * Auto-detection: If IR0_TAP_NETWORKING is defined at compile time (via
     * make run-tap), use TAP defaults. Otherwise, use QEMU user-mode defaults.
     * 
     * QEMU user mode networking uses 10.0.2.x by default:
     * - Guest IP: 10.0.2.15
     * - Gateway: 10.0.2.2 (router virtual)
     * - DNS: 10.0.2.3
     * 
     * TAP networking typically uses:
     * - Guest IP: 192.168.100.2 (common for development)
     * - Gateway: 192.168.100.1 (host bridge)
     * - Netmask: 255.255.255.0
     */
#ifdef IR0_TAP_NETWORKING
    /* TAP networking mode: Auto-configure for TAP */
    ip_local_addr = ip_make_addr(192, 168, 100, 2);
    ip_netmask = ip_make_addr(255, 255, 255, 0);
    ip_gateway = ip_make_addr(192, 168, 100, 1);
    LOG_INFO("IP", "TAP networking mode: Auto-configured IP 192.168.100.2");
#else
    /* User-mode networking (QEMU default) */
    ip_local_addr = ip_make_addr(10, 0, 2, 15);
    ip_netmask = ip_make_addr(255, 255, 255, 0);
    ip_gateway = ip_make_addr(10, 0, 2, 2);
    LOG_INFO("IP", "User-mode networking: Using QEMU defaults (10.0.2.15)");
#endif
    
    /* Synchronize ARP's IP address */
    arp_set_my_ip(ip_local_addr);
    
    /* Configure interface IP for all registered network devices */
    struct net_device *dev = net_get_devices();
    
    while (dev)
    {
        if (arp_set_interface_ip(dev, ip_local_addr) == 0)
        {
            LOG_INFO_FMT("IP", "Configured interface IP " IP4_FMT " for device %s",
                        IP4_ARGS(ntohl(ip_local_addr)), dev->name);
        }
        dev = dev->next;
    }

    LOG_INFO_FMT("IP", "Initializing IPv4 with address " IP4_FMT, IP4_ARGS(ntohl(ip_local_addr)));

    /* Soft FIB mirrors globals so /proc/net/route is not synthesis-only. */
    (void)ip_routes_seed_from_globals();

    /* Register IP protocol handler */
    memset(&ip_proto, 0, sizeof(ip_proto));
    ip_proto.name = "IP";
    ip_proto.ethertype = ETHERTYPE_IP;
    ip_proto.ipproto = 0;  /* IP itself doesn't have an IP protocol number */
    ip_proto.handler = ip_receive_handler;
    ip_proto.priv = NULL;

    if (net_register_protocol(&ip_proto) != 0)
    {
        LOG_ERROR("IP", "Failed to register IP protocol");
        return -1;
    }

    LOG_INFO("IP", "IPv4 protocol initialized");
    return 0;
}

/**
 * ip_routes_seed_from_globals - Install connected + default into soft FIB.
 *
 * Called from ip_init / DHCP / NET_SET_CONFIG so /proc/net/route and LPM
 * share the same table (ip_route_add updates duplicates in place).
 */
int ip_routes_seed_from_globals(void)
{
	ip4_addr_t connected;
	int ret = 0;

	if (ip_local_addr == 0 || ip_netmask == 0)
		return -EINVAL;

	connected = ip_local_addr & ip_netmask;
	if (ip_route_add(connected, ip_netmask, 0) != 0)
		ret = -1;
	if (ip_gateway != 0 && ip_route_add(0, 0, ip_gateway) != 0)
		ret = -1;
	return ret;
}

/**
 * ip_route_add - Add a route to the routing table
 * @dest_network: Destination network address
 * @netmask: Network mask for the route
 * @gateway: Gateway address (0 for direct route)
 * @return: 0 on success, -1 on error
 */
int ip_route_add(ip4_addr_t dest_network, ip4_addr_t netmask, ip4_addr_t gateway)
{
    if (ip_route_count >= MAX_ROUTES)
    {
        LOG_ERROR("IP", "Routing table full");
        return -1;
    }
    
    /* Check for duplicate route */
    struct ip_route_entry *route = ip_routes;
    while (route)
    {
        if (route->dest_network == dest_network && route->netmask == netmask)
        {
            /* Update existing route */
            route->gateway = gateway;
            LOG_INFO_FMT("IP", "Updated route: " IP4_FMT "/%d -> " IP4_FMT,
                        IP4_ARGS(ntohl(dest_network)),
                        ip_count_bits(ntohl(netmask)),
                        IP4_ARGS(ntohl(gateway)));
            return 0;
        }
        route = route->next;
    }
    
    /* Create new route entry */
    route = kmalloc(sizeof(struct ip_route_entry));
    if (!route)
        return -1;
    
    route->dest_network = dest_network & netmask; /* Ensure network address */
    route->netmask = netmask;
    route->gateway = gateway;
    route->next = ip_routes;
    ip_routes = route;
    ip_route_count++;
    
    if (gateway)
    {
        LOG_INFO_FMT("IP", "Added route: " IP4_FMT "/%d -> gateway " IP4_FMT,
                    IP4_ARGS(ntohl(route->dest_network)),
                    ip_count_bits(ntohl(netmask)),
                    IP4_ARGS(ntohl(gateway)));
    }
    else
    {
        LOG_INFO_FMT("IP", "Added route: " IP4_FMT "/%d -> direct " IP4_FMT,
                    IP4_ARGS(ntohl(route->dest_network)),
                    ip_count_bits(ntohl(netmask)),
                    IP4_ARGS(ntohl(dest_network)));
    }
    return 0;
}

/**
 * ip_route_del - Remove a route from the routing table
 * @dest_network: Destination network address
 * @netmask: Network mask
 * @return: 0 on success, -1 if route not found
 */
int ip_route_del(ip4_addr_t dest_network, ip4_addr_t netmask)
{
    struct ip_route_entry *route = ip_routes;
    struct ip_route_entry *prev = NULL;
    
    while (route)
    {
        if (route->dest_network == (dest_network & netmask) && route->netmask == netmask)
        {
            if (prev)
                prev->next = route->next;
            else
                ip_routes = route->next;
            
            LOG_INFO_FMT("IP", "Deleted route: " IP4_FMT "/%d",
                        IP4_ARGS(ntohl(route->dest_network)),
                        ip_count_bits(ntohl(route->netmask)));
            kfree(route);
            ip_route_count--;
            return 0;
        }
        prev = route;
        route = route->next;
    }
    
    return -1;
}

/**
 * ip_route_walk - Invoke @cb for each entry in the software route list.
 * Does not synthesize the connected/default routes from globals (caller does).
 */
int ip_route_walk(int (*cb)(ip4_addr_t dest, ip4_addr_t mask, ip4_addr_t gw,
			     void *ctx),
		  void *ctx)
{
	struct ip_route_entry *route;

	if (!cb)
		return -EINVAL;

	for (route = ip_routes; route; route = route->next)
	{
		if (cb(route->dest_network, route->netmask, route->gateway, ctx) != 0)
			return -1;
	}
	return 0;
}

