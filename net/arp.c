/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arp.c
 * Description: ARP (Address Resolution Protocol) implementation
 */

#include "arp.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/klog.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <string.h>

/* Macros for IP address formatting */
#define IP4_FMT "%d.%d.%d.%d"
#define IP4_ARGS(ip) \
    (int)((ip >> 24) & 0xFF), \
    (int)((ip >> 16) & 0xFF), \
    (int)((ip >> 8) & 0xFF), \
    (int)(ip & 0xFF)

/* ARP Cache timeout: entries expire after 5 minutes. This matches typical
 * Linux behavior - ARP cache entries are considered stale after a few minutes
 * to handle cases where hosts change MAC addresses or move to different
 * networks. Expired entries are automatically removed during lookup.
 */
#define ARP_CACHE_TIMEOUT_MS (5 * 60 * 1000)  /* 5 minutes */

/* ARP Resolution parameters: when we need to resolve an IP address and it's
 * not in the cache, we send an ARP request and wait for a reply. We retry
 * up to 3 times with a 2 second timeout per attempt, giving us up to 6 seconds
 * total wait time. This is reasonable for local network resolution.
 */
#define ARP_RESOLVE_TIMEOUT_MS 2000  /* 2 seconds per attempt */
#define ARP_RESOLVE_RETRIES 3        /* 3 attempts before giving up */

/* Maximum ARP cache entries; oldest (tail) is evicted when full */
#define MAX_ARP_ENTRIES 64

/* ARP Cache: a simple linked list of IP-to-MAC mappings. This is a basic
 * implementation - production systems might use a hash table for O(1) lookups,
 * but for small networks (typical home/office), linear search is acceptable.
 * The cache is updated automatically when ARP packets arrive (requests or replies).
 */
static struct arp_cache_entry *arp_cache = NULL;

/* Our IP addresses: track IP per network interface.
 * We maintain a linked list mapping net_device -> IP address.
 * This allows multiple NICs with different IP addresses.
 */
struct arp_interface_ip {
    struct net_device *dev;
    ip4_addr_t ip;
    struct arp_interface_ip *next;
};

static struct arp_interface_ip *interface_ips = NULL;
static ip4_addr_t my_ip = 0;  /* Default IP (backward compatibility) */

/* ARP Protocol registration */
static struct net_protocol arp_proto;

/* Broadcast MAC address */
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static inline uint64_t arp_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void arp_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

static int arp_lookup_mac(ip4_addr_t ip, mac_addr_t mac_out)
{
    struct arp_cache_entry *entry;
    struct arp_cache_entry *prev = NULL;
    uint64_t now = clock_get_uptime_milliseconds();
    uint64_t flags;

    if (!mac_out)
        return -1;

    flags = arp_irq_save();
    entry = arp_cache;
    while (entry)
    {
        if (now - entry->timestamp > ARP_CACHE_TIMEOUT_MS)
        {
            struct arp_cache_entry *next = entry->next;
            if (prev)
                prev->next = next;
            else
                arp_cache = next;
            kfree(entry);
            entry = next;
            continue;
        }
        if (entry->ip == ip)
        {
            memcpy(mac_out, entry->mac, 6);
            arp_irq_restore(flags);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }
    arp_irq_restore(flags);
    return -1;
}

/**
 * arp_receive_handler - Process incoming ARP packets (requests and replies)
 *
 * This function is called by the networking layer when an ARP packet arrives.
 * ARP (Address Resolution Protocol) is used to map IP addresses to MAC addresses
 * on a local network. The protocol has two message types:
 *
 *   - ARP Request: "Who has IP X? Tell me." (broadcast to all hosts)
 *   - ARP Reply: "I have IP X, my MAC is Y." (unicast to requester)
 *
 * When we receive an ARP request for our IP, we send a reply. For any ARP packet
 * (request or reply), we update our ARP cache with the sender's IP/MAC mapping.
 * This opportunistic caching helps populate the cache without explicit requests.
 *
 * @dev: Network device that received the packet
 * @data: Pointer to ARP packet (after Ethernet header)
 * @len: Length of ARP packet
 * @priv: Private data (unused, provided for protocol handler signature compatibility)
 */
static void arp_receive_handler(struct net_device *dev, const void *data, 
                                 size_t len, void *priv)
{
    (void)priv;
    
    if (len < sizeof(struct arp_header))
    {
        LOG_WARNING("ARP", "Packet too short");
        return;
    }
    
    const struct arp_header *arp = (const struct arp_header *)data;
    
    /* Validate ARP packet format. ARP was designed to work with different
     * hardware and protocol types (not just Ethernet/IP), so we must verify
     * we're dealing with Ethernet hardware and IPv4 protocol. The length fields
     * must match: 6 bytes for MAC addresses, 4 bytes for IPv4 addresses.
     */
    if (ntohs(arp->hw_type) != ARP_HW_TYPE_ETHERNET ||
        ntohs(arp->proto_type) != ARP_PROTO_TYPE_IPV4 ||
        arp->hw_len != 6 ||
        arp->proto_len != 4)
    {
        LOG_WARNING("ARP", "Invalid ARP packet format");
        return;
    }
    
    uint16_t opcode = ntohs(arp->opcode);
    
    /* Keep IPs in network byte order for comparisons and cache operations.
     * The ARP header stores IPs in network byte order (big-endian), and we
     * maintain them that way in the cache to avoid conversion overhead.
     */
    ip4_addr_t sender_ip = arp->sender_ip;  /* Already in network byte order */
    ip4_addr_t target_ip = arp->target_ip;  /* Already in network byte order */
    
    LOG_INFO_FMT("ARP", "Received ARP packet: opcode=%d, sender_ip=" IP4_FMT ", target_ip=" IP4_FMT,
                 (int)opcode, IP4_ARGS(ntohl(sender_ip)), IP4_ARGS(ntohl(target_ip)));
    LOG_INFO_FMT("ARP", "Sender MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 arp->sender_mac[0], arp->sender_mac[1], arp->sender_mac[2],
                 arp->sender_mac[3], arp->sender_mac[4], arp->sender_mac[5]);
    LOG_INFO_FMT("ARP", "Target MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 arp->target_mac[0], arp->target_mac[1], arp->target_mac[2],
                 arp->target_mac[3], arp->target_mac[4], arp->target_mac[5]);
    
    if (opcode == ARP_OP_REQUEST)
    {
        LOG_INFO("ARP", "ARP Request received");
        
        /* Opportunistic cache update: even if the request isn't for us, we
         * learn the sender's IP/MAC mapping. This helps populate our cache
         * passively - we don't need to send our own requests for every host
         * we see on the network. This is a standard ARP optimization.
         */
        arp_cache_add(sender_ip, arp->sender_mac);
        
        /* Check if the ARP request is asking for our IP address on this interface.
         * We check both the interface-specific IP and the default IP (for backward
         * compatibility).
         */
        ip4_addr_t interface_ip = 0;
        {
            uint64_t flags = arp_irq_save();
            struct arp_interface_ip *if_ip = interface_ips;
            while (if_ip)
            {
                if (if_ip->dev == dev)
                {
                    interface_ip = if_ip->ip;
                    break;
                }
                if_ip = if_ip->next;
            }
            arp_irq_restore(flags);
        }
        
        /* Log IPs for debugging - CRITICAL for debugging ARP replies */
        LOG_INFO_FMT("ARP", "ARP Request check: target=" IP4_FMT ", my_ip=" IP4_FMT ", interface_ip=" IP4_FMT,
                     IP4_ARGS(ntohl(target_ip)), IP4_ARGS(ntohl(my_ip)), IP4_ARGS(ntohl(interface_ip)));
        LOG_INFO_FMT("ARP", "Device: %s, target matches my_ip=%d, target matches interface_ip=%d",
                     dev->name ? dev->name : "unknown",
                     (target_ip == my_ip) ? 1 : 0,
                     (target_ip == interface_ip) ? 1 : 0);
        
        if (target_ip == interface_ip || target_ip == my_ip)
        {
            LOG_INFO("ARP", "ARP Request is for us, sending reply");
            LOG_INFO_FMT("ARP", "Using sender_ip=" IP4_FMT " in reply, our IP will be " IP4_FMT,
                         IP4_ARGS(ntohl(arp->sender_ip)), 
                         IP4_ARGS(ntohl((interface_ip != 0) ? interface_ip : my_ip)));
            
            /* Send ARP Reply */
            struct arp_header *reply = (struct arp_header *)kmalloc(sizeof(struct arp_header));
            if (!reply)
                return;
            
            memset(reply, 0, sizeof(struct arp_header));
            reply->hw_type = htons(ARP_HW_TYPE_ETHERNET);
            reply->proto_type = htons(ARP_PROTO_TYPE_IPV4);
            reply->hw_len = 6;
            reply->proto_len = 4;
            reply->opcode = htons(ARP_OP_REPLY);
            
            /* Sender (us): use interface-specific IP if available, else default */
            memcpy(reply->sender_mac, dev->mac, 6);
            reply->sender_ip = (interface_ip != 0) ? interface_ip : my_ip;
            
            /* Target (request sender) */
            memcpy(reply->target_mac, arp->sender_mac, 6);
            reply->target_ip = arp->sender_ip;
            
            /* Send reply to the sender */
            if (net_send(dev, ETHERTYPE_ARP, arp->sender_mac, reply, sizeof(struct arp_header)) == 0)
            {
                LOG_INFO("ARP", "ARP Reply sent");
            }
            else
            {
                LOG_ERROR("ARP", "Failed to send ARP Reply");
            }
            
            kfree(reply);
        }
    }
    else if (opcode == ARP_OP_REPLY)
    {
        LOG_INFO("ARP", "ARP Reply received");
        
        /* Update ARP cache with sender information */
        arp_cache_add(sender_ip, arp->sender_mac);
        
        LOG_INFO_FMT("ARP", "Resolved IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                     IP4_ARGS(ntohl(sender_ip)),
                     arp->sender_mac[0], arp->sender_mac[1], arp->sender_mac[2],
                     arp->sender_mac[3], arp->sender_mac[4], arp->sender_mac[5]);
    }
}

/**
 * arp_init - Initialize ARP protocol
 * @return: 0 on success, -1 on error
 */
int arp_init(void)
{
    LOG_INFO("ARP", "Initializing ARP protocol");
    
    memset(&arp_proto, 0, sizeof(arp_proto));
    arp_proto.name = "ARP";
    arp_proto.ethertype = ETHERTYPE_ARP;
    arp_proto.handler = arp_receive_handler;
    arp_proto.priv = NULL;
    
    /* Set default IP (should be configurable per interface) */
    /* QEMU user mode networking default: 10.0.2.15 */
    my_ip = make_ip4_addr(10, 0, 2, 15);
    
    int ret = net_register_protocol(&arp_proto);
    if (ret == 0)
    {
        LOG_INFO("ARP", "ARP protocol registered");
        /* Print initial cache state (should be empty) */
        arp_print_cache();
    }
    else
    {
        LOG_ERROR("ARP", "Failed to register ARP protocol");
    }
    
    return ret;
}

/**
 * arp_lookup - Look up MAC address for a given IP in the ARP cache
 *
 * This function searches the ARP cache for an IP-to-MAC mapping. During the
 * search, it also performs cache maintenance by removing expired entries
 * (entries older than ARP_CACHE_TIMEOUT_MS). This lazy expiration keeps the
 * cache clean without needing a separate cleanup thread.
 *
 * The function uses linear search through a linked list. For small networks,
 * this is efficient enough. For larger networks with many entries, a hash table
 * would provide O(1) lookups instead of O(n).
 *
 * @ip: IP address to look up (in network byte order)
 * @return: ARP cache entry if found and valid, NULL if not found or expired
 */
struct arp_cache_entry *arp_lookup(ip4_addr_t ip)
{
    uint64_t now = clock_get_uptime_milliseconds();
    uint64_t flags = arp_irq_save();
    struct arp_cache_entry *entry = arp_cache;
    struct arp_cache_entry *prev = NULL;
    
    while (entry)
    {
        /* Check if entry has expired. We do lazy expiration - expired entries
         * are removed during lookup rather than by a separate cleanup thread.
         * This is simpler and avoids needing timers or background tasks.
         */
        if (now - entry->timestamp > ARP_CACHE_TIMEOUT_MS)
        {
            /* Remove expired entry */
            struct arp_cache_entry *next = entry->next;
            if (prev)
            {
                prev->next = next;
            }
            else
            {
                arp_cache = next;
            }
            kfree(entry);
            entry = next;
            continue;
        }
        
        if (entry->ip == ip)
        {
            arp_irq_restore(flags);
            return entry;
        }
        
        prev = entry;
        entry = entry->next;
    }
    arp_irq_restore(flags);
    return NULL;
}

/* Remove the tail entry (oldest insert for prepend-only list) */
static void arp_cache_remove_oldest(void)
{
    if (!arp_cache)
        return;

    if (!arp_cache->next)
    {
        kfree(arp_cache);
        arp_cache = NULL;
        return;
    }

    struct arp_cache_entry *prev = arp_cache;
    while (prev->next->next)
        prev = prev->next;

    struct arp_cache_entry *tail = prev->next;
    prev->next = NULL;
    kfree(tail);
}

/**
 * arp_cache_add - Add or update an entry in the ARP cache
 *
 * This function maintains the ARP cache by adding new entries or updating
 * existing ones. If an entry for the IP already exists, it's updated with
 * the new MAC address and timestamp (this handles cases where a host changes
 * its MAC address or we receive updated information). If no entry exists,
 * a new one is created and added to the front of the list.
 *
 * The cache is updated automatically whenever we receive ARP packets (requests
 * or replies), allowing us to build up knowledge of the network passively.
 * This is called opportunistic caching - we learn mappings without explicitly
 * requesting them.
 *
 * @ip: IP address (in network byte order)
 * @mac: MAC address (6 bytes)
 */
void arp_cache_add(ip4_addr_t ip, const mac_addr_t mac)
{
    uint64_t flags;
    struct arp_cache_entry *entry;
    size_t cache_count = 0;

    if (!mac)
        return;

    flags = arp_irq_save();
    /* Check if entry already exists. arp_lookup() will return NULL if the
     * entry doesn't exist or has expired. If it exists, we update it rather
     * than creating a duplicate.
     */
    entry = arp_cache;
    while (entry)
    {
        if (entry->ip == ip)
            break;
        entry = entry->next;
    }

    if (entry)
    {
        /* Update existing entry: MAC address might have changed (host moved,
         * NIC replaced), or timestamp needs refreshing to prevent expiration.
         */
        memcpy(entry->mac, mac, 6);
        entry->timestamp = clock_get_uptime_milliseconds();
        arp_irq_restore(flags);
        LOG_INFO_FMT("ARP", "Updated ARP cache entry for IP " IP4_FMT, IP4_ARGS(ntohl(ip)));
        return;
    }

    for (struct arp_cache_entry *e = arp_cache; e; e = e->next)
        cache_count++;

    while (cache_count >= MAX_ARP_ENTRIES)
    {
        arp_cache_remove_oldest();
        cache_count--;
    }
    
    /* Create new entry */
    entry = (struct arp_cache_entry *)kmalloc(sizeof(struct arp_cache_entry));
    if (!entry)
    {
        arp_irq_restore(flags);
        LOG_ERROR("ARP", "Failed to allocate ARP cache entry");
        return;
    }
    
    entry->ip = ip;
    memcpy(entry->mac, mac, 6);
    entry->timestamp = clock_get_uptime_milliseconds();
    entry->next = arp_cache;
    arp_cache = entry;
    arp_irq_restore(flags);
    LOG_INFO_FMT("ARP", "Added ARP cache entry: IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 IP4_ARGS(ntohl(ip)),
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * arp_send_request - Send ARP request for given IP address
 * @dev: Network device to send on
 * @target_ip: IP address to resolve
 */
void arp_send_request(struct net_device *dev, ip4_addr_t target_ip)
{
    LOG_INFO_FMT("ARP", "Sending ARP request for IP " IP4_FMT, IP4_ARGS(ntohl(target_ip)));
    
    struct arp_header *request = (struct arp_header *)kmalloc(sizeof(struct arp_header));
    if (!request)
    {
        LOG_ERROR("ARP", "Failed to allocate ARP request");
        return;
    }
    
    memset(request, 0, sizeof(struct arp_header));
    request->hw_type = htons(ARP_HW_TYPE_ETHERNET);
    request->proto_type = htons(ARP_PROTO_TYPE_IPV4);
    request->hw_len = 6;
    request->proto_len = 4;
    request->opcode = htons(ARP_OP_REQUEST);
    
    /* Sender (us): prefer per-interface IP when configured */
    memcpy(request->sender_mac, dev->mac, 6);
    {
        ip4_addr_t sender_ip = my_ip;
        ip4_addr_t if_ip;

        if (arp_get_interface_ip(dev, &if_ip) == 0)
            sender_ip = if_ip;
        request->sender_ip = sender_ip;
    }
    
    /* Target (unknown MAC, IP to resolve) */
    memset(request->target_mac, 0, 6);
    /* target_ip is already in network byte order */
    request->target_ip = target_ip;
    
    /* Send ARP request as broadcast */
    if (net_send(dev, ETHERTYPE_ARP, broadcast_mac, request, sizeof(struct arp_header)) == 0)
    {
        LOG_INFO("ARP", "ARP Request sent");
    }
    else
    {
        LOG_ERROR("ARP", "Failed to send ARP Request");
    }
    
    kfree(request);
}

/**
 * arp_resolve - Resolve an IP address to a MAC address
 *
 * This is the main ARP resolution function used by the IP layer when it needs
 * to send a packet to a specific IP address. The resolution process:
 *
 *   1. Check ARP cache (fast path - most common case)
 *   2. If not cached, send ARP request and wait for reply
 *   3. Retry up to ARP_RESOLVE_RETRIES times with timeout per attempt
 *   4. Update cache and return MAC on success
 *
 * The function blocks until resolution completes or all retries are exhausted.
 * During the wait, it polls the network driver to ensure received packets
 * (including ARP replies) are processed promptly, since we might be waiting
 * for an interrupt-driven packet arrival.
 *
 * @dev: Network device to use for sending ARP requests
 * @ip: IP address to resolve (in network byte order)
 * @mac: Buffer to store resolved MAC address (6 bytes)
 * @return: 0 on success, -1 if resolution fails after all retries
 */
int arp_resolve(struct net_device *dev, ip4_addr_t ip, mac_addr_t mac)
{
    /* Don't try to resolve our own IP address - use our MAC directly */
    extern ip4_addr_t ip_local_addr;
    
    /* Check if this is our interface-specific IP */
    ip4_addr_t interface_ip = 0;
    if (dev)
    {
        extern int arp_get_interface_ip(struct net_device *dev, ip4_addr_t *ip_out);
        if (arp_get_interface_ip(dev, &interface_ip) == 0 && ip == interface_ip)
        {
            /* This is our interface IP, use our MAC */
            memcpy(mac, dev->mac, 6);
            LOG_INFO_FMT("ARP", "IP " IP4_FMT " is our interface IP, using our MAC", IP4_ARGS(ntohl(ip)));
            return 0;
        }
    }
    
    /* Check if this is our default IP */
    if (ip == my_ip || ip == ip_local_addr)
    {
        /* This is our own IP, use our MAC */
        if (dev)
        {
            memcpy(mac, dev->mac, 6);
            LOG_INFO_FMT("ARP", "IP " IP4_FMT " is our own IP, using our MAC", IP4_ARGS(ntohl(ip)));
            return 0;
        }
        return -1;  /* No device, can't get MAC */
    }
    
    /* Fast path: check ARP cache first. Most resolutions will hit the cache
     * since we opportunistically update it when ARP packets arrive. This
     * avoids sending unnecessary ARP requests for hosts we've recently
     * communicated with.
     */
    if (arp_lookup_mac(ip, mac) == 0)
    {
        LOG_INFO_FMT("ARP", "IP " IP4_FMT " resolved from cache", IP4_ARGS(ntohl(ip)));
        return 0;
    }
    
    /* Cache miss: need to send ARP request and wait for reply. This is the
     * slow path - we'll block here for up to (ARP_RESOLVE_TIMEOUT_MS * ARP_RESOLVE_RETRIES)
     * milliseconds waiting for a response. We poll the network driver during
     * the wait to ensure ARP replies are processed promptly.
     */
    LOG_INFO_FMT("ARP", "IP " IP4_FMT " not in cache, attempting resolution", IP4_ARGS(ntohl(ip)));
    
    for (int retry = 0; retry < ARP_RESOLVE_RETRIES; retry++)
    {
        if (retry > 0)
        {
            LOG_INFO_FMT("ARP", "Retry %d/%d for IP " IP4_FMT, 
                        retry, ARP_RESOLVE_RETRIES, IP4_ARGS(ntohl(ip)));
        }
        
        /* Send ARP request */
        arp_send_request(dev, ip);
        
        /* Wait for ARP reply with timeout */
        uint64_t start_time = clock_get_uptime_milliseconds();
        uint64_t timeout_ms = ARP_RESOLVE_TIMEOUT_MS;

        int check_count = 0;
        int max_checks = (timeout_ms / 10) + 10; /* Safety limit */

        while (check_count < max_checks)
        {
            uint64_t current_time = clock_get_uptime_milliseconds();
            uint64_t elapsed = 0;

            /* Check for overflow */
            if (current_time >= start_time)
            {
                elapsed = current_time - start_time;
            }
            else
            {
                LOG_WARNING("ARP", "Timer overflow detected!");
                elapsed = timeout_ms + 1; /* Force exit */
            }

            /* Check timeout */
            if (elapsed >= timeout_ms)
            {
                LOG_INFO_FMT("ARP", "Timeout reached: elapsed=%d ms >= timeout=%d ms",
                            (int)elapsed, (int)timeout_ms);
                break;
            }
            
            /* CRITICAL: Poll network stack for received packets */
            /* This processes packets through the full stack: Ethernet -> IP -> ARP */
            /* Using net_poll() instead of rtl8139_poll() ensures full stack processing */
            {
                net_poll();
            }
            
            /* Check if entry appeared in cache (ARP reply received) */
            if (arp_lookup_mac(ip, mac) == 0)
            {
                LOG_INFO_FMT("ARP", "IP " IP4_FMT " resolved after %d ms (attempt %d)",
                            IP4_ARGS(ntohl(ip)), (int)elapsed, retry + 1);
                LOG_INFO_FMT("ARP", "Resolved MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                return 0;
            }
            
            check_count++;
            
            /* Reduced logging: only log every 50 checks (every ~500ms) to reduce VGA clutter */
            if ((check_count % 50) == 0)
            {
                uint64_t now = clock_get_uptime_milliseconds();
                uint64_t elapsed_check = (now >= start_time) ? (now - start_time) : 0;
                /* Use serial only for verbose logging */
                extern void klog_print(const char *);
                extern void klog_hex32(uint32_t);
                klog_print("[ARP] Loop check #");
                klog_hex32((uint32_t)check_count);
                klog_print(", elapsed=");
                klog_hex32((uint32_t)elapsed_check);
                klog_print(" ms\n");
            }
            
            /* Small delay to allow interrupts to be processed */
            /* Instead of clock_sleep() which uses hlt (can block forever),
             * we do a small busy-wait loop that checks the timer periodically.
             * CRITICAL: We must enable interrupts during the delay to allow
             * timer interrupts to be processed and update the clock.
             */
            uint64_t delay_start = clock_get_uptime_milliseconds();
            uint64_t delay_target = delay_start + 10; /* 10ms delay */
            int delay_iterations = 0;
            
            /* Enable interrupts during delay to allow timer to advance */
            enable_interrupts();
            
            /* Busy-wait checking timer periodically */
            while (clock_get_uptime_milliseconds() < delay_target)
            {
                delay_iterations++;
                /* Check every ~1000 iterations to avoid excessive timer calls */
                if ((delay_iterations % 1000) == 0)
                {
                    /* If timer hasn't advanced, break to avoid infinite loop */
                    if (clock_get_uptime_milliseconds() == delay_start && delay_iterations > 10000)
                    {
                        LOG_WARNING("ARP", "Timer not advancing during delay, breaking delay loop");
                        break;
                    }
                }
                /* Small CPU pause to reduce power consumption and allow interrupts */
                cpu_relax();
            }
            
            /* Keep interrupts enabled to allow RX interrupts to be processed */
            /* Note: We don't disable interrupts here because we need RX interrupts
             * from the network card to process ARP replies. The ARP resolution
             * loop is already protected by checking the cache entry atomically.
             */
        }
        
        uint64_t elapsed = clock_get_uptime_milliseconds() - start_time;
        LOG_WARNING_FMT("ARP", "Timeout waiting for ARP reply after %d ms (attempt %d/%d, checks=%d)",
                       (int)elapsed, retry + 1, ARP_RESOLVE_RETRIES, check_count);
        
        /* Check if we should continue with next retry */
        if (retry < ARP_RESOLVE_RETRIES - 1)
        {
            LOG_INFO_FMT("ARP", "Continuing to next retry... (retry %d/%d)", 
                        retry + 2, ARP_RESOLVE_RETRIES);
        }
    }
    
    LOG_ERROR_FMT("ARP", "Failed to resolve IP " IP4_FMT " after %d attempts",
                 IP4_ARGS(ntohl(ip)), ARP_RESOLVE_RETRIES);
    return -1;
}

/**
 * arp_print_cache - Print all entries in ARP cache
 */
void arp_print_cache(void)
{
    uint64_t flags;
    struct arp_cache_entry *entry;
    int count = 0;
    
    LOG_INFO("ARP", "ARP Cache:");
    
    flags = arp_irq_save();
    entry = arp_cache;
    if (!entry)
    {
        arp_irq_restore(flags);
        LOG_INFO("ARP", "  (cache empty)");
        return;
    }
    
    while (entry)
    {
        LOG_INFO_FMT("ARP", "  %d. IP " IP4_FMT " -> MAC %02x:%02x:%02x:%02x:%02x:%02x",
                     ++count,
                     IP4_ARGS(ntohl(entry->ip)),
                     entry->mac[0], entry->mac[1], entry->mac[2],
                     entry->mac[3], entry->mac[4], entry->mac[5]);
        entry = entry->next;
    }
    arp_irq_restore(flags);
    LOG_INFO_FMT("ARP", "Total entries: %d", count);
}

/**
 * arp_set_my_ip - Update ARP's default IP address (synchronize with IP layer)
 * @ip: New IP address in network byte order
 */
void arp_set_my_ip(ip4_addr_t ip)
{
    uint64_t flags = arp_irq_save();
    my_ip = ip;
    arp_irq_restore(flags);
    LOG_INFO_FMT("ARP", "Updated ARP default IP address to " IP4_FMT, IP4_ARGS(ntohl(ip)));
}

/**
 * arp_set_interface_ip - Set IP address for a specific network interface
 * @dev: Network device
 * @ip: IP address in network byte order
 * @return: 0 on success, -1 on error
 */
int arp_set_interface_ip(struct net_device *dev, ip4_addr_t ip)
{
    uint64_t flags;

    if (!dev)
        return -1;

    flags = arp_irq_save();
    /* Check if interface already has an IP */
    struct arp_interface_ip *if_ip = interface_ips;
    while (if_ip)
    {
        if (if_ip->dev == dev)
        {
            /* Update existing IP */
            if_ip->ip = ip;
            arp_irq_restore(flags);
            LOG_INFO_FMT("ARP", "Updated IP for interface %s: " IP4_FMT,
                        dev->name, IP4_ARGS(ntohl(ip)));
            return 0;
        }
        if_ip = if_ip->next;
    }
    
    /* Create new interface IP entry */
    if_ip = kmalloc(sizeof(struct arp_interface_ip));
    if (!if_ip)
    {
        arp_irq_restore(flags);
        return -1;
    }
    
    if_ip->dev = dev;
    if_ip->ip = ip;
    if_ip->next = interface_ips;
    interface_ips = if_ip;
    arp_irq_restore(flags);
    LOG_INFO_FMT("ARP", "Set IP for interface %s: " IP4_FMT,
                dev->name, IP4_ARGS(ntohl(ip)));
    return 0;
}

/**
 * arp_get_interface_ip - Get IP address for a specific network interface
 * @dev: Network device
 * @ip: Output parameter for IP address
 * @return: 0 on success, -1 if interface has no IP
 */
int arp_get_interface_ip(struct net_device *dev, ip4_addr_t *ip)
{
    uint64_t flags;

    if (!dev || !ip)
        return -1;

    flags = arp_irq_save();
    struct arp_interface_ip *if_ip = interface_ips;
    while (if_ip)
    {
        if (if_ip->dev == dev)
        {
            *ip = if_ip->ip;
            arp_irq_restore(flags);
            return 0;
        }
        if_ip = if_ip->next;
    }
    arp_irq_restore(flags);
    return -1;
}

