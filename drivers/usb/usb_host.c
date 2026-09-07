/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: usb_host.c
 * Description: USB host PCI discovery (multifunction scan); host-controller bring-up TBD.
 */

#include <config.h>
#include <ir0/usb_host.h>
#include <ir0/logging.h>
#include <ir0/arch_io.h>
#include <ir0/cpu.h>
#include <ir0/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if CONFIG_ENABLE_USB_HOST

#define PCI_CONFIG_ADDRESS_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT    0xCFC
#define PCI_ENABLE_BIT          0x80000000

#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_SUBCLASS_USB        0x03

#define PCI_OFFSET_VENDOR_DEVICE  0x00
#define PCI_OFFSET_CLASS_REV      0x08
#define PCI_OFFSET_BAR0           0x10

#define USB_HOST_MAX_HC 24

typedef struct usb_hc_info
{
    uint16_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t prog_if;
    uint32_t bar0;
} usb_hc_info_t;

static int g_usb_controller_count;

static usb_hc_info_t g_usb_hcs[USB_HOST_MAX_HC];

static uint32_t usb_pci_config_read(uint8_t bus, uint8_t slot, uint8_t func,
                                    uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) |
                                  (offset & 0xfc) | ((uint32_t)PCI_ENABLE_BIT));

    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static int usb_host_record_controller(uint16_t bus, uint8_t slot, uint8_t func,
                                       uint16_t vendor, uint16_t device,
                                       uint32_t class_rev_dw, uint32_t bar0)
{
    usb_hc_info_t *slotp;

    if (g_usb_controller_count >= USB_HOST_MAX_HC)
        return 0;

    slotp = &g_usb_hcs[g_usb_controller_count];
    slotp->bus = bus;
    slotp->slot = slot;
    slotp->func = func;
    slotp->vendor = vendor;
    slotp->device = device;
    slotp->prog_if = (uint8_t)((class_rev_dw >> 8) & 0xFFU);
    slotp->bar0 = bar0;

    LOG_INFO_FMT("USB",
                 "PCI USB HC %04x:%04x %u:%u.%u prog_if=0x%02x BAR0=0x%08x",
                 (unsigned)vendor, (unsigned)device,
                 (unsigned)bus, (unsigned)slot, (unsigned)func,
                 (unsigned)slotp->prog_if, (unsigned)bar0);
    g_usb_controller_count++;
    return 1;
}

static int usb_host_pci_scan(void)
{
    g_usb_controller_count = 0;
    memset(g_usb_hcs, 0, sizeof(g_usb_hcs));

    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t slot = 0; slot < 32; slot++)
        {
            for (uint8_t func = 0; func < 8; func++)
            {
                uint32_t id;
                uint32_t class_rev;
                uint8_t pci_class;
                uint8_t pci_subclass;
                uint32_t bar0;
                uint16_t vendor;
                uint16_t device;

                id = usb_pci_config_read((uint8_t)bus, slot, func,
                                          PCI_OFFSET_VENDOR_DEVICE);
                if (id == 0xFFFFFFFFU || id == 0)
                    continue;

                class_rev = usb_pci_config_read((uint8_t)bus, slot, func,
                                                PCI_OFFSET_CLASS_REV);
                pci_class = (uint8_t)((class_rev >> 24) & 0xFFU);
                pci_subclass = (uint8_t)((class_rev >> 16) & 0xFFU);

                if (pci_class != PCI_CLASS_SERIAL_BUS ||
                    pci_subclass != PCI_SUBCLASS_USB)
                    continue;

                bar0 = usb_pci_config_read((uint8_t)bus, slot, func,
                                           PCI_OFFSET_BAR0);
                vendor = (uint16_t)(id & 0xFFFFU);
                device = (uint16_t)((id >> 16) & 0xFFFFU);
                (void)usb_host_record_controller(bus, slot, func, vendor, device,
                                                 class_rev, bar0);
            }
        }
    }

    return g_usb_controller_count;
}

int ir0_usb_host_init(void)
{
    int count = usb_host_pci_scan();

    LOG_INFO_FMT("USB", "Host scaffold init: %d controller(s) on PCI bus", count);
    return 0;
}

int ir0_usb_host_controller_count(void)
{
    return g_usb_controller_count;
}

int ir0_usb_host_describe(int idx, char *buf, size_t len)
{
    const usb_hc_info_t *hc;

    if (!buf || len == 0)
        return -EINVAL;

    if (idx < 0 || idx >= g_usb_controller_count)
        return -ENODEV;

    hc = &g_usb_hcs[idx];
    {
        int n;

        n = snprintf(buf, len,
                     "%u:%u.%u %04x:%04x prog_if=0x%02x BAR0=0x%08x\n",
                     (unsigned)hc->bus, (unsigned)hc->slot,
                     (unsigned)hc->func,
                     (unsigned)hc->vendor, (unsigned)hc->device,
                     (unsigned)hc->prog_if,
                     (unsigned)hc->bar0);
        if (n < 0 || (size_t)n >= len)
            return -ENOMEM;
    }

    return 0;
}

#else

int ir0_usb_host_init(void)
{
    return 0;
}

int ir0_usb_host_controller_count(void)
{
    return 0;
}

int ir0_usb_host_describe(int idx, char *buf, size_t len)
{
    (void)idx;
    (void)buf;
    (void)len;
    return -ENODEV;
}

#endif /* CONFIG_ENABLE_USB_HOST */
