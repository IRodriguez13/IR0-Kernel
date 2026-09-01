/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net_compat.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Networking compatibility stubs
 *
 * Weak fallbacks used when networking backend is not linked.
 * Real implementations in net/net.c override these symbols.
 */

#include <ir0/errno.h>
#include <ir0/net.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/dns.h>
#include <net/arp.h>

ip4_addr_t ip_local_addr __attribute__((weak));
ip4_addr_t ip_netmask __attribute__((weak));
ip4_addr_t ip_gateway __attribute__((weak));

__attribute__((weak)) void net_stack_poll(void)
{
}

__attribute__((weak)) int net_stack_post_irq_init(void)
{
    /*
     * Reached only when no networking backend is linked. The single caller is
     * the explicit "dhcp" devfs command, so a real failure must be visible to
     * userspace; boot does not depend on this hook.
     */
    return -ENODEV;
}

__attribute__((weak)) int net_stack_get_irq_line(void)
{
    return -1;
}

__attribute__((weak)) int net_stack_handle_irq(uint8_t irq)
{
    (void)irq;
    return 0;
}

__attribute__((weak)) void net_stack_get_stats(uint64_t *rx_pkts, uint64_t *tx_pkts,
                                               uint64_t *rx_errs, uint64_t *tx_errs)
{
    if (rx_pkts)
        *rx_pkts = 0;
    if (tx_pkts)
        *tx_pkts = 0;
    if (rx_errs)
        *rx_errs = 0;
    if (tx_errs)
        *tx_errs = 0;
}

__attribute__((weak)) void net_poll(void)
{
}

__attribute__((weak)) struct net_device *net_get_devices(void)
{
    return (struct net_device *)0;
}

__attribute__((weak)) int icmp_send_echo_request(struct net_device *dev, ip4_addr_t dest_ip,
                                                 uint16_t id, uint16_t seq,
                                                 const void *data, size_t len)
{
    (void)dev;
    (void)dest_ip;
    (void)id;
    (void)seq;
    (void)data;
    (void)len;
    return -1;
}

__attribute__((weak)) bool icmp_get_echo_result(uint16_t id, uint16_t seq, uint64_t *rtt_out,
                                                uint8_t *ttl_out, size_t *payload_bytes_out,
                                                ip4_addr_t *reply_ip_out)
{
    (void)id;
    (void)seq;
    (void)rtt_out;
    (void)ttl_out;
    (void)payload_bytes_out;
    (void)reply_ip_out;
    return false;
}

__attribute__((weak)) bool icmp_get_next_echo_result(uint16_t id, uint16_t *seq_out, uint64_t *rtt_out,
                                                     uint8_t *ttl_out, size_t *payload_bytes_out,
                                                     ip4_addr_t *reply_ip_out)
{
    (void)id;
    (void)seq_out;
    (void)rtt_out;
    (void)ttl_out;
    (void)payload_bytes_out;
    (void)reply_ip_out;
    return false;
}

__attribute__((weak)) bool icmp_has_ready_echo_result(uint16_t id)
{
    (void)id;
    return false;
}

__attribute__((weak)) uint16_t icmp_allocate_echo_seq(void)
{
    return 1;
}

__attribute__((weak)) ip4_addr_t dns_resolve(const char *domain_name, ip4_addr_t dns_server_ip)
{
    (void)domain_name;
    (void)dns_server_ip;
    return 0;
}

__attribute__((weak)) void dns_set_default_server(ip4_addr_t dns_server_ip)
{
    (void)dns_server_ip;
}

__attribute__((weak)) ip4_addr_t dns_get_default_server(void)
{
    return 0;
}

__attribute__((weak)) void arp_set_my_ip(ip4_addr_t ip)
{
    (void)ip;
}

__attribute__((weak)) int arp_set_interface_ip(struct net_device *dev, ip4_addr_t ip)
{
    (void)dev;
    (void)ip;
    return -1;
}
