/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bt_sysfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth sysfs interface
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Exposes Bluetooth adapter and topology as files under /sys/class/bluetooth/.
 * Design: Bluetooth as observable network (adapters, neighbors, sessions as files).
 */

#include <ir0/kmem.h>
#include "bt_device.h"
#include "hci_core.h"
#include <string.h>
#include <stddef.h>
#include <ir0/errno.h>

/**
 * bt_sysfs_hci0_address_read - Read local adapter address for /sys/class/bluetooth/hci0/address
 * @buf: Output buffer
 * @count: Buffer size
 *
 * Returns: Bytes written on success, negative error on failure
 */
int bt_sysfs_hci0_address_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;

    struct hci_device *hdev = hci_get_device();
    if (!hdev)
    {
        int n = snprintf(buf, count, "00:00:00:00:00:00\n");
        return (n < 0 || (size_t)n >= count) ? (int)(count - 1) : n;
    }

    /*
     * BD_ADDR is little-endian in HCI; display as usual MSB first (colon-separated).
     */
    int n = snprintf(buf, count, "%02X:%02X:%02X:%02X:%02X:%02X\n",
                     hdev->bd_addr[5], hdev->bd_addr[4], hdev->bd_addr[3],
                     hdev->bd_addr[2], hdev->bd_addr[1], hdev->bd_addr[0]);
    if (n < 0)
        return -EINVAL;
    if ((size_t)n >= count)
        return (int)(count - 1);
    return n;
}

/**
 * bt_sysfs_hci0_state_read - Read adapter state for /sys/class/bluetooth/hci0/state
 * @buf: Output buffer
 * @count: Buffer size
 *
 * Returns: Bytes written on success, negative error on failure
 */
int bt_sysfs_hci0_state_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;

    struct hci_device *hdev = hci_get_device();
    const char *state = (hdev && hdev->initialized) ? "UP\n" : "DOWN\n";
    size_t len = strlen(state);
    if (len >= count)
        len = count - 1;
    memcpy(buf, state, len);
    buf[len] = '\0';
    return (int)len;
}

/**
 * bt_sysfs_topology_neighbors_read - Read neighbor list for /sys/class/bluetooth/topology/neighbors
 * @buf: Output buffer
 * @count: Buffer size
 *
 * One line per device: "ADDRESS RSSI NAME\n" (RSSI before NAME so NAME can contain spaces).
 * Enables tools like lsblue to show Bluetooth topology analogous to ip neigh.
 *
 * Returns: Bytes written on success, negative error on failure
 */
static int bt_sysfs_neighbors_fill(char *buf, size_t count,
                                   struct bluetooth_device *devices);

/* 8 KiB of devices; see bt_proc_devices_read for why this is not on the
 * stack. A walk of /sys reaches this reader the same way. */
int bt_sysfs_topology_neighbors_read(char *buf, size_t count)
{
    struct bluetooth_device *devices;
    int rc;

    if (!buf || count == 0)
        return -EINVAL;

    devices = kmalloc_try(sizeof(*devices) * 32);
    if (!devices)
        return -ENOMEM;

    rc = bt_sysfs_neighbors_fill(buf, count, devices);
    kfree(devices);
    return rc;
}

static int bt_sysfs_neighbors_fill(char *buf, size_t count,
                                   struct bluetooth_device *devices)
{
    hci_process_events();

    int num = hci_get_discovered_devices(devices, 32);
    if (num < 0)
        num = 0;

    size_t off = 0;
    for (int i = 0; i < num && off < count; i++)
    {
        struct bluetooth_device *d = &devices[i];
        if (!d->discovered)
            continue;

        int n = snprintf(buf + off,
                         (off < count) ? (count - off) : 0,
                         "%02X:%02X:%02X:%02X:%02X:%02X %d %s\n",
                         d->bd_addr[5], d->bd_addr[4], d->bd_addr[3],
                         d->bd_addr[2], d->bd_addr[1], d->bd_addr[0],
                         (int)d->rssi,
                         d->name[0] ? d->name : "(unknown)");
        if (n <= 0)
            break;
        if ((size_t)n >= count - off)
        {
            off = count - 1;
            break;
        }
        off += (size_t)n;
    }

    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/**
 * bt_sysfs_sessions_read - Read session list for /sys/class/bluetooth/sessions
 * @buf: Output buffer
 * @count: Buffer size
 *
 * The HCI core tracks inquiry results only: there is no ACL/L2CAP connection
 * object to enumerate, so this node cannot report a session count. It reports
 * the unsupported state instead of an empty or zeroed list, which would read as
 * "the link layer exists and has no sessions". One line per session
 * ("sess0 ADDRESS ...") once connection management lands in hci_core.
 *
 * Returns: Bytes written on success, negative error on failure
 */
int bt_sysfs_sessions_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;

    int n = snprintf(buf, count, "unsupported: no ACL/L2CAP session layer\n");
    if (n < 0)
        return -EINVAL;
    if ((size_t)n >= count)
        return (int)(count - 1);
    return n;
}
