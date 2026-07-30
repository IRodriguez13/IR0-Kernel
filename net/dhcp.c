/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dhcp.c
 * Description: DHCPv4 client for runtime IPv4 configuration
 */

#include "dhcp.h"
#include "arp.h"
#include "dns.h"
#include "ip.h"
#include "udp.h"
#include <ir0/logging.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <string.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY 2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET 6
#define DHCP_FLAG_BROADCAST 0x8000

#define DHCP_MAGIC_COOKIE 0x63825363U

#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS_SERVER 6
#define DHCP_OPTION_REQUESTED_IP 50
#define DHCP_OPTION_LEASE_TIME 51
#define DHCP_OPTION_MSG_TYPE 53
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_PARAMETER_LIST 55
#define DHCP_OPTION_CLIENT_ID 61
#define DHCP_OPTION_END 255

#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER 2
#define DHCP_MSG_REQUEST 3
#define DHCP_MSG_ACK 5
#define DHCP_MSG_NAK 6

#define DHCP_POLL_INTERVAL_MS 10U
#define DHCP_DISCOVER_TIMEOUT_MS 3000U
#define DHCP_REQUEST_TIMEOUT_MS 3000U
#define DHCP_MAX_HANDSHAKE_TRIES 3

/* RFC 2131 BOOTP fixed header (without cookie/options). */
struct dhcp_bootp_header
{
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
} __attribute__((packed));

struct dhcp_offer_data
{
    ip4_addr_t yiaddr;
    ip4_addr_t subnet_mask;
    ip4_addr_t router;
    ip4_addr_t dns_server;
    ip4_addr_t server_id;
    uint32_t lease_time_s;
};

struct dhcp_runtime_state
{
    uint32_t xid;
    int offer_ready;
    int ack_ready;
    int nak_ready;
    struct dhcp_offer_data offer;
};

static struct dhcp_runtime_state dhcp_state;
static int dhcp_handler_registered;

static inline uint64_t dhcp_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void dhcp_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

static void dhcp_state_reset_for_xid(uint32_t xid)
{
    uint64_t flags = dhcp_irq_save();
    memset(&dhcp_state, 0, sizeof(dhcp_state));
    dhcp_state.xid = xid;
    dhcp_irq_restore(flags);
}

static int dhcp_append_option_u8(uint8_t **opt, uint8_t *end, uint8_t code, uint8_t value)
{
    if (!opt || !*opt || !end || *opt + 3 > end)
    {
        return -1;
    }

    *(*opt)++ = code;
    *(*opt)++ = 1;
    *(*opt)++ = value;
    return 0;
}

static int dhcp_append_option_ip(uint8_t **opt, uint8_t *end, uint8_t code, ip4_addr_t ip)
{
    if (!opt || !*opt || !end || *opt + 6 > end)
    {
        return -1;
    }

    *(*opt)++ = code;
    *(*opt)++ = 4;
    memcpy(*opt, &ip, 4);
    *opt += 4;
    return 0;
}

static size_t dhcp_build_payload(uint8_t *buffer, size_t buffer_len, const struct net_device *dev,
                                 uint32_t xid, uint8_t msg_type, ip4_addr_t requested_ip,
                                 ip4_addr_t server_id)
{
    if (!buffer || !dev || buffer_len < sizeof(struct dhcp_bootp_header) + 4 + 16)
    {
        return 0;
    }

    memset(buffer, 0, buffer_len);

    struct dhcp_bootp_header *hdr = (struct dhcp_bootp_header *)buffer;
    hdr->op = DHCP_OP_BOOTREQUEST;
    hdr->htype = DHCP_HTYPE_ETHERNET;
    hdr->hlen = DHCP_HLEN_ETHERNET;
    hdr->hops = 0;
    hdr->xid = htonl(xid);
    hdr->secs = 0;
    hdr->flags = htons(DHCP_FLAG_BROADCAST);
    hdr->ciaddr = 0;
    hdr->yiaddr = 0;
    hdr->siaddr = 0;
    hdr->giaddr = 0;
    memcpy(hdr->chaddr, dev->mac, 6);

    uint8_t *opt = buffer + sizeof(struct dhcp_bootp_header);
    uint8_t *opt_end = buffer + buffer_len;
    uint32_t cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(opt, &cookie, sizeof(cookie));
    opt += sizeof(cookie);

    if (dhcp_append_option_u8(&opt, opt_end, DHCP_OPTION_MSG_TYPE, msg_type) != 0)
    {
        return 0;
    }

    /*
     * RFC 2132 client identifier: type(1=Ethernet) + hardware address.
     * This helps servers keep lease identity stable across retries.
     */
    if (opt + 2 + 1 + 6 > opt_end)
    {
        return 0;
    }
    *opt++ = DHCP_OPTION_CLIENT_ID;
    *opt++ = 7;
    *opt++ = DHCP_HTYPE_ETHERNET;
    memcpy(opt, dev->mac, 6);
    opt += 6;

    if (msg_type == DHCP_MSG_REQUEST)
    {
        if (dhcp_append_option_ip(&opt, opt_end, DHCP_OPTION_REQUESTED_IP, requested_ip) != 0)
        {
            return 0;
        }
        if (dhcp_append_option_ip(&opt, opt_end, DHCP_OPTION_SERVER_ID, server_id) != 0)
        {
            return 0;
        }
    }

    if (opt + 2 + 4 > opt_end)
    {
        return 0;
    }
    *opt++ = DHCP_OPTION_PARAMETER_LIST;
    *opt++ = 4;
    *opt++ = DHCP_OPTION_SUBNET_MASK;
    *opt++ = DHCP_OPTION_ROUTER;
    *opt++ = DHCP_OPTION_DNS_SERVER;
    *opt++ = DHCP_OPTION_LEASE_TIME;

    if (opt + 1 > opt_end)
    {
        return 0;
    }
    *opt++ = DHCP_OPTION_END;

    return (size_t)(opt - buffer);
}

static int dhcp_send_message(struct net_device *dev, uint32_t xid, uint8_t msg_type,
                             ip4_addr_t requested_ip, ip4_addr_t server_id)
{
    uint8_t dhcp_payload[320];
    uint8_t ip_packet[sizeof(struct ip_header) + sizeof(struct udp_header) + sizeof(dhcp_payload)];
    uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    size_t payload_len = dhcp_build_payload(dhcp_payload, sizeof(dhcp_payload), dev, xid, msg_type,
                                            requested_ip, server_id);
    if (payload_len == 0)
    {
        LOG_ERROR("DHCP", "Failed to build DHCP payload");
        return -1;
    }

    struct ip_header *ip = (struct ip_header *)ip_packet;
    struct udp_header *udp = (struct udp_header *)(ip_packet + sizeof(struct ip_header));
    uint8_t *udp_payload = ip_packet + sizeof(struct ip_header) + sizeof(struct udp_header);
    size_t udp_len = sizeof(struct udp_header) + payload_len;
    size_t ip_len = sizeof(struct ip_header) + udp_len;

    memcpy(udp_payload, dhcp_payload, payload_len);

    udp->src_port = htons(DHCP_CLIENT_PORT);
    udp->dest_port = htons(DHCP_SERVER_PORT);
    udp->length = htons((uint16_t)udp_len);
    udp->checksum = 0;
    udp->checksum = udp_checksum(udp, udp_len, 0, htonl(0xFFFFFFFFU));

    ip->version_ihl = (4 << 4) | 5;
    ip->tos = 0;
    ip->total_len = htons((uint16_t)ip_len);
    ip->id = htons((uint16_t)(xid & 0xFFFFU));
    ip->flags_frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPPROTO_UDP;
    ip->checksum = 0;
    ip->src_addr = 0;
    ip->dest_addr = htonl(0xFFFFFFFFU);
    ip->checksum = ip_checksum(ip, sizeof(struct ip_header));

    if (net_send(dev, ETHERTYPE_IP, broadcast_mac, ip_packet, ip_len) != 0)
    {
        LOG_WARNING_FMT("DHCP", "Failed to send DHCP message type=%d", (int)msg_type);
        return -1;
    }

    LOG_INFO_FMT("DHCP", "Sent DHCP message type=%d (xid=0x%x)", (int)msg_type, (unsigned int)xid);
    return 0;
}

static int dhcp_parse_options(const uint8_t *opt, size_t opt_len, uint8_t *msg_type,
                              struct dhcp_offer_data *offer)
{
    size_t pos = 0;

    if (!opt || !msg_type || !offer)
    {
        return -1;
    }

    *msg_type = 0;

    while (pos < opt_len)
    {
        uint8_t code = opt[pos++];
        if (code == DHCP_OPTION_END)
        {
            break;
        }
        if (code == 0)
        {
            continue;
        }
        if (pos >= opt_len)
        {
            return -1;
        }

        uint8_t len = opt[pos++];
        if (pos + len > opt_len)
        {
            return -1;
        }

        switch (code)
        {
            case DHCP_OPTION_MSG_TYPE:
                if (len == 1)
                {
                    *msg_type = opt[pos];
                }
                break;
            case DHCP_OPTION_SUBNET_MASK:
                if (len >= 4)
                {
                    memcpy(&offer->subnet_mask, opt + pos, 4);
                }
                break;
            case DHCP_OPTION_ROUTER:
                if (len >= 4)
                {
                    memcpy(&offer->router, opt + pos, 4);
                }
                break;
            case DHCP_OPTION_DNS_SERVER:
                if (len >= 4)
                {
                    memcpy(&offer->dns_server, opt + pos, 4);
                }
                break;
            case DHCP_OPTION_SERVER_ID:
                if (len >= 4)
                {
                    memcpy(&offer->server_id, opt + pos, 4);
                }
                break;
            case DHCP_OPTION_LEASE_TIME:
                if (len >= 4)
                {
                    uint32_t lease;
                    memcpy(&lease, opt + pos, 4);
                    offer->lease_time_s = ntohl(lease);
                }
                break;
            default:
                break;
        }

        pos += len;
    }

    return 0;
}

static void dhcp_udp_handler(struct net_device *dev, ip4_addr_t src_ip, uint16_t src_port,
                             const void *data, size_t len, void *priv)
{
    (void)dev;
    (void)src_ip;
    (void)priv;

    if (!data || len < sizeof(struct dhcp_bootp_header) + 4 || src_port != DHCP_SERVER_PORT)
    {
        return;
    }

    const struct dhcp_bootp_header *hdr = (const struct dhcp_bootp_header *)data;
    if (hdr->op != DHCP_OP_BOOTREPLY)
    {
        return;
    }

    uint32_t xid = ntohl(hdr->xid);
    struct dhcp_offer_data parsed = {0};
    uint8_t msg_type = 0;

    uint32_t cookie;
    memcpy(&cookie, (const uint8_t *)data + sizeof(struct dhcp_bootp_header), 4);
    if (ntohl(cookie) != DHCP_MAGIC_COOKIE)
    {
        return;
    }

    parsed.yiaddr = hdr->yiaddr;
    if (hdr->siaddr != 0)
    {
        parsed.server_id = hdr->siaddr;
    }

    if (dhcp_parse_options((const uint8_t *)data + sizeof(struct dhcp_bootp_header) + 4,
                           len - sizeof(struct dhcp_bootp_header) - 4,
                           &msg_type, &parsed) != 0)
    {
        return;
    }

    uint64_t flags = dhcp_irq_save();
    if (xid != dhcp_state.xid)
    {
        dhcp_irq_restore(flags);
        return;
    }

    if (msg_type == DHCP_MSG_OFFER)
    {
        dhcp_state.offer = parsed;
        dhcp_state.offer_ready = 1;
    }
    else if (msg_type == DHCP_MSG_ACK)
    {
        if (parsed.yiaddr == 0)
        {
            parsed.yiaddr = dhcp_state.offer.yiaddr;
        }
        if (parsed.subnet_mask == 0)
        {
            parsed.subnet_mask = dhcp_state.offer.subnet_mask;
        }
        if (parsed.router == 0)
        {
            parsed.router = dhcp_state.offer.router;
        }
        if (parsed.dns_server == 0)
        {
            parsed.dns_server = dhcp_state.offer.dns_server;
        }
        if (parsed.server_id == 0)
        {
            parsed.server_id = dhcp_state.offer.server_id;
        }
        if (parsed.lease_time_s == 0)
        {
            parsed.lease_time_s = dhcp_state.offer.lease_time_s;
        }
        dhcp_state.offer = parsed;
        dhcp_state.ack_ready = 1;
    }
    else if (msg_type == DHCP_MSG_NAK)
    {
        dhcp_state.nak_ready = 1;
    }
    dhcp_irq_restore(flags);
}

static int dhcp_wait_offer(uint32_t timeout_ms)
{
    uint64_t start = clock_get_uptime_milliseconds();
    uint64_t last_poll = start;

    while (clock_get_uptime_milliseconds() - start < timeout_ms)
    {
        uint64_t flags = dhcp_irq_save();
        int ready = dhcp_state.offer_ready;
        dhcp_irq_restore(flags);
        if (ready)
        {
            return 0;
        }

        uint64_t now = clock_get_uptime_milliseconds();
        if (now - last_poll >= DHCP_POLL_INTERVAL_MS)
        {
            net_poll();
            last_poll = now;
        }

        for (volatile int spin = 0; spin < 4000; spin++)
            cpu_relax();
    }

    return -1;
}

static int dhcp_wait_ack(uint32_t timeout_ms)
{
    uint64_t start = clock_get_uptime_milliseconds();
    uint64_t last_poll = start;

    while (clock_get_uptime_milliseconds() - start < timeout_ms)
    {
        uint64_t flags = dhcp_irq_save();
        int ack = dhcp_state.ack_ready;
        int nak = dhcp_state.nak_ready;
        dhcp_irq_restore(flags);
        if (nak)
        {
            return -1;
        }
        if (ack)
        {
            return 0;
        }

        uint64_t now = clock_get_uptime_milliseconds();
        if (now - last_poll >= DHCP_POLL_INTERVAL_MS)
        {
            net_poll();
            last_poll = now;
        }

        for (volatile int spin = 0; spin < 4000; spin++)
            cpu_relax();
    }

    return -1;
}

static void dhcp_apply_config(struct dhcp_offer_data *offer)
{
    if (!offer || offer->yiaddr == 0)
    {
        return;
    }

    ip_local_addr = offer->yiaddr;
    if (offer->subnet_mask != 0)
    {
        ip_netmask = offer->subnet_mask;
    }
    if (offer->router != 0)
    {
        ip_gateway = offer->router;
    }

    arp_set_my_ip(ip_local_addr);

    {
        struct net_device *dev = net_get_devices();

        while (dev)
        {
            arp_set_interface_ip(dev, ip_local_addr);
            dev = dev->next;
        }
    }

    (void)ip_routes_seed_from_globals();

    if (offer->dns_server != 0)
    {
        dns_set_default_server(offer->dns_server);
    }

    LOG_INFO_FMT("DHCP", "Lease applied: ip=" IP4_FMT ", mask=" IP4_FMT ", gw=" IP4_FMT ", dns=" IP4_FMT ", lease_s=%d",
                 IP4_ARGS(ntohl(ip_local_addr)),
                 IP4_ARGS(ntohl(ip_netmask)),
                 IP4_ARGS(ntohl(ip_gateway)),
                 IP4_ARGS(ntohl(offer->dns_server)),
                 (int)offer->lease_time_s);
}

int dhcp_init(void)
{
    struct net_device *dev = net_get_devices();
    if (!dev)
    {
        LOG_WARNING("DHCP", "No network device available, skipping DHCP");
        return 0;
    }

    if (!dhcp_handler_registered)
    {
        udp_register_handler(DHCP_CLIENT_PORT, dhcp_udp_handler, NULL);
        dhcp_handler_registered = 1;
    }

    /*
     * Keep boot deterministic: DHCP is best effort, with bounded retries.
     * If it fails, the stack continues using preconfigured static addresses.
     */
    for (int attempt = 0; attempt < DHCP_MAX_HANDSHAKE_TRIES; attempt++)
    {
        uint32_t xid = (uint32_t)clock_get_tick_count() ^ (uint32_t)((attempt + 1) * 0x12345u);
        dhcp_state_reset_for_xid(xid);

        if (dhcp_send_message(dev, xid, DHCP_MSG_DISCOVER, 0, 0) != 0)
        {
            continue;
        }

        if (dhcp_wait_offer(DHCP_DISCOVER_TIMEOUT_MS) != 0)
        {
            LOG_WARNING_FMT("DHCP", "No OFFER received (attempt %d/%d)", attempt + 1, DHCP_MAX_HANDSHAKE_TRIES);
            continue;
        }

        struct dhcp_offer_data offer;
        memset(&offer, 0, sizeof(offer));
        {
            uint64_t flags = dhcp_irq_save();
            offer = dhcp_state.offer;
            dhcp_irq_restore(flags);
        }

        if (offer.yiaddr == 0 || offer.server_id == 0)
        {
            LOG_WARNING("DHCP", "Invalid OFFER received, retrying");
            continue;
        }

        if (dhcp_send_message(dev, xid, DHCP_MSG_REQUEST, offer.yiaddr, offer.server_id) != 0)
        {
            continue;
        }

        if (dhcp_wait_ack(DHCP_REQUEST_TIMEOUT_MS) != 0)
        {
            LOG_WARNING_FMT("DHCP", "No ACK received (attempt %d/%d)", attempt + 1, DHCP_MAX_HANDSHAKE_TRIES);
            continue;
        }

        {
            uint64_t flags = dhcp_irq_save();
            offer = dhcp_state.offer;
            dhcp_irq_restore(flags);
        }

        dhcp_apply_config(&offer);
        LOG_INFO("DHCP", "DHCP handshake completed successfully");
        return 0;
    }

    LOG_WARNING("DHCP", "DHCP unavailable, keeping static IPv4 configuration");
    return 0;
}

