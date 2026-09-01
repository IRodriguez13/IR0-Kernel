/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net.c
 * Description: Networking abstraction layer core implementation.
 *
 * This module implements the core networking abstraction layer that bridges
 * hardware network drivers (Layer 2 - Data Link) with network protocols
 * (Layer 3+ - Network and above). The design follows a layered architecture:
 *
 *   Layer 1 (Physical): Hardware NICs (RTL8139 driver)
 *   Layer 2 (Data Link): Ethernet frames, MAC addressing
 *   Layer 3 (Network): IP protocol (IPv4)
 *   Layer 4+ (Transport/Application): ICMP, UDP (DNS client over UDP)
 *
 * The abstraction provides:
 * - Device registration: NICs register themselves as net_device structures
 * - Protocol registration: Protocols (ARP, IP, ICMP) register handlers
 * - Frame encapsulation: net_send() wraps payloads in Ethernet headers
 * - Protocol demultiplexing: net_receive() routes frames to protocol handlers
 *
 * The key design is polymorphism: drivers expose a common net_device interface,
 * and protocols register handlers that get called when frames arrive. This
 * allows adding new drivers or protocols without modifying existing code.
 */

#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"
#include <ir0/net.h>
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/klog.h>
#include <string.h>

/* Global device and protocol lists. These linked lists maintain all registered
 * network interfaces and protocol handlers. Devices are registered by NIC drivers
 * during initialization, and protocols register themselves when their init()
 * functions are called (typically during init_net_stack()).
 */
static struct net_device *devices = NULL;
static struct net_protocol *protocols = NULL;

/**
 * net_register_device - Register a network interface with the networking layer
 *
 * Network drivers call this function during initialization to register their
 * device with the networking subsystem. Once registered, the device becomes
 * available for sending and receiving packets through the net_send() and
 * net_receive() functions. The device structure must remain valid for the
 * lifetime of the driver.
 *
 * This function maintains a linked list of registered devices. Duplicate
 * registrations are silently ignored (idempotent). The list is not protected
 * by locks, so registration should happen during driver initialization before
 * interrupts are enabled.
 *
 * @dev: Pointer to net_device structure (must persist for device lifetime)
 * @return: 0 on success (or if already registered), -1 on error
 */
int net_register_device(struct net_device *dev)
{
    if (!dev)
        return -1;

    /* Check if already registered - prevents double registration if driver
     * init is called multiple times. This makes registration idempotent.
     */
    struct net_device *curr = devices;
    while (curr)
    {
        if (curr == dev)
        {
            klog_info_fmt("NET", "Device already registered: %s",
                          dev->name ? dev->name : "(null)");
            return 0;
        }
        curr = curr->next;
    }

    /* Add to front of device list. This is O(1) insertion, and lookup
     * order doesn't matter for our use case. If we had many devices,
     * we might want to hash by name or maintain a sorted list.
     */
    dev->next = devices;
    devices = dev;

    klog_info_fmt("NET", "Registered device: %s",
                  dev->name ? dev->name : "(null)");

    return 0;
}

void net_unregister_device(struct net_device *dev)
{
    if (!dev || !devices)
        return;

    if (devices == dev)
    {
        devices = dev->next;
        return;
    }

    struct net_device *curr = devices;
    while (curr->next)
    {
        if (curr->next == dev)
        {
            curr->next = dev->next;
            return;
        }
        
        curr = curr->next;
    }
}

/**
 * net_send - Send an Ethernet frame through a network device
 *
 * This function encapsulates a protocol payload (e.g., IP packet, ARP message)
 * in an Ethernet frame and sends it through the specified network interface.
 * The function performs Layer 2 encapsulation: it prepends an Ethernet header
 * with source MAC (from device), destination MAC (provided by caller), and
 * EtherType (identifies payload protocol).
 *
 * The flow is:
 *   1. Validate parameters (device, callback, payload size vs MTU)
 *   2. Allocate buffer for complete frame (header + payload)
 *   3. Fill Ethernet header (dest MAC, src MAC, EtherType)
 *   4. Copy payload after header
 *   5. Call device's send callback (driver handles hardware transmission)
 *   6. Free frame buffer
 *
 * Note: The frame buffer is allocated and freed here, not by the driver.
 * This ensures consistent memory management, but means the driver must
 * copy or DMA the frame before returning (it can't hold references).
 *
 * @dev: Network device to send through (must be registered)
 * @ethertype: Ethernet type field (ETHERTYPE_IP, ETHERTYPE_ARP, etc.)
 * @dest_mac: Destination MAC address (6 bytes)
 * @payload: Protocol payload to send (IP packet, ARP message, etc.)
 * @len: Payload length in bytes (must fit within device MTU)
 * @return: 0 on success, -1 on error
 */
int net_send(struct net_device *dev, uint16_t ethertype, const uint8_t *dest_mac, const void *payload, size_t len)
{
    LOG_DEBUG_FMT("NET", "net_send: dev=%p, ethertype=0x%04x, payload=%p, len=%d",
                  dev, ethertype, payload, (int)len);
    
    if (!dev || !dev->send || !payload || len > dev->mtu)
    {
        LOG_ERROR_FMT("NET", "net_send: Invalid parameters (dev=%p, send=%p, payload=%p, len=%d, mtu=%d)",
                     dev, dev ? dev->send : NULL, payload, (int)len, dev ? (int)dev->mtu : 0);
        return -1;
    }

    if (!dest_mac)
    {
        klog_error("NET", "net_send: NULL destination MAC");
        return -1;
    }

    /* Calculate total frame length: Ethernet header (14 bytes) + payload.
     * Standard Ethernet MTU is 1500 bytes, so frames are typically <= 1514 bytes.
     */
    size_t frame_len = sizeof(struct eth_header) + len;
    LOG_DEBUG_FMT("NET", "net_send: Allocating frame buffer, frame_len=%d", (int)frame_len);
    uint8_t *frame = kmalloc(frame_len);

    if (!frame)
    {
        LOG_ERROR("NET", "net_send: Failed to allocate frame buffer");
        return -1;
    }
    LOG_DEBUG_FMT("NET", "net_send: Frame buffer allocated at %p", frame);

    /* Fill Ethernet header. The header structure is:
     *   - dest[6]: Destination MAC address
     *   - src[6]: Source MAC address (from device)
     *   - type: EtherType (network byte order)
     */
    struct eth_header *eth = (struct eth_header *)frame;
    memcpy(eth->dest, dest_mac, 6);
    memcpy(eth->src, dev->mac, 6);
    eth->type = htons(ethertype);
    LOG_DEBUG_FMT("NET", "net_send: Ethernet header filled, copying %d bytes of payload", (int)len);

    /* Copy payload after Ethernet header. Protocols (IP, ARP) place their
     * packets here. The driver will transmit the entire frame including header.
     */
    memcpy(frame + sizeof(struct eth_header), payload, len);
    LOG_DEBUG("NET", "net_send: Payload copied, calling dev->send");
    
    /* Verify frame length is reasonable before passing to driver.
     * This sanity check prevents buffer overflows if len was corrupted.
     */
    if (frame_len > 2000)
    {
        LOG_ERROR_FMT("NET", "net_send: CORRUPTION DETECTED! frame_len=%d is too large!", (int)frame_len);
        kfree(frame);
        return -1;
    }

    /* Call device's send function. The driver is responsible for:
     * - DMA'ing the frame to hardware (or copying to hardware buffers)
     * - Handling transmission queue management
     * - Triggering hardware transmission
     * The frame must be fully transmitted or queued before this returns.
     */
    int ret = dev->send(dev, frame, frame_len);
    LOG_DEBUG_FMT("NET", "net_send: dev->send returned %d, freeing frame buffer", ret);

    /* Free frame buffer. The driver must have copied/DMA'd the frame by now.
     * If the driver needs to keep the frame (e.g., for retransmission), it
     * must allocate its own buffer and copy the data.
     */
    kfree(frame);
    LOG_DEBUG("NET", "net_send: Frame buffer freed, returning");
    return ret;
}

/**
 * net_receive - Process a received Ethernet frame and route to protocol handler
 *
 * Network drivers call this function when they receive a frame from hardware.
 * This function performs Layer 2 demultiplexing: it examines the EtherType
 * field in the Ethernet header to determine which protocol handler should
 * process the frame, then extracts the payload and calls the appropriate
 * handler.
 *
 * The demultiplexing process:
 *   1. Validate frame (check minimum length, valid pointers)
 *   2. Parse Ethernet header to extract EtherType
 *   3. Look up protocol handler in registered protocols list
 *   4. Extract payload (frame minus Ethernet header)
 *   5. Call protocol's receive handler
 *
 * If no handler is registered for the EtherType, the frame is silently dropped.
 * This is normal for unknown protocols or frames not intended for this system.
 *
 * Note: The frame data buffer is owned by the driver and may be reused after
 * this function returns. Protocol handlers must copy any data they need to keep.
 *
 * @dev: Network device that received the frame
 * @data: Pointer to received Ethernet frame (must include header + payload)
 * @len: Total frame length (header + payload)
 */
void net_receive(struct net_device *dev, const void *data, size_t len)
{
    if (!dev || !data || len < sizeof(struct eth_header))
    {
        if (dev)
            dev->rx_dropped++;
        LOG_WARNING_FMT("NET", "Invalid packet: dev=%p, data=%p, len=%d", dev, data, (int)len);
        return;
    }

    /* Parse Ethernet header to extract EtherType. The EtherType field
     * identifies the protocol of the payload (IP, ARP, IPv6, etc.).
     */
    struct eth_header *eth = (struct eth_header *)data;
    uint16_t type = ntohs(eth->type);

    /* Check if packet is broadcast or for our MAC */
    bool is_broadcast = (eth->dest[0] == 0xFF && eth->dest[1] == 0xFF && 
                         eth->dest[2] == 0xFF && eth->dest[3] == 0xFF && 
                         eth->dest[4] == 0xFF && eth->dest[5] == 0xFF);
    
    /* Check if packet is multicast (not broadcast, but multicast MAC) */
    bool is_multicast = !is_broadcast && (eth->dest[0] & 0x01) == 0x01;
    
    bool is_for_us = is_broadcast || 
                     (memcmp(eth->dest, dev->mac, 6) == 0);

    if (is_multicast)
        dev->rx_multicast++;
    
    /* FILTER: Drop multicast packets early if they're not for us and not ARP/IP protocols we handle.
     * This prevents spam from mDNS, SSDP, IPv6 neighbor discovery, etc.
     * We only process:
     *   - Broadcast packets (for ARP requests)
     *   - Unicast packets for our MAC
     *   - ARP packets (even multicast, as some systems use multicast for ARP)
     */
    if (is_multicast && !is_for_us && type != ETHERTYPE_ARP)
    {
        /* Drop multicast IPv6 and non-ARP multicast IPv4 silently */
        dev->rx_dropped++;
        /* Log only occasionally to avoid spam (every 100th packet) */
        static int multicast_drop_count = 0;
        multicast_drop_count++;
        if ((multicast_drop_count % 100) == 0)
        {
            LOG_DEBUG_FMT("NET", "Dropped %d multicast packets (mDNS/SSDP/IPv6 ND)", multicast_drop_count);
        }
        return;
    }
    
    /* Hot path: keep packet traces at DEBUG to avoid serial spam (F8 smokes). */
    LOG_DEBUG_FMT("NET", "Received packet on %s: len=%d, type=0x%04x, src_mac=%02x:%02x:%02x:%02x:%02x:%02x, dst_mac=%02x:%02x:%02x:%02x:%02x:%02x, broadcast=%d, multicast=%d, for_us=%d",
                 dev->name, (int)len, type,
                 eth->src[0], eth->src[1], eth->src[2], eth->src[3], eth->src[4], eth->src[5],
                 eth->dest[0], eth->dest[1], eth->dest[2], eth->dest[3], eth->dest[4], eth->dest[5],
                 is_broadcast ? 1 : 0, is_multicast ? 1 : 0, is_for_us ? 1 : 0);
    
    /* CRITICAL: Process ALL ARP packets, not just broadcast ones.
     * Some systems send unicast ARP requests or ARP replies might be unicast.
     * We need to process all ARP traffic to respond correctly.
     */
    if (type == ETHERTYPE_ARP)
    {
        if (is_broadcast)
        {
            LOG_DEBUG("NET", "Received broadcast ARP packet - processing");
        }
        else
        {
            LOG_DEBUG("NET", "Received unicast ARP packet - processing");
        }
    }
    else if (!is_for_us && type != ETHERTYPE_ARP)
    {
        /* For non-ARP packets not for us, we'll still log but note they're being dropped */
        /* This helps debug connectivity issues */
        LOG_DEBUG_FMT("NET", "Packet not for us (not broadcast, not our MAC) - will be dropped by protocol handler");
    }

    /* Look up protocol handler by EtherType. Protocols register themselves
     * during initialization (e.g., ARP registers for ETHERTYPE_ARP).
     * 
     * CRITICAL: Process ALL packets that arrive, regardless of destination MAC.
     * This ensures we receive broadcast ARP requests even if MAC filtering
     * might otherwise drop them. The protocol handlers will determine if
     * the packet is actually for us.
     */
    struct net_protocol *proto = net_find_protocol_by_ethertype(type);
    if (proto && proto->handler)
    {
        LOG_DEBUG_FMT("NET", "Found protocol handler: %s", proto->name);
        
        /* Extract payload: skip Ethernet header (14 bytes), pass rest to protocol.
         * The protocol handler will parse its own header (IP, ARP, etc.).
         */
        const void *payload = (const uint8_t *)data + sizeof(struct eth_header);
        size_t payload_len = len - sizeof(struct eth_header);
        
        LOG_DEBUG_FMT("NET", "Calling protocol handler %s with payload_len=%d", proto->name, (int)payload_len);
        
        /* Call protocol handler. The handler receives:
         *   - dev: Network device (for sending replies if needed)
         *   - payload: Protocol packet (IP, ARP header + data)
         *   - payload_len: Length of protocol packet
         *   - proto->priv: Protocol-specific private data (can be NULL)
         */
        proto->handler(dev, payload, payload_len, proto->priv);
        LOG_DEBUG_FMT("NET", "Protocol handler %s returned", proto->name);
    }
    else
    {
        dev->rx_dropped++;
        /* CRITICAL: Log ARP packets even if handler not found (shouldn't happen) */
        if (type == ETHERTYPE_ARP)
        {
            LOG_ERROR_FMT("NET", "ARP packet received but NO HANDLER FOUND! EtherType=0x%04x", type);
            LOG_ERROR("NET", "This should never happen - ARP protocol should be registered!");
        }
        /* No handler registered for this EtherType. This is normal for:
         * - Unknown protocols
         * - Frames not intended for this system
         * - Protocols not registered for this EtherType (e.g. unknown L2 types)
         * 
         * Note: We silently drop these frames as they're not for us or not supported.
         * Excessive logging would spam the serial output.
         */
        /* Log only in debug mode to avoid spam */
        #ifdef NET_DEBUG
        klog_debug_fmt("NET", "No handler registered for EtherType 0x%x",
                       (unsigned)type);
        #endif
    }
}

struct net_device *net_get_devices(void)
{
    return devices;
}

/* --- Protocol Registration System --- */

/**
 * net_register_protocol - Register a protocol handler for incoming frames
 *
 * Protocol implementations (ARP, IP, etc.) call this during initialization to
 * register their receive handler. When frames arrive with a matching EtherType
 * or IP protocol number, the handler will be called.
 *
 * Protocols can register for either:
 *   - Layer 2 (EtherType): ARP, IP register via ethertype field
 *   - Layer 3+ (IP protocol): ICMP and UDP register via ipproto field
 *
 * The protocol structure must persist for the lifetime of the protocol. This
 * function is idempotent: registering the same protocol twice is a no-op.
 *
 * @proto: Protocol registration structure (must persist)
 * @return: 0 on success, -1 on error
 */
int net_register_protocol(struct net_protocol *proto)
{
    if (!proto || !proto->name || !proto->handler)
        return -1;

    /* Check if already registered */
    struct net_protocol *curr = protocols;
    while (curr)
    {
        if (curr == proto)
        {
            klog_info_fmt("NET", "Protocol already registered: %s",
                          proto->name ? proto->name : "(null)");
            return 0;
        }
        curr = curr->next;
    }

    /* Add to protocol list */
    proto->next = protocols;
    protocols = proto;

    if (proto->ethertype != 0 && proto->ipproto != 0)
        klog_info_fmt("NET",
                      "Registered protocol: %s (EtherType: 0x%x) (IP Proto: 0x%x)",
                      proto->name, (unsigned)proto->ethertype,
                      (unsigned)proto->ipproto);
    else if (proto->ethertype != 0)
        klog_info_fmt("NET", "Registered protocol: %s (EtherType: 0x%x)",
                      proto->name, (unsigned)proto->ethertype);
    else if (proto->ipproto != 0)
        klog_info_fmt("NET", "Registered protocol: %s (IP Proto: 0x%x)",
                      proto->name, (unsigned)proto->ipproto);
    else
        klog_info_fmt("NET", "Registered protocol: %s", proto->name);

    return 0;
}

/**
 * Unregister a network protocol handler
 * @proto: Protocol registration structure
 */
void net_unregister_protocol(struct net_protocol *proto)
{
    if (!proto || !protocols)
        return;

    if (protocols == proto)
    {
        protocols = proto->next;
        klog_info_fmt("NET", "Unregistered protocol: %s",
                      proto->name ? proto->name : "(null)");
        return;
    }

    struct net_protocol *curr = protocols;
    while (curr->next)
    {
        if (curr->next == proto)
        {
            curr->next = proto->next;
            klog_info_fmt("NET", "Unregistered protocol: %s",
                          proto->name ? proto->name : "(null)");
            return;
        }
        curr = curr->next;
    }
}

/**
 * net_find_protocol_by_ethertype - Find protocol handler by EtherType (Layer 2)
 *
 * This function searches the registered protocols list for a handler matching
 * the given EtherType. Used by net_receive() to route incoming Ethernet frames
 * to the correct protocol handler. EtherTypes identify Layer 2 protocols like
 * ARP (0x0806) and IP (0x0800).
 *
 * Time complexity: O(n) where n is number of registered protocols. This is
 * acceptable since protocol registration is small (typically < 10 protocols).
 * For higher performance, we could use a hash table keyed by EtherType.
 *
 * @ethertype: Ethernet type in host byte order (e.g., ETHERTYPE_ARP, ETHERTYPE_IP)
 * @return: Protocol structure if found, NULL otherwise
 */
struct net_protocol *net_find_protocol_by_ethertype(uint16_t ethertype)
{
    struct net_protocol *curr = protocols;
    while (curr)
    {
        if (curr->ethertype == ethertype)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * net_find_protocol_by_ipproto - Find protocol handler by IP protocol number (Layer 3+)
 *
 * This function searches for a protocol handler registered for a specific IP
 * protocol number. Used by the IP layer (ip_receive_handler) to route incoming
 * IP packets to upper-layer protocols (this stack: ICMP and UDP). IP
 * protocol numbers identify what type of data is in the IP payload.
 *
 * Common protocol numbers (IANA; IR0 implements 1 and 17 only):
 *   - 1: ICMP (Internet Control Message Protocol)
 *   - 17: UDP (User Datagram Protocol)
 *
 * @ipproto: IP protocol number (e.g., IPPROTO_ICMP=1, IPPROTO_UDP=17)
 * @return: Protocol structure if found, NULL otherwise
 */
struct net_protocol *net_find_protocol_by_ipproto(uint8_t ipproto)
{
    struct net_protocol *curr = protocols;
    while (curr)
    {
        if (curr->ipproto == ipproto)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * init_net_stack - Initialize the complete network stack
 *
 * This function performs the full initialization sequence for the networking
 * subsystem. It must be called during kernel boot before any network operations
 * can be performed. The initialization order is critical:
 *
 *   1. Network driver (RTL8139): Register hardware device
 *   2. ARP protocol: Must initialize before IP (IP needs ARP for MAC resolution)
 *   3. IP protocol: Must initialize before ICMP (ICMP is carried over IP)
 *   4. ICMP protocol: Depends on IP for packet delivery
 *   5. DNS defaults: must be available even if DHCP is deferred
 *
 * If any step fails, the function returns an error and the stack is considered
 * uninitialized. Subsequent network operations may fail or panic.
 *
 * Thread safety: This function should only be called once during boot, before
 * interrupts are enabled. It's not safe to call concurrently.
 *
 * @return: 0 on success, -1 if any initialization step fails
 */
int init_net_stack(void)
{
    LOG_INFO("NET", "Initializing network stack");
    
    /* Initialize network drivers first. These register net_device structures
     * that protocols will use for sending packets. Drivers probe hardware
     * and set up interrupt handlers. If hardware isn't present, drivers
     * gracefully return without registering devices.
     */
    {
        extern int net_stack_probe_drivers(void) __attribute__((weak));
        if (net_stack_probe_drivers && net_stack_probe_drivers() != 0)
        {
            LOG_WARNING("NET", "Some network drivers failed to probe");
        }
    }
    
    /* Initialize network protocols in dependency order. ARP must come before
     * IP because IP uses ARP to resolve MAC addresses. IP must come before
     * ICMP because ICMP packets are encapsulated in IP. This ordering ensures
     * that when a protocol initializes, its dependencies are already available.
     */
    if (arp_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize ARP protocol");
        return -1;
    }
    
    if (ip_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize IP protocol");
        return -1;
    }
    
    if (icmp_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize ICMP protocol");
        return -1;
    }
    
    if (udp_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize UDP protocol");
        return -1;
    }

    if (tcp_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize TCP protocol");
        return -1;
    }

    if (dns_init() != 0)
    {
        LOG_ERROR("NET", "Failed to initialize DNS client");
        return -1;
    }
    
    LOG_INFO("NET", "Network stack initialized successfully");
    return 0;
}

int net_stack_post_irq_init(void)
{
    /*
     * DHCP waits rely on uptime progression. Run this only after clock/IRQ
     * plumbing is active (kmain calls us right after global sti).
     */
    if (dhcp_init() != 0)
    {
        LOG_WARNING("NET", "DHCP post-IRQ init failed, keeping static network configuration");
        return -1;
    }

    return 0;
}

/**
 * net_poll - Poll network devices for incoming packets
 * 
 * This function should be called periodically to ensure the kernel receives
 * incoming packets even when not actively waiting for responses. This is
 * necessary because the polling mechanism only activates when waiting for
 * specific responses (e.g., ARP replies).
 * 
 * This function polls all registered network devices to check for incoming
 * packets. It should be called from a periodic timer or main loop.
 */
void net_poll(void)
{
    struct net_device *dev = devices;
    while (dev)
    {
        if (dev->poll)
            dev->poll(dev);
        dev = dev->next;
    }
}

void net_stack_poll(void)
{
    net_poll();
}

int net_stack_get_irq_line(void)
{
    struct net_device *dev = devices;
    while (dev)
    {
        if (dev->get_irq_line)
        {
            int irq = dev->get_irq_line(dev);
            if (irq >= 0)
                return irq;
        }
        dev = dev->next;
    }
    return -1;
}

int net_stack_handle_irq(uint8_t irq)
{
    int handled = 0;
    struct net_device *dev = devices;
    while (dev)
    {
        if (dev->handle_irq && dev->handle_irq(dev, irq) > 0)
            handled = 1;
        dev = dev->next;
    }
    return handled;
}

void net_stack_get_stats(uint64_t *rx_pkts, uint64_t *tx_pkts,
                         uint64_t *rx_errs, uint64_t *tx_errs)
{
    uint64_t rx_total = 0;
    uint64_t tx_total = 0;
    uint64_t rx_err_total = 0;
    uint64_t tx_err_total = 0;
    struct net_device *dev = devices;

    while (dev)
    {
        if (dev->get_stats)
        {
            uint64_t drx = 0, dtx = 0, drxe = 0, dtxe = 0;
            dev->get_stats(dev, &drx, &dtx, &drxe, &dtxe);
            rx_total += drx;
            tx_total += dtx;
            rx_err_total += drxe;
            tx_err_total += dtxe;
        }
        dev = dev->next;
    }

    if (rx_pkts) *rx_pkts = rx_total;
    if (tx_pkts) *tx_pkts = tx_total;
    if (rx_errs) *rx_errs = rx_err_total;
    if (tx_errs) *tx_errs = tx_err_total;
}
