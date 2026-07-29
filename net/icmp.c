/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: icmp.c
 * Description: ICMP (Internet Control Message Protocol) implementation
 */

#include "icmp.h"
#include "ip.h"
#include <ir0/sock_icmp.h>
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <string.h>

/* Macros for IP address formatting (matching ARP/IP) */
#define IP4_FMT "%d.%d.%d.%d"
#define IP4_ARGS(ip) \
    (int)((ip >> 24) & 0xFF), \
    (int)((ip >> 16) & 0xFF), \
    (int)((ip >> 8) & 0xFF), \
    (int)(ip & 0xFF)

/* ICMP Protocol registration */
static struct net_protocol icmp_proto;

/* ICMP Echo tracking: track pending ping requests to match replies */
struct icmp_pending_echo {
    uint16_t id;
    uint16_t seq;
    ip4_addr_t dest_ip;
    uint64_t timestamp;  /* When request was sent */
    bool resolved;       /* True when reply received */
    uint64_t rtt;        /* Round-trip time in milliseconds */
    uint8_t ttl;         /* TTL from reply */
    size_t payload_bytes; /* Payload size in reply */
    ip4_addr_t reply_ip; /* IP address that replied */
    struct icmp_pending_echo *next;
};

static struct icmp_pending_echo *pending_echos = NULL;
static uint16_t icmp_echo_seq_counter = 1;

/* Timeout for pending echo requests (10 seconds) */
#define ICMP_ECHO_TIMEOUT_MS 10000

static void icmp_pending_remove_locked(struct icmp_pending_echo *prev, struct icmp_pending_echo *entry)
{
    if (!entry)
        return;
    if (prev)
        prev->next = entry->next;
    else
        pending_echos = entry->next;
    kfree(entry);
}

static void icmp_pending_cleanup_expired_locked(uint64_t now)
{
    struct icmp_pending_echo *echo = pending_echos;
    struct icmp_pending_echo *prev = NULL;

    while (echo)
    {
        if (now - echo->timestamp > ICMP_ECHO_TIMEOUT_MS)
        {
            struct icmp_pending_echo *next = echo->next;
            icmp_pending_remove_locked(prev, echo);
            echo = next;
            continue;
        }
        prev = echo;
        echo = echo->next;
    }
}

static void icmp_pending_append_locked(struct icmp_pending_echo *entry)
{
    struct icmp_pending_echo *tail;

    if (!entry)
        return;
    entry->next = NULL;
    if (!pending_echos)
    {
        pending_echos = entry;
        return;
    }
    tail = pending_echos;
    while (tail->next)
        tail = tail->next;
    tail->next = entry;
}

static inline uint64_t icmp_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void icmp_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

/**
 * icmp_checksum - Calculate ICMP packet checksum (RFC 792)
 *
 * ICMP uses the same Internet checksum algorithm as IP (RFC 1071). The checksum
 * covers the entire ICMP message (header + payload). This provides error detection
 * for ICMP packets, which are used for network diagnostics and error reporting.
 *
 * The algorithm is identical to ip_checksum(): sum all 16-bit words, fold to
 * 16 bits, take one's complement. See ip_checksum() for detailed explanation.
 *
 * @data: Pointer to ICMP packet data (header + payload)
 * @len: Length of ICMP packet in bytes
 * @return: 16-bit checksum in network byte order
 */
uint16_t icmp_checksum(const void *data, size_t len)
{
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;
    size_t i;

    /* Sum all 16-bit words */
    for (i = 0; i < len / 2; i++)
    {
        sum += ntohs(words[i]);
    }

    /* Handle odd byte */
    if (len & 1)
    {
        sum += ((uint8_t *)data)[len - 1] << 8;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Return one's complement */
    return htons(~sum);
}

/**
 * icmp_receive_handler - Process incoming ICMP packets
 *
 * ICMP (Internet Control Message Protocol) is used for error reporting and
 * network diagnostics. Common ICMP message types include:
 *
 *   - Echo Request/Reply (ping): Network connectivity testing
 *   - Destination Unreachable: Routing errors, host unreachable
 *   - Time Exceeded: TTL expired (traceroute uses this)
 *
 * This function validates incoming ICMP packets and dispatches them to
 * appropriate handlers based on message type. Currently, we handle Echo
 * Request (respond with Echo Reply) and log other message types.
 *
 * @dev: Network device that received the packet
 * @data: Pointer to ICMP packet (after IP header)
 * @len: Length of ICMP packet
 * @priv: Private data (unused, provided for protocol handler signature compatibility)
 */
void icmp_receive_handler(struct net_device *dev, const void *data, 
                           size_t len, void *priv)
{
    const struct ip_rx_context *rx_ctx = (const struct ip_rx_context *)priv;
    ip4_addr_t src_ip = rx_ctx ? rx_ctx->src_addr : 0;
    uint8_t rx_ttl = rx_ctx ? rx_ctx->ttl : 0;

    if (len < sizeof(struct icmp_header))
    {
        LOG_WARNING("ICMP", "Packet too short");
        return;
    }

    const struct icmp_header *icmp = (const struct icmp_header *)data;

    /* Verify ICMP checksum to detect corruption. We temporarily zero the
     * checksum field, recalculate, then restore. If the received checksum
     * matches our calculation, the packet is valid. If not, we drop it.
     */
    uint16_t received_checksum = icmp->checksum;
    uint8_t *icmp_scratch = kmalloc(len);
    uint16_t calculated_checksum;

    if (!icmp_scratch)
    {
        LOG_WARNING("ICMP", "No memory to verify checksum");
        return;
    }
    memcpy(icmp_scratch, data, len);
    ((struct icmp_header *)icmp_scratch)->checksum = 0;
    calculated_checksum = icmp_checksum(icmp_scratch, len);
    kfree(icmp_scratch);

    if (received_checksum != calculated_checksum)
    {
        LOG_WARNING("ICMP", "Checksum mismatch");
        return;
    }

    /* Extract message type and code. The type identifies the ICMP message
     * category (Echo Request, Destination Unreachable, etc.). The code
     * provides additional detail within that category (e.g., different
     * reasons for "destination unreachable").
     */
    uint8_t type = icmp->type;
    uint8_t code = icmp->code;

    LOG_INFO_FMT("ICMP", "Received ICMP packet: type=%d, code=%d", 
                 (int)type, (int)code);

    /*
     * Deliver to SOCK_RAW IPPROTO_ICMP sockets (BusyBox ping). Skip echo
     * requests: the kernel auto-replies and userspace did not send them.
     */
    if (type != ICMP_TYPE_ECHO_REQUEST)
        sock_icmp_rx_deliver((uint32_t)src_ip,
                             (uint32_t)(rx_ctx ? rx_ctx->dest_addr : 0),
                             rx_ttl, data, len);

    switch (type)
    {
        case ICMP_TYPE_ECHO_REQUEST:
        {
            /* Echo Request (ping request): respond with Echo Reply (ping reply).
             * The Echo Request/Reply mechanism is used by the ping utility to
             * test network connectivity. We copy the entire packet (including
             * any payload data), change the type to Echo Reply, recalculate
             * the checksum, and send it back to the source.
             */
            LOG_INFO("ICMP", "Echo Request received, sending Echo Reply");

            /* Allocate buffer for reply. We use the same length as the request
             * to preserve any payload data (ping often includes timestamps or
             * other data in the payload for round-trip time calculation).
             */
            size_t reply_len = len;
            if (reply_len > 1500)
                reply_len = 1500;
            uint8_t *reply = kmalloc(reply_len);
            if (!reply)
            {
                LOG_ERROR("ICMP", "Failed to allocate memory for ICMP reply");
                return;
            }

            /* Copy original packet: header and payload. We'll modify the header
             * fields (type, code, checksum) but preserve the rest, including
             * the echo identifier and sequence number, which the requester uses
             * to match replies to requests.
             */
            memcpy(reply, data, reply_len);

            struct icmp_header *reply_icmp = (struct icmp_header *)reply;

            /* Modify header for Echo Reply. Type changes from ECHO_REQUEST (8)
             * to ECHO_REPLY (0). Code is always 0 for echo messages. We zero
             * the checksum before recalculating (checksum field must be 0 during
             * calculation).
             */
            reply_icmp->type = ICMP_TYPE_ECHO_REPLY;
            reply_icmp->code = 0;
            reply_icmp->checksum = 0;

            /* Recalculate checksum. Since we changed the type field, the checksum
             * must be recalculated. The payload and other fields remain unchanged.
             */
            reply_icmp->checksum = icmp_checksum(reply, reply_len);

            if (src_ip == 0)
            {
                LOG_WARNING("ICMP", "Cannot send Echo Reply: source IP not available");
                kfree(reply);
                return;
            }

            /* Send reply via IP layer */
            int ret = ip_send(dev, src_ip, IPPROTO_ICMP, reply, reply_len);
            if (ret != 0)
            {
                LOG_ERROR("ICMP", "Failed to send ICMP Echo Reply");
            }
            else
            {
                LOG_INFO("ICMP", "ICMP Echo Reply sent");
            }

            kfree(reply);
            break;
        }

        case ICMP_TYPE_ECHO_REPLY:
        {
            /* Echo Reply received: match it to a pending request */
            uint16_t id = ntohs(icmp->un.echo.id);
            uint16_t seq = ntohs(icmp->un.echo.seq);
            
            LOG_INFO_FMT("ICMP", "Echo Reply received: id=%d, seq=%d, src=" IP4_FMT,
                         (int)id, (int)seq, IP4_ARGS(ntohl(src_ip)));
            
            /* Search for matching pending echo request */
            uint64_t irq_flags = icmp_irq_save();
            struct icmp_pending_echo *echo = pending_echos;
            uint64_t now = clock_get_uptime_milliseconds();
            icmp_pending_cleanup_expired_locked(now);
            echo = pending_echos;
            while (echo)
            {
                /* Check if this reply matches the pending echo */
                if (echo->id == id && echo->seq == seq && echo->dest_ip == src_ip)
                {
                    /* Match found! Calculate round-trip time */
                    uint64_t rtt = now - echo->timestamp;
                    
                    /* Get TTL for logging */
                    uint8_t ttl = rx_ttl;
                    size_t payload_bytes = len - sizeof(struct icmp_header);
                    
                    /* Store reply information for dbgshell to access */
                    echo->resolved = true;
                    echo->rtt = rtt;
                    echo->ttl = ttl;
                    echo->payload_bytes = payload_bytes;
                    echo->reply_ip = src_ip;
                    
                    /* Log the ping response with Linux-style format for serial debugging */
                    /* Note: VGA output should be handled by dbgshell/userspace, not by ICMP layer */
                    LOG_INFO_FMT("ICMP", "%d bytes from " IP4_FMT ": icmp_seq=%d ttl=%d time=%d ms",
                                (int)payload_bytes, IP4_ARGS(ntohl(src_ip)), (int)seq, (int)ttl, (int)rtt);
                    
                    /* Keep it pending until userspace consumes via NET_GET_PING_RESULT. */
                    icmp_irq_restore(irq_flags);
                    break;
                }
                
                echo = echo->next;
            }

            if (!echo)
            {
                LOG_INFO_FMT("ICMP", "Echo Reply id=%d, seq=%d not matched to any pending request",
                            (int)id, (int)seq);
            }
            if (echo == NULL)
                icmp_irq_restore(irq_flags);
            break;
        }

        case ICMP_TYPE_DEST_UNREACH:
        {
            /* Destination Unreachable: indicates a routing or delivery problem.
             * Common codes include:
             *   0: Network unreachable (no route to network)
             *   1: Host unreachable (host not on network)
             *   3: Port unreachable (UDP/TCP port not open)
             *
             * This is useful for network diagnostics. A full implementation might
             * trigger retransmission in upper-layer protocols or notify the
             * application that initiated the failed connection.
             */
            LOG_WARNING_FMT("ICMP", "Destination Unreachable: code=%d", (int)code);
            break;
        }

        case ICMP_TYPE_TIME_EXCEEDED:
        {
            /* Time Exceeded: TTL (Time To Live) expired during routing. This
             * happens when a packet's TTL reaches zero before reaching its
             * destination, often due to routing loops or the destination being
             * too many hops away. The traceroute utility uses this message type
             * to discover the path packets take through the network.
             */
            LOG_WARNING_FMT("ICMP", "Time Exceeded: code=%d", (int)code);
            break;
        }

        default:
        {
            /* Unhandled ICMP message type. ICMP has many message types we don't
             * implement (Redirect, Parameter Problem, Timestamp, etc.). For
             * now, we just log them. A full implementation might handle more
             * types for better network diagnostics and error reporting.
             */
            LOG_INFO_FMT("ICMP", "Unhandled ICMP message type: %d", (int)type);
            break;
        }
    }
}

/**
 * icmp_send_echo_request - Send an ICMP Echo Request (ping packet)
 *
 * This function constructs and sends an ICMP Echo Request message, which is
 * the packet type used by the ping utility to test network connectivity.
 * The request includes an identifier and sequence number to help match replies
 * to requests, and optional payload data (often used for timing or identification).
 *
 * The identifier is typically the process ID or a unique value that identifies
 * the ping instance. The sequence number increments for each ping packet sent,
 * allowing multiple ping packets to be distinguished. Both values are echoed
 * back in the Echo Reply, allowing the sender to match replies to requests.
 *
 * @dev: Network device to send on
 * @dest_ip: Destination IP address (in network byte order)
 * @id: Echo identifier (helps match replies to requests, e.g., process ID)
 * @seq: Echo sequence number (increments per packet)
 * @data: Optional payload data (can be NULL if len is 0)
 * @len: Payload data length in bytes (0 if no payload)
 * @return: 0 on success, -1 on error
 */
int icmp_send_echo_request(struct net_device *dev, ip4_addr_t dest_ip, 
                           uint16_t id, uint16_t seq, const void *data, size_t len)
{
    if (!dev)
        return -1;

    /* ICMP Echo Request structure.
     * If no payload provided, add minimum payload to ensure packet meets
     * Ethernet minimum frame size requirement (64 bytes total). This helps
     * avoid packets being dropped by routers/switches that enforce minimum sizes.
     */
    size_t actual_payload_len = len;
    if (len == 0)
    {
        /* Add default 32-byte payload (total packet will be ~64 bytes: 14 Eth + 20 IP + 8 ICMP + 32 payload = 74) */
        actual_payload_len = 32;
    }
    
    size_t icmp_len = sizeof(struct icmp_header) + actual_payload_len;
    uint8_t *icmp_packet = kmalloc(icmp_len);
    if (!icmp_packet)
        return -1;

    struct icmp_header *icmp = (struct icmp_header *)icmp_packet;

    /* Fill ICMP header */
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = htons(id);
    icmp->un.echo.seq = htons(seq);

    /* Copy payload data if provided, otherwise use default payload */
    if (data && len > 0)
    {
        memcpy(icmp_packet + sizeof(struct icmp_header), data, len);
    }
    else if (len == 0)
    {
        /* Add default payload: timestamp data to help with RTT calculation */
        uint32_t timestamp = (uint32_t)clock_get_uptime_milliseconds();
        memcpy(icmp_packet + sizeof(struct icmp_header), &timestamp, sizeof(timestamp));
        /* Pad to 32 bytes with zeros */
        memset(icmp_packet + sizeof(struct icmp_header) + sizeof(timestamp), 0, 
               32 - sizeof(timestamp));
    }

    /* Calculate checksum */
    icmp->checksum = icmp_checksum(icmp_packet, icmp_len);

    LOG_INFO_FMT("ICMP", "Sending Echo Request to " IP4_FMT " (id=%d, seq=%d)",
                 IP4_ARGS(ntohl(dest_ip)),
                 (int)id, (int)seq);

    /* Track this echo request so we can match the reply */
    struct icmp_pending_echo *pending = kmalloc(sizeof(struct icmp_pending_echo));
    if (pending)
    {
        uint64_t irq_flags = icmp_irq_save();
        uint64_t now = clock_get_uptime_milliseconds();
        icmp_pending_cleanup_expired_locked(now);
        pending->id = id;
        pending->seq = seq;
        pending->dest_ip = dest_ip;
        pending->timestamp = now;
        pending->resolved = false;
        pending->rtt = 0;
        pending->ttl = 0;
        pending->payload_bytes = 0;
        pending->reply_ip = 0;
        pending->next = NULL;
        icmp_pending_append_locked(pending);
        icmp_irq_restore(irq_flags);
        
        LOG_INFO_FMT("ICMP", "Tracking echo request: id=%d, seq=%d", (int)id, (int)seq);
    }

    /* Send via IP layer */
    int ret = ip_send(dev, dest_ip, IPPROTO_ICMP, icmp_packet, icmp_len);

    kfree(icmp_packet);
    return ret;
}

/**
 * icmp_get_echo_result - Get result of a pending echo request (for dbgshell)
 * This function allows dbgshell to check if a ping has received a response
 * and get the result data to display it.
 * 
 * @id: ICMP echo ID
 * @seq: ICMP echo sequence number
 * @rtt_out: Output parameter for round-trip time (milliseconds)
 * @ttl_out: Output parameter for TTL
 * @payload_bytes_out: Output parameter for payload size
 * @reply_ip_out: Output parameter for reply IP address
 * @return: true if reply received, false if still pending or not found
 */
bool icmp_get_echo_result(uint16_t id, uint16_t seq, uint64_t *rtt_out, 
                          uint8_t *ttl_out, size_t *payload_bytes_out, 
                          ip4_addr_t *reply_ip_out)
{
    uint64_t irq_flags;
    uint64_t now;

    if (!rtt_out || !ttl_out || !payload_bytes_out || !reply_ip_out)
        return false;

    irq_flags = icmp_irq_save();
    now = clock_get_uptime_milliseconds();
    icmp_pending_cleanup_expired_locked(now);
    struct icmp_pending_echo *echo = pending_echos;
    struct icmp_pending_echo *prev = NULL;
    
    while (echo)
    {
        if (echo->id == id && echo->seq == seq)
        {
            if (echo->resolved)
            {
                *rtt_out = echo->rtt;
                *ttl_out = echo->ttl;
                *payload_bytes_out = echo->payload_bytes;
                *reply_ip_out = echo->reply_ip;
                icmp_pending_remove_locked(prev, echo);
                icmp_irq_restore(irq_flags);
                return true;
            }
            else
            {
                /* Still pending */
                icmp_irq_restore(irq_flags);
                return false;
            }
        }
        prev = echo;
        echo = echo->next;
    }
    icmp_irq_restore(irq_flags);
    /* Not found */
    return false;
}

bool icmp_get_next_echo_result(uint16_t id, uint16_t *seq_out, uint64_t *rtt_out,
                               uint8_t *ttl_out, size_t *payload_bytes_out,
                               ip4_addr_t *reply_ip_out)
{
    uint64_t irq_flags;
    uint64_t now;
    struct icmp_pending_echo *echo;
    struct icmp_pending_echo *prev = NULL;

    if (!seq_out || !rtt_out || !ttl_out || !payload_bytes_out || !reply_ip_out)
        return false;

    irq_flags = icmp_irq_save();
    now = clock_get_uptime_milliseconds();
    icmp_pending_cleanup_expired_locked(now);
    echo = pending_echos;

    while (echo)
    {
        if (echo->id == id && echo->resolved)
        {
            *seq_out = echo->seq;
            *rtt_out = echo->rtt;
            *ttl_out = echo->ttl;
            *payload_bytes_out = echo->payload_bytes;
            *reply_ip_out = echo->reply_ip;
            icmp_pending_remove_locked(prev, echo);
            icmp_irq_restore(irq_flags);
            return true;
        }
        prev = echo;
        echo = echo->next;
    }

    icmp_irq_restore(irq_flags);
    return false;
}

bool icmp_has_ready_echo_result(uint16_t id)
{
    uint64_t irq_flags;
    uint64_t now;
    struct icmp_pending_echo *echo;

    irq_flags = icmp_irq_save();
    now = clock_get_uptime_milliseconds();
    icmp_pending_cleanup_expired_locked(now);
    echo = pending_echos;
    while (echo)
    {
        if (echo->id == id && echo->resolved)
        {
            icmp_irq_restore(irq_flags);
            return true;
        }
        echo = echo->next;
    }
    icmp_irq_restore(irq_flags);
    return false;
}

uint16_t icmp_allocate_echo_seq(void)
{
    uint64_t irq_flags;
    uint16_t seq;

    irq_flags = icmp_irq_save();
    seq = icmp_echo_seq_counter++;
    if (icmp_echo_seq_counter == 0)
        icmp_echo_seq_counter = 1;
    icmp_irq_restore(irq_flags);
    return seq;
}

/**
 * icmp_init - Initialize ICMP protocol
 * @return: 0 on success, -1 on error
 */
int icmp_init(void)
{
    LOG_INFO("ICMP", "Initializing ICMP protocol");

    /* Register ICMP protocol handler */
    memset(&icmp_proto, 0, sizeof(icmp_proto));
    icmp_proto.name = "ICMP";
    icmp_proto.ethertype = 0;  /* ICMP doesn't have an EtherType */
    icmp_proto.ipproto = IPPROTO_ICMP;
    icmp_proto.handler = icmp_receive_handler;
    icmp_proto.priv = NULL;

    if (net_register_protocol(&icmp_proto) != 0)
    {
        LOG_ERROR("ICMP", "Failed to register ICMP protocol");
        return -1;
    }

    LOG_INFO("ICMP", "ICMP protocol initialized");
    return 0;
}

