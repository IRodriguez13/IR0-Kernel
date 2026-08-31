/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bt_device.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth Device Management Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Device management and filesystem integration for Bluetooth
 */

#include "bt_device.h"
#include "hci_core.h"
#include "hci_uart.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <string.h>
#include <ir0/errno.h>
#include <ir0/clock.h>
#include <stdint.h>

/* Helper: Get uptime in milliseconds */
static uint32_t bt_get_uptime_ms(void)
{
    return (uint32_t)clock_get_uptime_milliseconds();
}

/* Device manager instance */
static struct bt_device_manager *bt_manager = NULL;

/* Open/close state */
static bool bt_hci_opened = false;

/**
 * bt_device_init - Initialize Bluetooth device management
 */
int bt_device_init(void)
{
    if (bt_manager)
        return 0;  /* Already initialized */
    
    /* Initialize HCI core */
    int ret = hci_core_init();
    if (ret < 0)
        return ret;
    
    /* Allocate device manager */
    bt_manager = (struct bt_device_manager *)kmalloc(sizeof(struct bt_device_manager));
    if (!bt_manager)
        return -ENOMEM;
    
    memset(bt_manager, 0, sizeof(struct bt_device_manager));
    bt_manager->initialized = true;
    bt_manager->hci_dev = hci_get_device();
    bt_manager->scan_active = 0;
    bt_manager->scan_start_time = 0;
    
    bt_hci_opened = false;
    
    return 0;
}

/**
 * bt_device_cleanup - Cleanup Bluetooth device management
 */
void bt_device_cleanup(void)
{
    if (bt_manager)
    {
        if (bt_manager->scan_active)
            hci_inquiry_cancel();
        
        kfree(bt_manager);
        bt_manager = NULL;
    }
    
    bt_hci_opened = false;
}

/**
 * bt_scan_state_sync - Align proc scan flag with HCI inquiry state
 */
void bt_scan_state_sync(void)
{
    struct hci_device *hdev;

    if (!bt_manager)
        return;

    hdev = hci_get_device();
    if (!hdev)
        return;

    if (bt_manager->scan_active && !hdev->scanning)
        bt_manager->scan_active = 0;
}

/**
 * bt_device_get_manager - Get device manager instance
 */
struct bt_device_manager *bt_device_get_manager(void)
{
    return bt_manager;
}

/**
 * bt_hci_open - Open HCI device
 */
int bt_hci_open(void)
{
    if (!bt_manager)
    {
        int ret = bt_device_init();
        if (ret < 0)
            return ret;
    }
    
    if (bt_hci_opened)
        return 0;  /* Already open */
    
    bt_hci_opened = true;
    
    /* Initialize HCI if not already done */
    if (!bt_manager->hci_dev || !bt_manager->hci_dev->initialized)
    {
        hci_reset();
    }
    
    return 0;
}

/**
 * bt_hci_close - Close HCI device
 */
int bt_hci_close(void)
{
    if (!bt_hci_opened)
        return 0;
    
    /* Cancel any active scan */
    if (bt_manager && bt_manager->scan_active)
    {
        hci_inquiry_cancel();
        bt_manager->scan_active = 0;
    }
    
    bt_hci_opened = false;
    
    return 0;
}

/**
 * bt_hci_read - Read HCI events
 */
int bt_hci_read(char *buffer, size_t count)
{
    if (!bt_hci_opened || !buffer || count == 0)
        return -EINVAL;
    
    /* Process pending events */
    hci_process_events();
    
    /* Return HCI event status and scan information */
    if (count < 64)
        return -EINVAL;
    
    /* Build status message */
    size_t off = 0;
    if (bt_manager)
    {
        if (bt_manager->scan_active)
        {
            uint32_t elapsed = bt_get_uptime_ms() - bt_manager->scan_start_time;
            off += (size_t)snprintf(buffer + off, count - off, 
                                   "HCI Status: Scanning (elapsed: %u ms)\n", elapsed);
        }
        else
        {
            off += (size_t)snprintf(buffer + off, count - off, "HCI Status: Ready\n");
        }
        
        /* Add device count */
        struct bluetooth_device devices[32];
        int num_devices = hci_get_discovered_devices(devices, 32);
        if (num_devices >= 0)
        {
            off += (size_t)snprintf(buffer + off, count - off, 
                                   "Discovered devices: %d\n", num_devices);
        }
    }
    else
    {
        off += (size_t)snprintf(buffer + off, count - off, "HCI Status: Not initialized\n");
    }
    
    return (int)off;
}

/**
 * bt_hci_write - Write HCI commands (raw)
 */
int bt_hci_write(const char *buffer, size_t count)
{
    if (!bt_hci_opened || !buffer || count == 0)
        return -EINVAL;
    
    /* For minimal implementation, just process simple text commands */
    char cmd_buf[256];
    size_t cmd_len = (count < sizeof(cmd_buf) - 1) ? count : (sizeof(cmd_buf) - 1);
    memcpy(cmd_buf, buffer, cmd_len);
    cmd_buf[cmd_len] = '\0';
    
    /* Simple command parsing */
    if (strncmp(cmd_buf, "reset", 5) == 0)
    {
        return hci_reset();
    }
    else if (strncmp(cmd_buf, "scan", 4) == 0)
    {
        if (!bt_manager)
            return -ENODEV;
        
        if (bt_manager->scan_active)
            return -EBUSY;
        
        int ret = hci_inquiry(8, 0);  /* 10.24 seconds, unlimited responses */
        if (ret == 0)
        {
            bt_manager->scan_active = 1;
            bt_manager->scan_start_time = bt_get_uptime_ms();
        }
        return ret;
    }
    else if (strncmp(cmd_buf, "stop", 4) == 0)
    {
        if (!bt_manager)
            return -ENODEV;
        
        if (!bt_manager->scan_active)
            return 0;
        
        int ret = hci_inquiry_cancel();
        if (ret == 0)
            bt_manager->scan_active = 0;
        return ret;
    }
    
    /* Unknown command */
    return -EINVAL;
}

/**
 * bt_hci_ioctl - IOCTL control interface
 */
int bt_hci_ioctl(unsigned int cmd, unsigned long arg)
{
    if (!bt_hci_opened)
        return -ENODEV;
    
    switch (cmd)
    {
        case BT_IOCTL_RESET:
            return hci_reset();
        
        case BT_IOCTL_READ_BD_ADDR:
            if (!arg)
                return -EINVAL;
            return hci_read_bd_addr((uint8_t *)arg);
        
        case BT_IOCTL_START_INQUIRY:
            if (!bt_manager)
                return -ENODEV;
            
            if (bt_manager->scan_active)
                return -EBUSY;
            
            {
                struct bt_inquiry_params *params = (struct bt_inquiry_params *)arg;
                uint8_t duration = params ? params->duration : 8;
                uint8_t max_resp = params ? params->max_responses : 0;
                
                int ret = hci_inquiry(duration, max_resp);
                if (ret == 0)
                {
                    bt_manager->scan_active = 1;
                    bt_manager->scan_start_time = bt_get_uptime_ms();
                }
                return ret;
            }
        
        case BT_IOCTL_STOP_INQUIRY:
            if (!bt_manager || !bt_manager->scan_active)
                return 0;
            
            {
                int ret = hci_inquiry_cancel();
                if (ret == 0)
                    bt_manager->scan_active = 0;
                return ret;
            }
        
        case BT_IOCTL_CLEAR_DEVICES:
            return hci_clear_discovered_devices();
        
        case BT_IOCTL_GET_DEVICE_COUNT:
            {
                struct bluetooth_device devices[32];
                int count = hci_get_discovered_devices(devices, 32);
                if (count < 0)
                    return count;
                if (arg)
                    *(int *)arg = count;
                return count;
            }
        
        default:
            return -EINVAL;
    }
}

/**
 * bt_proc_devices_read - Read /proc/bluetooth/devices
 */
static int bt_proc_devices_fill(char *buffer, size_t count,
                                struct bluetooth_device *devices);

/*
 * The discovered-device array is 8 KiB. On the stack it made this reader the
 * deepest kernel path a plain session could reach: `cat /proc/bluetooth/
 * devices`, or any find over /proc, ran it under sys_read for a measured
 * 14 KiB peak on a 32 KiB kernel stack.
 */
int bt_proc_devices_read(char *buffer, size_t count)
{
    struct bluetooth_device *devices;
    int rc;

    if (!buffer || count == 0)
        return -EINVAL;

    devices = kmalloc_try(sizeof(*devices) * 32);
    if (!devices)
        return -ENOMEM;

    rc = bt_proc_devices_fill(buffer, count, devices);
    kfree(devices);
    return rc;
}

static int bt_proc_devices_fill(char *buffer, size_t count,
                                struct bluetooth_device *devices)
{
    /* Process pending events to update device list */
    hci_process_events();
    
    int num_devices = hci_get_discovered_devices(devices, 32);
    if (num_devices < 0)
        return num_devices;
    
    size_t off = 0;
    
    /* Header */
    int n = snprintf(buffer + off, (off < count) ? (count - off) : 0,
                     "Bluetooth Devices:\n");
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    
    if (num_devices == 0)
    {
        n = snprintf(buffer + off, (off < count) ? (count - off) : 0,
                     "No devices discovered.\n");
        if (n > 0 && n < (int)(count - off))
            off += (size_t)n;
        
        return (int)off;
    }
    
    /* List devices */
    for (int i = 0; i < num_devices; i++)
    {
        struct bluetooth_device *dev = &devices[i];
        if (!dev->discovered)
            continue;
        
        n = snprintf(buffer + off, (off < count) ? (count - off) : 0,
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     dev->bd_addr[5], dev->bd_addr[4], dev->bd_addr[3],
                     dev->bd_addr[2], dev->bd_addr[1], dev->bd_addr[0]);
        if (n > 0 && n < (int)(count - off))
            off += (size_t)n;
        else
            break;
        
        if (dev->name[0] != '\0')
        {
            n = snprintf(buffer + off, (off < count) ? (count - off) : 0,
                         " %s", dev->name);
            if (n > 0 && n < (int)(count - off))
                off += (size_t)n;
        }
        
        n = snprintf(buffer + off, (off < count) ? (count - off) : 0,
                     " (Class: %02X:%02X:%02X, RSSI: %d)\n",
                     dev->device_class[2], dev->device_class[1], dev->device_class[0],
                     dev->rssi);
        if (n > 0 && n < (int)(count - off))
            off += (size_t)n;
        else
            break;
    }
    
    if (off < count)
        buffer[off] = '\0';
    
    return (int)off;
}

/**
 * bt_proc_scan_read - Read /proc/bluetooth/scan
 */
int bt_proc_scan_read(char *buffer, size_t count)
{
    if (!buffer || count == 0)
        return -EINVAL;
    
    /* Process pending events */
    hci_process_events();
    bt_scan_state_sync();
    
    if (!bt_manager)
    {
        return snprintf(buffer, count, "Bluetooth not initialized\n");
    }
    
    if (bt_manager->scan_active)
    {
        uint32_t elapsed = bt_get_uptime_ms() - bt_manager->scan_start_time;
        return snprintf(buffer, count, "Scanning... (elapsed: %u ms)\n", elapsed);
    }
    else
    {
        return snprintf(buffer, count, "Not scanning\n");
    }
}

/**
 * bt_proc_scan_write - Write to /proc/bluetooth/scan (start/stop)
 */
int bt_proc_scan_write(const char *command)
{
    if (!command)
        return -EINVAL;
    
    if (!bt_manager)
    {
        int ret = bt_device_init();
        if (ret < 0)
            return ret;
    }

    {
        int open_ret = bt_hci_open();

        if (open_ret < 0)
            return open_ret;
    }
    
    if (strncmp(command, "start", 5) == 0)
    {
        if (bt_manager->scan_active)
        {
            LOG_INFO("BLUETOOTH", "Scan already active");
            return -EBUSY;
        }
        int ret = hci_inquiry(8, 0);
        if (ret == 0)
        {
            bt_manager->scan_active = 1;
            bt_manager->scan_start_time = bt_get_uptime_ms();
            LOG_INFO("BLUETOOTH", "Scan started (inquiry 8*1.28s, max responses unlimited)");
        }
        else
            LOG_ERROR("BLUETOOTH", "Scan start failed");
        return ret;
    }
    else if (strncmp(command, "stop", 4) == 0)
    {
        if (!bt_manager->scan_active)
            return 0;
        int ret = hci_inquiry_cancel();
        if (ret == 0)
        {
            bt_manager->scan_active = 0;
            LOG_INFO("BLUETOOTH", "Scan stopped");
        }
        return ret;
    }
    
    return -EINVAL;
}
