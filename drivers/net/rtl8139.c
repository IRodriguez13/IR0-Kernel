/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtl8139.c
 * Description: RTL8139 network card driver implementation
 */

#include <ir0/net.h>
#include "rtl8139.h"
#include <stdbool.h>
#include <ir0/ktm/klog.h>
#include <ir0/arch_io.h>
#include <mm/allocator.h>
#include <ir0/kmem.h>
#include <string.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <ir0/oops.h>
#include <ir0/cpu.h>  /* For ASSERT and panicex */
#include <ir0/resource_registry.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <ir0/ktm/klog.h>

/*
 * Bus/DMA address for RTL8139 register writes. The kernel identity-maps
 * low memory, so kernel virtual addresses match physical addresses for
 * these buffers. Do not use for highmem or non-identity mappings.
 */
#define KVA_TO_PHYS(ptr) ((uint32_t)(uintptr_t)(ptr))

/* Global driver state */
static uint16_t rtl8139_io_base = 0;
static int rtl8139_irq_line = -1;
static uint8_t *rtl8139_rx_buffer = NULL;
static uint8_t *rtl8139_tx_buffers[4] = {NULL, NULL, NULL, NULL}; /* 4 TX descriptors */
static uint32_t rtl8139_current_tx_descriptor = 0;
static uint8_t rtl8139_mac[6];
static uint16_t rtl8139_rx_read_offset = 0; /* Current read offset in RX buffer */
static volatile uint8_t rtl8139_rx_processing = 0;

/* TX tracking for robust DMA management */
#define RTL8139_MAX_TX_IN_FLIGHT 4  /* Maximum number of TX packets in flight */
static volatile uint32_t rtl8139_tx_in_flight = 0;  /* Counter of TX packets currently being DMA'd */
static volatile uint32_t rtl8139_tx_descriptor_own_state[4] = {1, 1, 1, 1};  /* 1 = host owns (idle) */

/*
 * Packet and error counters for /proc/net/dev and diagnostics.
 * Updated from RX processing, TX completion, and interrupt status.
 */
static struct rtl8139_counters {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
} rtl8139_counters;

/* Packet lengths queued per TX descriptor (added to tx_bytes on TOK). */
static uint32_t rtl8139_tx_pending_len[4];

/*
 * Per-descriptor TX start time (clock ticks) for stuck-DMA detection;
 * tx_timeout_latched avoids counting the same stall repeatedly.
 */
static uint64_t rtl8139_tx_desc_start_tick[4];
static uint8_t rtl8139_tx_timeout_latched[4];

static struct net_device rtl8139_dev;

/* Rate-limit full TX ring recover (ms-scale via timer ticks). */
static uint64_t rtl8139_last_full_recover_tick;
static uint32_t rtl8139_recover_suppressed;

/* Forward declarations */
static int32_t rtl8139_hw_init(void);
static void rtl8139_check_tx_completion(void);
static int rtl8139_find_free_tx_descriptor(void);

/*
 * Force-recover TX path when descriptors appear wedged.
 * This drops any in-flight frames and re-arms all TX descriptors.
 * Rate-limited: at most one full recover per ~100ms of timer ticks.
 * Returns 1 if a full recover ran, 0 if suppressed.
 */
static int rtl8139_recover_tx_path(void)
{
    uint64_t now;
    uint32_t hz;

    if (!rtl8139_io_base)
        return 0;

    now = clock_get_tick_count();
    hz = clock_get_timer_frequency();
    if (hz != 0 && rtl8139_last_full_recover_tick != 0)
    {
        uint64_t window = (uint64_t)hz / 10; /* ~100ms */

        if (window == 0)
            window = 1;
        if ((now - rtl8139_last_full_recover_tick) < window)
        {
            rtl8139_recover_suppressed++;
            LOG_DEBUG("RTL8139", "TX recover suppressed (rate limit)");
            return 0;
        }
    }
    rtl8139_last_full_recover_tick = now;

    uint8_t cr = inb(rtl8139_io_base + RTL8139_REG_CR);
    uint8_t rx_enabled = cr & RTL8139_CR_RE;

    /* Temporarily disable TX engine, then re-enable it. */
    outb(rtl8139_io_base + RTL8139_REG_CR, rx_enabled);
    outb(rtl8139_io_base + RTL8139_REG_CR, (uint8_t)(rx_enabled | RTL8139_CR_TE));

    for (int i = 0; i < 4; i++)
    {
        if (rtl8139_tx_buffers[i])
        {
            uint32_t phys_addr = KVA_TO_PHYS(rtl8139_tx_buffers[i]);
            outl(rtl8139_io_base + RTL8139_REG_TSAD0 + (i * 4), phys_addr);
        }
        outl(rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4), RTL8139_TSD_OWN);
        rtl8139_tx_descriptor_own_state[i] = 1;
        rtl8139_tx_desc_start_tick[i] = 0;
        rtl8139_tx_timeout_latched[i] = 0;
    }
    rtl8139_tx_in_flight = 0;
    rtl8139_current_tx_descriptor = 0;

    /* Ack TX interrupts/errors that may have latched. */
    outw(rtl8139_io_base + RTL8139_REG_ISR, RTL8139_INT_TOK | RTL8139_INT_TER | RTL8139_INT_PUN);
    if (rtl8139_recover_suppressed)
    {
        LOG_DEBUG_FMT("RTL8139",
                      "TX path recovered (suppressed=%u prior)",
                      rtl8139_recover_suppressed);
        rtl8139_recover_suppressed = 0;
    }
    else
    {
        LOG_WARNING("RTL8139", "TX path recovered (descriptor ring re-armed)");
    }
    return 1;
}

static inline uint64_t rtl8139_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void rtl8139_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

static bool rtl8139_try_enter_rx(void)
{
    uint64_t flags = rtl8139_irq_save();
    if (rtl8139_rx_processing)
    {
        rtl8139_irq_restore(flags);
        return false;
    }
    rtl8139_rx_processing = 1;
    rtl8139_irq_restore(flags);
    return true;
}

static void rtl8139_leave_rx(void)
{
    uint64_t flags = rtl8139_irq_save();
    rtl8139_rx_processing = 0;
    rtl8139_irq_restore(flags);
}

static ir0_driver_ops_t rtl8139_ops = {
    .init = rtl8139_hw_init,
    .shutdown = NULL
};

static ir0_driver_info_t rtl8139_info = {
    .name = "RTL8139",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "Realtek RTL8139 PCI Fast Ethernet Driver",
    .language = IR0_DRIVER_LANG_C
};

/* Forward declarations for net_device ops */
static int rtl8139_netdev_send(struct net_device *dev, void *data, size_t len);
static void rtl8139_netdev_poll(struct net_device *dev);
static int rtl8139_netdev_get_irq_line(struct net_device *dev);
static int rtl8139_netdev_handle_irq(struct net_device *dev, uint8_t irq);
static void rtl8139_netdev_get_stats(struct net_device *dev, uint64_t *rx_pkts, uint64_t *tx_pkts,
                                     uint64_t *rx_errs, uint64_t *tx_errs);
static void rtl8139_netdev_get_byte_stats(struct net_device *dev, uint64_t *rx_bytes,
					  uint64_t *tx_bytes);

/* PCI configuration space access */
static uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)PCI_ENABLE_BIT));
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)PCI_ENABLE_BIT));
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    outl(PCI_CONFIG_DATA_PORT, data);
}

/* PCI device discovery */
static int find_rtl8139(uint8_t *bus, uint8_t *slot)
{
    for (uint16_t b = 0; b < 256; b++)
    {
        for (uint8_t s = 0; s < 32; s++)
        {
            uint32_t id = pci_config_read(b, s, 0, 0);
            if ((id & 0xFFFF) == RTL8139_VENDOR_ID && (id >> 16) == RTL8139_DEVICE_ID)
            {
                *bus = (uint8_t)b;
                *slot = s;
                return 0;
            }
        }
    }
    return -1;
}

/**
 * rtl8139_init - register RTL8139 driver
 */
int rtl8139_init(void)
{
    LOG_INFO("RTL8139", "Registering RTL8139 driver...");
    ir0_register_driver(&rtl8139_info, &rtl8139_ops);
    return 0;
}

static int32_t rtl8139_hw_init(void)
{
    uint8_t bus, slot;
    int i;

    LOG_INFO("RTL8139", "Searching for device...");

    if (find_rtl8139(&bus, &slot) != 0)
    {
        LOG_WARNING("RTL8139", "Device not found (absent)");
        return IR0_DRIVER_ABSENT;
    }

    LOG_INFO_FMT("RTL8139", "Found device at PCI %d:%d", (int)bus, (int)slot);

    memset(&rtl8139_counters, 0, sizeof(rtl8139_counters));
    memset(rtl8139_tx_desc_start_tick, 0, sizeof(rtl8139_tx_desc_start_tick));
    memset(rtl8139_tx_timeout_latched, 0, sizeof(rtl8139_tx_timeout_latched));

    /* Read BAR0 to get I/O base address */
    uint32_t bar0 = pci_config_read(bus, slot, 0, PCI_REG_BAR0);
    if (!(bar0 & 1))
    {
        LOG_ERROR_FMT("RTL8139", "BAR0 is not I/O space. BAR0=0x%x", bar0);
        return -1;
    }
    rtl8139_io_base = bar0 & ~0x3;
    LOG_INFO_FMT("RTL8139", "I/O Base address: 0x%x", rtl8139_io_base);

    /* Enable PCI Bus Mastering and I/O Space */
    uint32_t command = pci_config_read(bus, slot, 0, PCI_REG_COMMAND);
    command |= (PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER);
    pci_config_write(bus, slot, 0, PCI_REG_COMMAND, command);

    /* Software Reset */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_RST);
    {
        int reset_iters = 0;
        while ((inb(rtl8139_io_base + RTL8139_REG_CR) & RTL8139_CR_RST))
        {
            if (++reset_iters >= 1000)
            {
                klog_info("RTL8139", "ERROR: software reset timeout (CR_RST stuck)");
                return -1;
            }
        }
    }
    LOG_INFO("RTL8139", "Software reset complete.");

    /* Read MAC address (REAL DATA) */
    for (int i = 0; i < 6; i++)
    {
        rtl8139_mac[i] = inb(rtl8139_io_base + RTL8139_REG_MAC0 + i);
    }
    LOG_INFO_FMT("RTL8139", "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 rtl8139_mac[0], rtl8139_mac[1], rtl8139_mac[2],
                 rtl8139_mac[3], rtl8139_mac[4], rtl8139_mac[5]);

    /* Initialize RX buffer */
    rtl8139_rx_buffer = (uint8_t *)kmalloc(RTL8139_RX_BUF_SIZE + RTL8139_RX_BUF_PADDING);
    if (!rtl8139_rx_buffer)
    {
        LOG_ERROR("RTL8139", "Failed to allocate RX buffer");
        return -1;
    }
    memset(rtl8139_rx_buffer, 0, RTL8139_RX_BUF_SIZE + RTL8139_RX_BUF_PADDING);
    outl(rtl8139_io_base + RTL8139_REG_RBSTART, KVA_TO_PHYS(rtl8139_rx_buffer));
    rtl8139_rx_read_offset = 0; /* Initialize read offset */

    /* Initialize TX buffers (one per descriptor) */
    /* CRITICAL DMA REQUIREMENTS:
     * 1. Buffers must be 32-bit aligned (4-byte boundary)
     * 2. Buffers must be in physical memory within first 4GB (RTL8139 is 32-bit PCI)
     * 3. Buffers must remain valid until DMA completes (never freed while in use)
     */
    for (i = 0; i < 4; i++)
    {
        /* Allocate TX buffer - kmalloc should return aligned memory, but verify */
        rtl8139_tx_buffers[i] = (uint8_t *)kmalloc(RTL8139_MAX_TX_SIZE);
        if (!rtl8139_tx_buffers[i])
        {
            LOG_ERROR("RTL8139", "Failed to allocate TX buffer");
            goto init_fail;
        }
        
        /* Verify alignment (must be 32-bit aligned) */
        uintptr_t addr = (uintptr_t)rtl8139_tx_buffers[i];
        if (addr % 4 != 0)
        {
            LOG_ERROR_FMT("RTL8139", "TX buffer %d not 32-bit aligned: %p", i, rtl8139_tx_buffers[i]);
            goto init_fail;
        }
        
        /* Verify physical address is within 32-bit range (first 4GB) */
        uint32_t phys_addr = KVA_TO_PHYS(rtl8139_tx_buffers[i]);
        if (phys_addr > 0xFFFFFFFF)
        {
            LOG_ERROR_FMT("RTL8139", "TX buffer %d physical address > 4GB: 0x%x", i, phys_addr);
            goto init_fail;
        }
        
        /* Register TX buffer PHYSICAL address with hardware */
        /* Hardware uses this address for DMA - must be physical, not virtual */
        outl(rtl8139_io_base + RTL8139_REG_TSAD0 + (i * 4), phys_addr);
        
        /* Host-owns (OWN=1): ready for next TX. Matches QEMU TxHostOwns. */
        outl(rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4), RTL8139_TSD_OWN);
        
        uint32_t tsd_init = inl(rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4));
        if (!(tsd_init & RTL8139_TSD_OWN))
        {
            LOG_WARNING_FMT("RTL8139", "TX descriptor %d missing host-owns after init: 0x%x", i, tsd_init);
        }
        rtl8139_tx_descriptor_own_state[i] = 1;
        
        LOG_DEBUG_FMT("RTL8139", "TX buffer %d: virt=%p, phys=0x%x, size=%d, TSD=0x%x", 
                     i, rtl8139_tx_buffers[i], phys_addr, RTL8139_MAX_TX_SIZE, tsd_init);
    }

    /* Configure interrupts */
    outw(rtl8139_io_base + RTL8139_REG_IMR, RTL8139_INT_ROK | RTL8139_INT_TOK);

    /* Configure RX: Accept All Packets (debug) + Broadcast + Multicast + My Physical, Wrap RX buffer */
    /* AAP is CRITICAL for debugging - without it, many responses never enter */
    outl(rtl8139_io_base + RTL8139_REG_RCR, 
         RTL8139_RCR_AAP |  /* Accept all packets (debug) */
         RTL8139_RCR_APM |  /* Accept physical match */
         RTL8139_RCR_AB  |  /* Accept broadcast */
         RTL8139_RCR_AM  |  /* Accept multicast */
         RTL8139_RCR_WRAP); /* Wrap around */

    /* Enable TX and RX */
    outb(rtl8139_io_base + RTL8139_REG_CR, RTL8139_CR_TE | RTL8139_CR_RE);

    /* Read PCI Interrupt Line to get actual IRQ */
    uint32_t pci_int_line = pci_config_read(bus, slot, 0, PCI_REG_INTERRUPT_LINE);
    uint8_t irq = (uint8_t)(pci_int_line & 0xFF);
    rtl8139_irq_line = (int)irq;
    LOG_INFO_FMT("RTL8139", "PCI Interrupt Line (IRQ): %d", (int)irq);
    resource_register_irq(irq, "rtl8139");
    resource_register_ioport(rtl8139_io_base, (uint16_t)(rtl8139_io_base + 0x40), "rtl8139");

    /*
     * MSR bit2 is LinkFail (datasheet / QEMU): 0 = link OK, 1 = down.
     * QEMU user-net keeps the link up; inverted polarity left eth0 without
     * IFF_UP so plain `ifconfig` (no -a) printed nothing.
     */
    uint8_t msr = inb(rtl8139_io_base + RTL8139_REG_MSR);
    bool link_ok = (msr & RTL8139_MSR_LINKB) == 0;

    /* Register with network stack */
    memset(&rtl8139_dev, 0, sizeof(rtl8139_dev));
    rtl8139_dev.name = "eth0";
    memcpy(rtl8139_dev.mac, rtl8139_mac, 6);
    rtl8139_dev.mtu = 1500; /* Standard Ethernet MTU */
    rtl8139_dev.flags = IFF_BROADCAST;
    if (link_ok)
    {
        rtl8139_dev.flags |= IFF_UP | IFF_RUNNING;
        LOG_INFO("RTL8139", "Link is UP");
    }
    else
    {
        LOG_WARNING("RTL8139", "Link is DOWN");
    }
    
    rtl8139_dev.send = rtl8139_netdev_send;
    rtl8139_dev.poll = rtl8139_netdev_poll;
    rtl8139_dev.get_irq_line = rtl8139_netdev_get_irq_line;
    rtl8139_dev.handle_irq = rtl8139_netdev_handle_irq;
    rtl8139_dev.get_stats = rtl8139_netdev_get_stats;
    rtl8139_dev.get_byte_stats = rtl8139_netdev_get_byte_stats;

    net_register_device(&rtl8139_dev);

    LOG_INFO("RTL8139", "Initialization successful.");
    return 0;

init_fail:
    for (int j = 0; j < 4; j++)
    {
        if (rtl8139_tx_buffers[j])
        {
            kfree(rtl8139_tx_buffers[j]);
            rtl8139_tx_buffers[j] = NULL;
        }
    }
    if (rtl8139_rx_buffer)
    {
        kfree(rtl8139_rx_buffer);
        rtl8139_rx_buffer = NULL;
    }
    rtl8139_io_base = 0;
    rtl8139_irq_line = -1;
    return -1;
}

static int rtl8139_find_free_tx_descriptor(void)
{
    /*
     * QEMU C-mode (and Realtek ring) only advances currTxDesc on a successful
     * TX of that slot. Always use our matching cursor — not the first free
     * index — or writes land on the wrong TSD and the frame never leaves.
     */
    int i = (int)(rtl8139_current_tx_descriptor % 4);
    uint32_t tsd_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4);
    uint32_t tsd = inl(tsd_addr);

    /* Free when host owns (OWN set). */
    if (tsd & RTL8139_TSD_OWN)
        return i;
    return -1;
}

static void rtl8139_force_release_busy_tx_descriptors(void)
{
    if (!rtl8139_io_base)
    {
        return;
    }

    for (int i = 0; i < 4; i++)
    {
        uint32_t reg_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4);
        uint32_t tsd = inl(reg_addr);
        /* Busy = host does not own (OWN clear). Return descriptor to host. */
        if (!(tsd & RTL8139_TSD_OWN))
        {
            outl((uint16_t)reg_addr, RTL8139_TSD_OWN);
            rtl8139_tx_descriptor_own_state[i] = 1;
            rtl8139_tx_desc_start_tick[i] = 0;
            rtl8139_tx_timeout_latched[i] = 0;
            if (rtl8139_tx_in_flight > 0)
            {
                rtl8139_tx_in_flight--;
            }
            rtl8139_counters.tx_errors++;
            LOG_DEBUG_FMT("RTL8139", "Force-released busy TX descriptor %d", i);
        }
    }
}

static void rtl8139_advance_rx_read_offset(uint16_t raw_length)
{
    rtl8139_rx_read_offset = (rtl8139_rx_read_offset + raw_length + 4 + 3) & ~3;
    rtl8139_rx_read_offset %= RTL8139_RX_BUF_SIZE;
}

/**
 * rtl8139_send - Send data through RTL8139 hardware
 * @data: Pointer to data buffer to send
 * @len: Length of data to send
 *
 * REIMPLEMENTED: Simple, clean version focused on core functionality.
 * This function copies data to a dedicated TX buffer and initiates DMA.
 *
 * Return: 0 on success, -1 if parameters are invalid, all TX descriptors are
 * busy, or the TX buffer for the chosen descriptor is missing.
 */
int rtl8139_send(void *data, size_t len)
{
    /* Basic parameter validation */
    if (!data || len == 0 || len > RTL8139_MAX_TX_SIZE || !rtl8139_io_base)
    {
        return -1;
    }

    /* Reap TX completions before searching for a free descriptor. */
    rtl8139_check_tx_completion();

    /* Find a free TX descriptor */
    int desc = rtl8139_find_free_tx_descriptor();

    if (desc == -1)
    {
        /*
         * Descriptor ring may be temporarily full. Re-check completions with
         * a longer settle window before forcing TX recovery.
         */
        for (int attempt = 0; attempt < 48 && desc == -1; attempt++)
        {
            io_wait();
            rtl8139_check_tx_completion();
            desc = rtl8139_find_free_tx_descriptor();
        }
    }

    if (desc == -1)
    {
        /*
         * Descriptors still busy after normal completion check:
         * perform one recovery pass (rate-limited) and retry.
         */
        if (!rtl8139_recover_tx_path())
        {
            /*
             * Recover suppressed by rate limit — still drop stalled OWN so a
             * free descriptor appears without re-arming the whole ring.
             */
            rtl8139_force_release_busy_tx_descriptors();
        }

        /*
         * Some devices need a short settle window after re-arming TX. Retry a
         * few reads before failing the send.
         */
        for (int attempt = 0; attempt < 64 && desc == -1; attempt++)
        {
            io_wait();
            rtl8139_check_tx_completion();
            desc = rtl8139_find_free_tx_descriptor();
        }

        if (desc == -1)
        {
            /* Last clear of OWN before giving up this send attempt. */
            rtl8139_force_release_busy_tx_descriptors();
            for (int attempt = 0; attempt < 16 && desc == -1; attempt++)
            {
                io_wait();
                desc = rtl8139_find_free_tx_descriptor();
            }
        }

        if (desc == -1)
        {
            LOG_DEBUG("RTL8139", "TX busy: descriptor ring still saturated after recovery window");
            return -1;
        }
    }

    /* Get TX buffer for this descriptor */
    void *tx_buf = rtl8139_tx_buffers[desc];
    if (!tx_buf)
    {
        return -1;
    }

    /* Copy data to TX buffer */
    memcpy(tx_buf, data, len);

    /* Memory barrier to ensure copy completes */
    smp_mb();

    /* Write TSD to start DMA */
    uint32_t tsd_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (desc * 4);
    uint32_t tsd_value = (uint32_t)len & RTL8139_TSD_SIZE_MASK;

#if KERNEL_DEBUG
    klog_debug_fmt("RTL8139",
                   "TX: desc=%d len=%d tsd_reg=0x%x tsd_val=0x%x",
                   desc, (int)len, (unsigned)tsd_addr, (unsigned)tsd_value);
#endif

    /* Execute outl - this starts DMA */
    outl((uint16_t)tsd_addr, tsd_value);

    /* Memory barrier after I/O */
    smp_mb();

    /* Update tracking: host released descriptor (OWN cleared by size write). */
    rtl8139_tx_pending_len[desc] = (uint32_t)len;
    rtl8139_tx_descriptor_own_state[desc] = 0;
    rtl8139_tx_in_flight++;
    rtl8139_current_tx_descriptor = (desc + 1) % 4;
    rtl8139_tx_desc_start_tick[desc] = clock_get_tick_count();
    rtl8139_tx_timeout_latched[desc] = 0;
    
#if KERNEL_DEBUG
    klog_debug_fmt("RTL8139", "TX: DMA started, in_flight=%d",
                   (int)rtl8139_tx_in_flight);
#endif

    return 0;
}

/**
 * rtl8139_check_tx_completion - Reap finished TX descriptors
 *
 * Host-owns bit (OWN): cleared by driver to start DMA; set by hardware when
 * DMA to FIFO completes (QEMU TxHostOwns / Realtek). Completion is 0→1.
 */
static void rtl8139_check_tx_completion(void)
{
    if (!rtl8139_io_base)
        return;
    
    for (int i = 0; i < 4; i++)
    {
        uint32_t reg_addr = rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4);
        
        smp_mb();
        uint32_t tsd = inl(reg_addr);
        smp_mb();
        
        uint32_t own = tsd & RTL8139_TSD_OWN;
        uint32_t prev_own = rtl8139_tx_descriptor_own_state[i];

        /*
         * In-flight descriptors have OWN clear. Stall → return to host-owns.
         */
        if (!own && rtl8139_tx_desc_start_tick[i] != 0) {
            uint32_t hz = clock_get_timer_frequency();
            if (hz != 0) {
                uint64_t now = clock_get_tick_count();
                uint64_t elapsed = now - rtl8139_tx_desc_start_tick[i];
                if (elapsed > (uint64_t)hz &&
                    !rtl8139_tx_timeout_latched[i]) {
                    rtl8139_counters.tx_errors++;
                    rtl8139_tx_timeout_latched[i] = 1;
                    LOG_WARNING_FMT("RTL8139", "TX descriptor %d stalled >1s (timeout)", i);

                    outl(rtl8139_io_base + RTL8139_REG_TSD0 + (i * 4), RTL8139_TSD_OWN);
                    rtl8139_tx_descriptor_own_state[i] = 1;
                    rtl8139_tx_desc_start_tick[i] = 0;
                    rtl8139_tx_timeout_latched[i] = 0;
                    /* Drop stale length so a later OWN edge cannot inflate tx_bytes. */
                    rtl8139_tx_pending_len[i] = 0;
                    if (rtl8139_tx_in_flight > 0)
                        rtl8139_tx_in_flight--;
                    LOG_DEBUG_FMT("RTL8139", "TX descriptor %d force-released after timeout", i);
                    continue;
                }
            }
        }

        rtl8139_tx_descriptor_own_state[i] = own ? 1 : 0;
        
        /* DMA finished: OWN went from clear → set (host owns again). */
        if (prev_own == 0 && own != 0)
        {
            rtl8139_tx_desc_start_tick[i] = 0;
            rtl8139_tx_timeout_latched[i] = 0;
            if (tsd & RTL8139_TSD_TOK)
            {
                rtl8139_counters.tx_packets++;
                rtl8139_counters.tx_bytes += rtl8139_tx_pending_len[i];
            }
            else
                rtl8139_counters.tx_errors++;
            rtl8139_tx_pending_len[i] = 0;

            if (rtl8139_tx_in_flight > 0)
            {
                rtl8139_tx_in_flight--;
                LOG_DEBUG_FMT("RTL8139", "TX descriptor %d completed, in-flight=%d", 
                             i, rtl8139_tx_in_flight);
            }
            else
            {
                LOG_WARNING_FMT("RTL8139", "TX descriptor %d completed but in-flight counter already 0", i);
            }
        }
    }
}

/**
 * rtl8139_netdev_send - Network device send wrapper
 * @dev: Network device structure (unused, kept for API compatibility)
 * @data: Pointer to data buffer to send
 * @len: Length of data to send
 * 
 * This wrapper adapts the net_device API to the rtl8139_send() function.
 * It provides the interface expected by the network stack.
 */
static int rtl8139_netdev_send(struct net_device *dev, void *data, size_t len)
{
    
#if KERNEL_DEBUG
    klog_debug_fmt("RTL8139",
                   "netdev_send ENTRY: dev=0x%x data=0x%x len=%d (0x%x)",
                   (unsigned)(uintptr_t)dev, (unsigned)(uintptr_t)data,
                   (int)len, (unsigned)len);
#endif
    
    (void)dev;

    /* CRITICAL: Validate len IMMEDIATELY - if corrupted, abort */
    if (len > RTL8139_MAX_TX_SIZE || len == 0 || len > 2000)
    {
        LOG_ERROR_FMT("RTL8139", "netdev_send: CORRUPTION! len=%d (0x%x) is invalid! MAX=%d", 
                     (int)len, (unsigned int)len, RTL8139_MAX_TX_SIZE);
#if KERNEL_DEBUG
        klog_info("RTL8139", "ERROR: Invalid len parameter!");
#endif
        return -1;
    }
    
    if (!data)
    {
        LOG_ERROR("RTL8139", "netdev_send: data is NULL");
        return -1;
    }

    LOG_DEBUG("RTL8139", "netdev_send: calling rtl8139_send");

    /*
     * TX descriptor ownership can transiently lag behind completion after
     * ring recovery. Retry a few times before surfacing a hard send failure.
     */
    for (int attempt = 0; attempt < 8; attempt++)
    {
        if (rtl8139_send(data, len) == 0)
        {
            if (attempt > 0)
            {
                LOG_DEBUG_FMT("RTL8139", "netdev_send: TX completed after retry #%d", attempt);
            }
            return 0;
        }

        io_wait();
        rtl8139_check_tx_completion();
        if (attempt == 2)
        {
            rtl8139_recover_tx_path();
        }

        LOG_DEBUG_FMT("RTL8139", "netdev_send: retrying TX after busy path (attempt=%d)", attempt + 1);
    }

    /*
     * Last-resort path for transient OWN wedging: release busy descriptors
     * then one recover (rate-limited), settle, and a final send attempt.
     */
    rtl8139_force_release_busy_tx_descriptors();
    (void)rtl8139_recover_tx_path();
    for (int settle = 0; settle < 32; settle++)
    {
        io_wait();
        rtl8139_check_tx_completion();
    }
    if (rtl8139_send(data, len) == 0)
    {
        LOG_DEBUG("RTL8139", "netdev_send: TX succeeded after forced descriptor release");
        return 0;
    }

    LOG_DEBUG("RTL8139", "netdev_send: rtl8139_send failed after retries");
    return -1;
}

static void rtl8139_netdev_poll(struct net_device *dev)
{
    (void)dev;
    rtl8139_poll();
}

static int rtl8139_netdev_get_irq_line(struct net_device *dev)
{
    (void)dev;
    return rtl8139_get_irq_line();
}

static int rtl8139_netdev_handle_irq(struct net_device *dev, uint8_t irq)
{
    (void)dev;
    int line = rtl8139_get_irq_line();
    if (line < 0 || line >= 16 || line != (int)irq)
        return 0;
    rtl8139_handle_interrupt();
    return 1;
}

static void rtl8139_netdev_get_stats(struct net_device *dev, uint64_t *rx_pkts, uint64_t *tx_pkts,
                                     uint64_t *rx_errs, uint64_t *tx_errs)
{
    (void)dev;
    rtl8139_get_stats(rx_pkts, tx_pkts, rx_errs, tx_errs);
}

static void rtl8139_netdev_get_byte_stats(struct net_device *dev, uint64_t *rx_bytes,
					  uint64_t *tx_bytes)
{
    (void)dev;
    if (rx_bytes)
        *rx_bytes = rtl8139_counters.rx_bytes;
    if (tx_bytes)
        *tx_bytes = rtl8139_counters.tx_bytes;
}

/*
 * Read 16-bit little-endian from the RX ring at byte offset off (wraps at
 * RTL8139_RX_BUF_SIZE).
 */
static uint16_t rtl8139_rx_ring_u16(uint16_t off)
{
    uint16_t mask = (uint16_t)(RTL8139_RX_BUF_SIZE - 1);
    uint8_t *b = rtl8139_rx_buffer;
    uint16_t o0 = (uint16_t)(off & mask);
    uint16_t o1 = (uint16_t)((o0 + 1) & mask);

    return (uint16_t)b[o0] | ((uint16_t)b[o1] << 8);
}

/*
 * Copy nbytes from the RX ring starting at byte offset ring_start (wraps).
 */
static void rtl8139_rx_ring_copy(uint16_t ring_start, uint16_t nbytes, uint8_t *dst)
{
    uint16_t mask = (uint16_t)(RTL8139_RX_BUF_SIZE - 1);
    uint16_t pos = (uint16_t)(ring_start & mask);

    while (nbytes != 0)
    {
        uint16_t room = (uint16_t)(RTL8139_RX_BUF_SIZE - pos);
        uint16_t chunk = nbytes < room ? nbytes : room;

        memcpy(dst, rtl8139_rx_buffer + pos, chunk);
        dst += chunk;
        nbytes = (uint16_t)(nbytes - chunk);
        pos = (uint16_t)((pos + chunk) & mask);
    }
}

/**
 * rtl8139_process_rx_packets - Process received packets from RX buffer
 * 
 * This is the core packet processing logic, extracted so it can be called
 * both from interrupt handler and from polling function.
 */
static void rtl8139_process_rx_packets(void)
{
    if (!rtl8139_io_base || !rtl8139_rx_buffer)
    {
        return;
    }

    /*
     * Hardware write cursor is CBR (I/O 0x3A), not rx_buffer+0x10.
     * Reading the ring as a write ptr desyncs CAPR under load → RxOverflow
     * and RX stays wedged (TX still works; ping/wget die after stress).
     * Spec: Realtek RTL8139 programming guide / datasheet CBR/CAPR.
     */
    uint16_t current_write = inw(rtl8139_io_base + RTL8139_REG_CBR);
    current_write &= (RTL8139_RX_BUF_SIZE - 1);
    
    /* Handle wrap-around: if current_write < read_offset, we wrapped */
    if (current_write < rtl8139_rx_read_offset)
    {
        /* Buffer wrapped around, process from read_offset to end, then from start to write */
        LOG_DEBUG("RTL8139", "RX buffer wrapped");
    }
    
    /* If write pointer hasn't advanced, there are no new packets */
    if (current_write == rtl8139_rx_read_offset)
    {
        return;
    }
    
    LOG_DEBUG_FMT("RTL8139", "RX buffer: read_offset=%d, write_offset=%d", 
                 rtl8139_rx_read_offset, current_write);
    
    int packet_count = 0;
    /* Process packets from our read offset to current write pointer */
    /* Safety limit: process at most 64 packets per call to prevent infinite loops */
    int max_packets = 64;
    while (rtl8139_rx_read_offset != current_write && max_packets-- > 0)
    {
        /* Safety check: ensure read_offset is within buffer bounds */
        if (rtl8139_rx_read_offset >= RTL8139_RX_BUF_SIZE)
        {
            LOG_ERROR_FMT("RTL8139", "RX read_offset out of bounds: %d >= %d", 
                         rtl8139_rx_read_offset, RTL8139_RX_BUF_SIZE);
            /* Reset to safe position */
            rtl8139_rx_read_offset = current_write & (RTL8139_RX_BUF_SIZE - 1);
            break;
        }
        
        /* Read packet header (4 bytes: status, length), header may wrap */
        uint16_t ro = rtl8139_rx_read_offset;
        uint16_t status = rtl8139_rx_ring_u16(ro);
        uint16_t length = rtl8139_rx_ring_u16((uint16_t)(ro + 2));
        
        /* CRITICAL: Check OWN bit (bit 0 of status). 
         * If not set, no valid packet at this offset - stop processing.
         * This prevents reading garbage from the ring buffer.
         * 
         * Also check that status is reasonable (not corrupted data).
         * Valid status should have bit 0 set (OWN) and reasonable upper bits.
         */
        /* CRITICAL: Log raw RX header BEFORE any processing for debugging */
#if KERNEL_DEBUG
        klog_debug_fmt("RTL8139",
                       "RX raw: status=0x%x length=%u offset=%u",
                       (unsigned)status, (unsigned)length,
                       (unsigned)rtl8139_rx_read_offset);
#endif
        
        /* CRITICAL: In RTL8139, the 'length' field is the size of:
         *   - Ethernet frame
         *   - CRC (4 bytes)
         * It does NOT include the 4-byte header (status + length) we just read.
         * To get the Ethernet frame size (without CRC), we subtract 4 bytes.
         * If length < 4, the packet is invalid (must at least have CRC).
         */
        /*
         * A zeroed header usually means the NIC has not finished publishing the
         * next frame yet. Do not consume or resync here; leave CAPR on the last
         * fully consumed frame and retry on next poll/IRQ.
         */
        if (status == 0x0000 && length == 0)
        {
            break;
        }

        if (length < 4)
        {
            rtl8139_counters.rx_errors++;
            LOG_WARNING_FMT("RTL8139", "Invalid RX length (too small): %d (must be >= 4), discarding packet", length);
            /* Invalid length cannot be advanced safely: resync to current writer. */
            rtl8139_rx_read_offset = current_write;
            break;
        }

        if (length > (RTL8139_RX_BUF_SIZE - 4))
        {
            rtl8139_counters.rx_errors++;
            LOG_WARNING_FMT("RTL8139", "Invalid RX length (too large): %d, resyncing RX ring", length);
            rtl8139_rx_read_offset = current_write;
            break;
        }

        if (!(status & RTL8139_RX_STAT_ROK))
        {
            rtl8139_counters.rx_errors++;
            LOG_WARNING_FMT("RTL8139", "RX status error 0x%04x, skipping frame len=%u", status, (unsigned)length);
            rtl8139_advance_rx_read_offset(length);
            continue;
        }
        
        /* Calculate Ethernet frame length (subtract 4-byte CRC).
         * 'length' includes frame + CRC, so we remove CRC to get frame size.
         */
        uint16_t packet_len = length - 4;
        
        /* Check if packet length is valid */
        if (packet_len == 0 || packet_len > RTL8139_RX_BUF_SIZE)
        {
            rtl8139_counters.rx_errors++;
            LOG_WARNING_FMT("RTL8139", "Invalid packet length after header subtraction: raw_len=%d, pkt_len=%d, discarding", 
                           length, packet_len);
            rtl8139_advance_rx_read_offset(length);
            continue;
        }
        
        /* Payload (frame + CRC) starts after 4-byte header; may wrap in ring */
        uint32_t payload_start = (uint32_t)rtl8139_rx_read_offset + 4U;
        uint32_t payload_end = payload_start + (uint32_t)length;
        int payload_wraps = payload_end > (uint32_t)RTL8139_RX_BUF_SIZE;

        /* Check if packet is OK */
        if (status & RTL8139_RX_STAT_ROK)
        {
            LOG_DEBUG_FMT("RTL8139", "Packet #%d: status=0x%04x, raw_length=%d, packet_len=%d",
                         packet_count, status, length, packet_len);
            
            if (packet_len > 0 && packet_len <= 1518) /* Valid Ethernet frame size */
            {
                void *packet_data;
                uint8_t merge_stack[2048];
                uint8_t *merge_heap = NULL;
                uint8_t *merge_buf = NULL;

                if (payload_wraps)
                {
                    /*
                     * Frame spans end of RX buffer: assemble contiguous copy
                     * then hand length-4 bytes (Ethernet without CRC) to stack.
                     */
                    if (length > (uint16_t)sizeof(merge_stack))
                    {
                        merge_heap = (uint8_t *)kmalloc(length);
                        if (!merge_heap)
                        {
                            LOG_WARNING_FMT("RTL8139", "RX wrap: kmalloc(%u) failed, skipping packet", (unsigned int)length);
                            merge_buf = NULL;
                        }
                        else
                        {
                            merge_buf = merge_heap;
                        }
                    }
                    else
                    {
                        merge_buf = merge_stack;
                    }

                    if (merge_buf)
                    {
                        rtl8139_rx_ring_copy((uint16_t)payload_start, length, merge_buf);
                        packet_data = merge_buf;
                    }
                    else
                    {
                        packet_data = NULL;
                    }
                }
                else
                {
                    packet_data = rtl8139_rx_buffer + rtl8139_rx_read_offset + 4;
                }

                if (packet_data)
                {
#if KERNEL_DEBUG
                    if (packet_len >= 14)
                    {
                        struct eth_header *eth = (struct eth_header *)packet_data;
                        uint16_t type = ntohs(eth->type);

                        klog_debug_fmt("RTL8139", "RX len=%d type=0x%x",
                                       (int)packet_len, (unsigned)type);
                    }
                    else
                    {
                        klog_debug_fmt("RTL8139", "RX len=%d", (int)packet_len);
                    }
#endif
                    
                    /* Pass Ethernet frame without trailing CRC */
                    net_receive(&rtl8139_dev, packet_data, packet_len);
                    rtl8139_counters.rx_packets++;
                    rtl8139_counters.rx_bytes += packet_len;
                    packet_count++;
                }

                if (merge_heap)
                    kfree(merge_heap);
            }
            else
            {
                rtl8139_counters.rx_errors++;
                LOG_WARNING_FMT("RTL8139", "Invalid packet length: %d", packet_len);
            }
        }
        /* CRITICAL: Move to next packet with correct alignment.
         * In RTL8139, the 'length' field is:
         *   - Ethernet frame + CRC (4 bytes)
         *   It does NOT include the 4-byte header (status + length).
         * 
         * Formula: rx_offset = (rx_offset + length + 4 + 3) & ~3
         *   - length = frame + CRC (e.g., 68 bytes for ARP: 64 frame + 4 CRC)
         *   - +4 = 4-byte header we already read (status + length)
         *   - (+ 3) & ~3 = round to 4-byte boundary
         */
        rtl8139_advance_rx_read_offset(length);
    }
    
    /* Update CAPR to acknowledge processed packets
     * CAPR indicates where we've read up to (minus 0x10 for hardware safety margin)
     */
    outw(rtl8139_io_base + RTL8139_REG_CAPR, (rtl8139_rx_read_offset - 0x10) & (RTL8139_RX_BUF_SIZE - 1));
    if (packet_count > 0)
    {
        /* Only log if we actually processed packets */
        LOG_DEBUG_FMT("RTL8139", "Processed %d packet(s)", packet_count);
    }
}

/**
 * rtl8139_handle_interrupt - Handle RTL8139 interrupt
 * 
 * This function processes received packets from the RTL8139 RX buffer.
 * It reads the interrupt status register, processes packets, and passes
 * them to the network stack via net_receive().
 */
void rtl8139_handle_interrupt(void)
{
    if (!rtl8139_io_base || !rtl8139_rx_buffer)
    {
        LOG_WARNING("RTL8139", "Interrupt handler called but device not initialized");
        return;
    }

    /* Read interrupt status register */
    uint16_t isr = inw(rtl8139_io_base + RTL8139_REG_ISR);
    
#if KERNEL_DEBUG
    klog_debug_fmt("RTL8139", "Interrupt handler called: ISR=0x%x",
                   (unsigned)isr);
#endif
    
    /*
     * ROK and RxBufOvw both need drain + CAPR update. Realtek: on overflow,
     * update CAPR first then clear ISR(ROK); otherwise RX DMA stays stopped.
     */
    if (isr & (RTL8139_INT_ROK | RTL8139_INT_RXOVW | RTL8139_INT_FIFOOVW))
    {
        if (isr & (RTL8139_INT_RXOVW | RTL8139_INT_FIFOOVW))
        {
            rtl8139_counters.rx_errors++;
            LOG_WARNING_FMT("RTL8139", "RX overflow ISR=0x%x — draining ring",
                            (unsigned)isr);
        }
        if (rtl8139_try_enter_rx())
        {
            rtl8139_process_rx_packets();
            rtl8139_leave_rx();
        }
    }

    if (isr & RTL8139_INT_RER)
    {
        rtl8139_counters.rx_errors++;
        LOG_DEBUG("RTL8139", "RX error interrupt");
    }
    if (isr & RTL8139_INT_TER)
    {
        rtl8139_counters.tx_errors++;
        LOG_DEBUG("RTL8139", "TX error interrupt");
    }
    if (isr & RTL8139_INT_PUN)
        LOG_DEBUG("RTL8139", "Packet underrun interrupt");
    
    /* Check for transmit interrupt (optional, for TX completion) */
    if (isr & RTL8139_INT_TOK)
    {
        LOG_DEBUG("RTL8139", "TX interrupt: transmission completed");
        /* CRITICAL: Check TX completion and update in-flight counter */
        rtl8139_check_tx_completion();
    }
    
    /* CRITICAL: Always check TX completion, even if no TX interrupt */
    /* Hardware might complete TX without generating interrupt */
    rtl8139_check_tx_completion();
    
    /* CRITICAL: Clear interrupt status by writing back ISR
     * This must be done AFTER processing to ensure we don't miss interrupts
     */
    outw(rtl8139_io_base + RTL8139_REG_ISR, isr);
    
    /* Verify ISR was cleared (for debugging) */
    uint16_t isr_after = inw(rtl8139_io_base + RTL8139_REG_ISR);
    if ((isr_after & RTL8139_INT_ROK) && (isr & RTL8139_INT_ROK))
    {
        /* ISR still set after clearing - this shouldn't happen but log it */
        LOG_WARNING_FMT("RTL8139", "ISR still set after clearing (was=0x%04x, now=0x%04x)", isr, isr_after);
    }
}

/**
 * rtl8139_poll - Poll for received packets (fallback when interrupts don't work)
 * 
 * This function can be called periodically to check for received packets
 * without relying on interrupts. Useful for debugging or when interrupts
 * are not working properly.
 */
void rtl8139_poll(void)
{
    if (!rtl8139_io_base || !rtl8139_rx_buffer)
    {
        return;
    }

    /* Check interrupt status register to see if there are packets */
    uint16_t isr = inw(rtl8139_io_base + RTL8139_REG_ISR);
    
    /* Debug: Log polling activity only in debug builds. */
#if KERNEL_DEBUG
    static int poll_count = 0;
    poll_count++;
    if ((poll_count % 500) == 0)
    {
        uint16_t current_write = inw(rtl8139_io_base + RTL8139_REG_CBR);

        current_write &= (RTL8139_RX_BUF_SIZE - 1);
        klog_debug_fmt("RTL8139",
                       "Poll #%d ISR=0x%x write=%u read=%u",
                       poll_count, (unsigned)isr, (unsigned)current_write,
                       (unsigned)rtl8139_rx_read_offset);
    }
#endif
    
    if (isr & (RTL8139_INT_ROK | RTL8139_INT_RXOVW | RTL8139_INT_FIFOOVW))
    {
        if (rtl8139_try_enter_rx())
        {
            rtl8139_process_rx_packets();
            rtl8139_leave_rx();
            outw(rtl8139_io_base + RTL8139_REG_ISR,
                 (uint16_t)(isr & (RTL8139_INT_ROK | RTL8139_INT_RXOVW |
                                   RTL8139_INT_FIFOOVW)));
        }
    }
    else
    {
        /* Even if ISR doesn't show ROK, check the buffer directly */
        if (rtl8139_try_enter_rx())
        {
            rtl8139_process_rx_packets();
            rtl8139_leave_rx();
        }
    }
    
    /* CRITICAL: Check TX completion during polling */
    rtl8139_check_tx_completion();
}

int rtl8139_get_irq_line(void)
{
    return rtl8139_irq_line;
}

void rtl8139_get_stats(uint64_t *rx_pkts, uint64_t *tx_pkts,
                        uint64_t *rx_errs, uint64_t *tx_errs)
{
    if (!rx_pkts || !tx_pkts || !rx_errs || !tx_errs)
        return;
    if (!rtl8139_io_base) {
        *rx_pkts = 0;
        *tx_pkts = 0;
        *rx_errs = 0;
        *tx_errs = 0;
        return;
    }
    *rx_pkts = rtl8139_counters.rx_packets;
    *tx_pkts = rtl8139_counters.tx_packets;
    *rx_errs = rtl8139_counters.rx_errors;
    *tx_errs = rtl8139_counters.tx_errors;
}
