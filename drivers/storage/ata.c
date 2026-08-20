/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ata.c
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ata.c
 * Description: ATA/IDE disk driver for storage device access
 */

#include "ata.h"
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/ktm/klog.h>
#include <string.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/resource_registry.h>
#include <stdint.h>

#define ATA_WAIT_LOOPS 50000
#define ATA_MAX_RETRY  3

int vfs_exec_audit_is_active(void);
static bool ata_wait_ready_probe(uint8_t drive);
static bool ata_wait_drq_probe(uint8_t drive);

static uint16_t ata_get_status_port(uint8_t drive);
static uint16_t ata_get_drive_head_port(uint8_t drive);
static uint16_t ata_get_sector_count_port(uint8_t drive);
static uint16_t ata_get_lba_low_port(uint8_t drive);
static uint16_t ata_get_lba_mid_port(uint8_t drive);
static uint16_t ata_get_lba_high_port(uint8_t drive);
static uint16_t ata_get_command_port(uint8_t drive);
static uint16_t ata_get_port_base(uint8_t drive);

typedef enum
{
	ATA_POLL_OK = 0,
	ATA_POLL_BSY_TIMEOUT,
	ATA_POLL_DRQ_TIMEOUT,
	ATA_POLL_ERR,
	ATA_POLL_DF,
	ATA_POLL_FLOATING,
} ata_poll_result_t;

static int ata_single_sector_announced;
static int ata_multi_sector_warned;
static int ata_buffer_align_warned;

static int ata_trace_on(void)
{
	return vfs_exec_audit_is_active();
}

static void ata_emit_classify(const char *tag)
{
	if (!tag)
		return;
	klog_info_fmt("ATA", "CLASSIFY %s", tag);
}

static const char *ata_poll_classify(ata_poll_result_t res)
{
	switch (res)
	{
	case ATA_POLL_BSY_TIMEOUT:
		return "ATA_FAIL_BSY_TIMEOUT";
	case ATA_POLL_DRQ_TIMEOUT:
		return "ATA_FAIL_DRQ_TIMEOUT";
	case ATA_POLL_ERR:
		return "ATA_FAIL_ERR";
	case ATA_POLL_DF:
		return "ATA_FAIL_DF";
	case ATA_POLL_FLOATING:
		return "ATA_FAIL_FLOATING_BUS";
	default:
		return NULL;
	}
}

static uint16_t ata_get_error_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_ERROR : ATA_SECONDARY_ERROR;
}

static const char *ata_channel_name(uint8_t drive)
{
	return (drive < 2) ? "primary" : "secondary";
}

static void ata_io_delay(uint8_t drive)
{
	uint16_t status_port = ata_get_status_port(drive);

	(void)inb(status_port);
	(void)inb(status_port);
	(void)inb(status_port);
	(void)inb(status_port);
}

static void ata_trace_status(const char *when, uint8_t drive, uint8_t status)
{
	if (!ata_trace_on())
		return;

	klog_info_fmt("ATA",
		      "when=%s drive=0x%x ch=%s status=0x%x BSY=%s DRQ=%s ERR=%s DF=%s",
		      when ? when : "?", (unsigned)drive, ata_channel_name(drive),
		      (unsigned)status, (status & ATA_STATUS_BSY) ? "1" : "0",
		      (status & ATA_STATUS_DRQ) ? "1" : "0",
		      (status & ATA_STATUS_ERR) ? "1" : "0",
		      (status & ATA_STATUS_DF) ? "1" : "0");
}

static void ata_trace_wait_fail(const char *fn, ata_poll_result_t res,
				uint8_t drive, int loops, uint8_t status)
{
	const char *tag = ata_poll_classify(res);

	if (status & ATA_STATUS_ERR)
	{
		klog_info_fmt("ATA",
			      "wait_fail fn=%s drive=0x%x loops=0x%x status=0x%x err_reg=0x%x",
			      fn ? fn : "?", (unsigned)drive, (unsigned)loops,
			      (unsigned)status,
			      (unsigned)inb(ata_get_error_port(drive)));
	}
	else
	{
		klog_info_fmt("ATA",
			      "wait_fail fn=%s drive=0x%x loops=0x%x status=0x%x",
			      fn ? fn : "?", (unsigned)drive, (unsigned)loops,
			      (unsigned)status);
	}
	if (tag)
		ata_emit_classify(tag);
}

static ata_poll_result_t ata_poll_not_busy(uint8_t drive, int *loops_out,
					   uint8_t *status_out)
{
	uint16_t status_port = ata_get_status_port(drive);
	int i;

	for (i = 0; i < ATA_WAIT_LOOPS; i++)
	{
		uint8_t status = inb(status_port);

		if (status == 0xFF)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_FLOATING;
		}
		if (status & ATA_STATUS_ERR)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_ERR;
		}
		if (status & ATA_STATUS_DF)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_DF;
		}
		if (!(status & ATA_STATUS_BSY))
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_OK;
		}
	}

	if (loops_out)
		*loops_out = ATA_WAIT_LOOPS;
	if (status_out)
		*status_out = inb(status_port);
	return ATA_POLL_BSY_TIMEOUT;
}

static ata_poll_result_t ata_poll_drq(uint8_t drive, int *loops_out,
				      uint8_t *status_out)
{
	uint16_t status_port = ata_get_status_port(drive);
	int i;

	for (i = 0; i < ATA_WAIT_LOOPS; i++)
	{
		uint8_t status = inb(status_port);

		if (status == 0xFF)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_FLOATING;
		}
		if (status & ATA_STATUS_ERR)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_ERR;
		}
		if (status & ATA_STATUS_DF)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_DF;
		}
		if (status & ATA_STATUS_DRQ)
		{
			if (loops_out)
				*loops_out = i;
			if (status_out)
				*status_out = status;
			return ATA_POLL_OK;
		}
	}

	if (loops_out)
		*loops_out = ATA_WAIT_LOOPS;
	if (status_out)
		*status_out = inb(status_port);
	return ATA_POLL_DRQ_TIMEOUT;
}

static void ata_recover_after_error(uint8_t drive)
{
	(void)inb(ata_get_error_port(drive));
	ata_io_delay(drive);
	(void)ata_poll_not_busy(drive, NULL, NULL);
}

// Global variables
bool ata_drives_present[4] = {false, false, false, false};

// Parsed device info array (filled during identify)
ata_device_info_t ata_devices[4] = {{0}};

bool ata_get_device_info(uint8_t drive, ata_device_info_t *out)
{
    if (drive >= 4 || !out)
        return false;
    if (!ata_devices[drive].present)
        return false;
    *out = ata_devices[drive];
    return true;
}

// Port I/O: inw/outw from <ir0/cpu.h>

// Get port base for drive
static uint16_t ata_get_port_base(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_DATA;
    } 
    else 
    {
        return ATA_SECONDARY_DATA;
    }
}

/**
 * ata_get_status_port - get status port for drive
 * @drive: drive number (0-3)
 *
 * Returns status port address for the specified drive.
 */
static uint16_t ata_get_status_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_STATUS : ATA_SECONDARY_STATUS;
}

/**
 * ata_get_drive_head_port - get drive head port for drive
 * @drive: drive number (0-3)
 *
 * Returns drive head port address for the specified drive.
 */
static uint16_t ata_get_drive_head_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_DRIVE_HEAD : ATA_SECONDARY_DRIVE_HEAD;
}

/**
 * ata_get_sector_count_port - get sector count port for drive
 * @drive: drive number (0-3)
 *
 * Returns sector count port address for the specified drive.
 */
static uint16_t ata_get_sector_count_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_SECTOR_COUNT : ATA_SECONDARY_SECTOR_COUNT;
}

/**
 * ata_get_lba_low_port - get LBA low port for drive
 * @drive: drive number (0-3)
 *
 * Returns LBA low port address for the specified drive.
 */
static uint16_t ata_get_lba_low_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_LBA_LOW : ATA_SECONDARY_LBA_LOW;
}

static uint16_t ata_get_lba_mid_port(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_LBA_MID;
    } 
    else 
    {
        return ATA_SECONDARY_LBA_MID;
    }
}

static uint16_t ata_get_lba_high_port(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_LBA_HIGH;
    } 
    else 
    {
        return ATA_SECONDARY_LBA_HIGH;
    }
}

// Get command port for drive
static uint16_t ata_get_command_port(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_COMMAND;
    } 
    else 
    {
        return ATA_SECONDARY_COMMAND;
    }
}

void ata_init(void) 
{
    // Reset all drives
    for (int i = 0; i < 4; i++) 
    {
        ata_reset_drive(i);
    }
    
    /* Try to identify drives */
    for (int i = 0; i < 4; i++)
    {
        ata_drives_present[i] = ata_identify_drive(i);
    }
    if (ata_drives_present[0] || ata_drives_present[1])
    {
        resource_register_irq(14, "ata14");
        resource_register_ioport(ATA_PRIMARY_DATA, ATA_PRIMARY_COMMAND, "ata primary");
    }
    if (ata_drives_present[2] || ata_drives_present[3])
    {
        resource_register_irq(15, "ata15");
        resource_register_ioport(ATA_SECONDARY_DATA, ATA_SECONDARY_COMMAND, "ata secondary");
    }
}

bool ata_is_available(void) 
{
    // Check if any drive is present
    for (int i = 0; i < 4; i++) 
    {
        if (ata_drives_present[i]) 
        {
            return true;
        }
    }
    return false;
}

void ata_reset_drive(uint8_t drive) 
{
    uint16_t status_port = ata_get_status_port(drive);
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select);
    
    // Wait a bit
    for (volatile int i = 0; i < 1000; i++);
    
    // Check if drive is present
    uint8_t status = inb(status_port);
    if (status == 0xFF) 
    {
        return; // No drive
    }
    
    // Wait for drive to be ready
    ata_wait_ready(drive);
}

bool ata_identify_drive(uint8_t drive) 
{
    
    uint16_t status_port = ata_get_status_port(drive);
    (void)status_port; // Variable not used in this implementation
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    uint16_t command_port = ata_get_command_port(drive);
    uint16_t data_port = ata_get_port_base(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select);
    
    // Wait for drive to be ready (quiet: empty slots are normal)
    if (!ata_wait_ready_probe(drive)) 
    {
        klog_info_fmt("ATA", "CLASSIFY ATA_PROBE_ABSENT drive=0x%x",
                      (unsigned)drive);
        return false;
    }
    
    // Send IDENTIFY command
    outb(command_port, ATA_CMD_IDENTIFY);
    
    // Wait for response
    if (!ata_wait_drq_probe(drive)) 
    {
        klog_info_fmt("ATA", "CLASSIFY ATA_PROBE_ABSENT drive=0x%x",
                      (unsigned)drive);
        return false;
    }
    
    // Read identify data and store/parse it for richer device information
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) 
    {
        identify_data[i] = inw(data_port);
    }

    // Mark presence
    ata_drives_present[drive] = true;
    ata_devices[drive].present = true;

    // Helper: extract ASCII string from identify words (each word is two bytes; high byte first)
    // Extract serial (words 10-19, 20 bytes)
    {
        int out_i = 0;
        for (int w = 10; w <= 19 && out_i < (int)sizeof(ata_devices[drive].serial) - 1; w++)
        {
            uint16_t word = identify_data[w];
            char c1 = (char)(word >> 8);
            char c2 = (char)(word & 0xFF);
            ata_devices[drive].serial[out_i++] = c1;
            if (out_i < (int)sizeof(ata_devices[drive].serial) - 1)
                ata_devices[drive].serial[out_i++] = c2;
        }
        ata_devices[drive].serial[out_i] = '\0';
        // Trim trailing spaces
        while (out_i > 0 && ata_devices[drive].serial[out_i - 1] == ' ')
            ata_devices[drive].serial[--out_i] = '\0';
    }

    // Extract model (words 27-46, 40 bytes)
    {
        int out_i = 0;
        for (int w = 27; w <= 46 && out_i < (int)sizeof(ata_devices[drive].model) - 1; w++)
        {
            uint16_t word = identify_data[w];
            char c1 = (char)(word >> 8);
            char c2 = (char)(word & 0xFF);
            ata_devices[drive].model[out_i++] = c1;
            if (out_i < (int)sizeof(ata_devices[drive].model) - 1)
                ata_devices[drive].model[out_i++] = c2;
        }
        ata_devices[drive].model[out_i] = '\0';
        while (out_i > 0 && ata_devices[drive].model[out_i - 1] == ' ')
            ata_devices[drive].model[--out_i] = '\0';
    }

    // Capacity: try 28-bit LBA words 60-61 first
    uint32_t sectors28 = ((uint32_t)identify_data[61] << 16) | (uint32_t)identify_data[60];
    if (sectors28 != 0)
    {
        ata_devices[drive].capacity_bytes = (uint64_t)sectors28 * (uint64_t)ATA_SECTOR_SIZE;
    }
    else
    {
        // Try 48-bit LBA (words 100-103)
        uint64_t sectors48 = ((uint64_t)identify_data[103] << 48) |
                             ((uint64_t)identify_data[102] << 32) |
                             ((uint64_t)identify_data[101] << 16) |
                             (uint64_t)identify_data[100];
        if (sectors48 != 0)
        {
            ata_devices[drive].capacity_bytes = sectors48 * (uint64_t)ATA_SECTOR_SIZE;
        }
        else
        {
            ata_devices[drive].capacity_bytes = 0; // unknown
        }
    }

    if (ata_devices[drive].capacity_bytes != 0)
        ata_devices[drive].size =
            ata_devices[drive].capacity_bytes / (uint64_t)ATA_SECTOR_SIZE;

    return true;
}

bool ata_wait_ready(uint8_t drive)
{
	int loops = 0;
	uint8_t status = 0;
	ata_poll_result_t res;

	res = ata_poll_not_busy(drive, &loops, &status);
	if (res != ATA_POLL_OK)
	{
		ata_trace_wait_fail("wait_ready", res, drive, loops, status);
		return false;
	}
	return true;
}

static bool ata_wait_ready_probe(uint8_t drive)
{
	int loops = 0;
	uint8_t status = 0;

	return ata_poll_not_busy(drive, &loops, &status) == ATA_POLL_OK;
}

bool ata_wait_drq(uint8_t drive)
{
	int loops = 0;
	uint8_t status = 0;
	ata_poll_result_t res;

	res = ata_poll_drq(drive, &loops, &status);
	if (res != ATA_POLL_OK)
	{
		ata_trace_wait_fail("wait_drq", res, drive, loops, status);
		return false;
	}
	return true;
}

static bool ata_wait_drq_probe(uint8_t drive)
{
	int loops = 0;
	uint8_t status = 0;

	return ata_poll_drq(drive, &loops, &status) == ATA_POLL_OK;
}

static bool ata_read_one_sector_pio(uint8_t drive, uint32_t lba, void *buffer,
				    uint8_t sector_idx, uint8_t cmd_sectors)
{
	uint16_t drive_head_port = ata_get_drive_head_port(drive);
	uint16_t sector_count_port = ata_get_sector_count_port(drive);
	uint16_t lba_low_port = ata_get_lba_low_port(drive);
	uint16_t lba_mid_port = ata_get_lba_mid_port(drive);
	uint16_t lba_high_port = ata_get_lba_high_port(drive);
	uint16_t command_port = ata_get_command_port(drive);
	uint16_t data_port = ata_get_port_base(drive);
	uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
	uint16_t *buffer16 = (uint16_t *)buffer;
	int attempt;
	uint8_t status;

	for (attempt = 0; attempt < ATA_MAX_RETRY; attempt++)
	{
		int loops = 0;
		ata_poll_result_t poll_res;

		if (attempt > 0)
			ata_recover_after_error(drive);

		outb(drive_head_port, drive_select | 0x40);
		ata_io_delay(drive);
		status = inb(ata_get_status_port(drive));
		ata_trace_status("after_select", drive, status);

		poll_res = ata_poll_not_busy(drive, &loops, &status);
		if (poll_res != ATA_POLL_OK)
		{
			ata_trace_wait_fail("pre_cmd_ready", poll_res, drive, loops,
					    status);
			continue;
		}

		outb(sector_count_port, cmd_sectors);
		outb(lba_low_port, lba & 0xFF);
		outb(lba_mid_port, (lba >> 8) & 0xFF);
		outb(lba_high_port, (lba >> 16) & 0xFF);
		outb(drive_head_port, drive_select | 0x40 | ((lba >> 24) & 0x0F));
		ata_io_delay(drive);
		status = inb(ata_get_status_port(drive));
		ata_trace_status("after_lba", drive, status);

		outb(command_port, ATA_CMD_READ_SECTORS);
		ata_io_delay(drive);
		status = inb(ata_get_status_port(drive));
		ata_trace_status("after_cmd", drive, status);

		if (ata_trace_on())
		{
			klog_info_fmt("ATA",
				      "read drive=0x%x lba=0x%x sector_idx=0x%x cmd_sectors=0x%x attempt=0x%x buf=0x%llx align=0x%x",
				      (unsigned)drive, (unsigned)lba,
				      (unsigned)sector_idx, (unsigned)cmd_sectors,
				      (unsigned)attempt,
				      (unsigned long long)(uintptr_t)buffer,
				      (unsigned)((uintptr_t)buffer & 0xFU));
		}

		poll_res = ata_poll_drq(drive, &loops, &status);
		if (poll_res != ATA_POLL_OK)
		{
			ata_trace_wait_fail("sector_drq", poll_res, drive, loops,
					    status);
			continue;
		}

		for (int i = 0; i < 256; i++)
			buffer16[i] = inw(data_port);

		poll_res = ata_poll_not_busy(drive, &loops, &status);
		if (poll_res != ATA_POLL_OK)
		{
			ata_trace_wait_fail("post_sector_ready", poll_res, drive,
					    loops, status);
			continue;
		}

		if (attempt > 0)
			ata_emit_classify("ATA_RETRY_RECOVERED");
		return true;
	}

	return false;
}

bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, void *buffer)
{
	uint8_t s;
	int need_bounce;

	if (!ata_drives_present[drive] || !buffer || num_sectors == 0)
		return false;

	if (!ata_single_sector_announced)
	{
		ata_single_sector_announced = 1;
		ata_emit_classify("ATA_SINGLE_SECTOR_MODE_FIXED");
	}

	need_bounce = (((uintptr_t)buffer & 0x1U) != 0U);
	if (need_bounce && !ata_buffer_align_warned)
	{
		ata_buffer_align_warned = 1;
		ata_emit_classify("ATA_BUFFER_ALIGNMENT_SUSPECT");
	}

	if (num_sectors > 1 && !ata_multi_sector_warned)
	{
		ata_multi_sector_warned = 1;
		ata_emit_classify("ATA_MULTI_SECTOR_UNSAFE");
	}

	/*
	 * PIO command/data is not reentrant. Concurrent exec of the same
	 * BusyBox inode (hexdump | head) preempted mid-DRQ and tore the ELF.
	 */
	{
		unsigned long irq_flags = irq_save();
		int failed = 0;

		for (s = 0; s < num_sectors; s++)
		{
			uint32_t sector_lba = lba + (uint32_t)s;
			uint8_t *dest = (uint8_t *)buffer + (size_t)s * ATA_SECTOR_SIZE;

			if (need_bounce)
			{
				uint8_t bounce[ATA_SECTOR_SIZE] __attribute__((aligned(2)));

				if (!ata_read_one_sector_pio(drive, sector_lba, bounce, s, 1))
				{
					failed = 1;
					break;
				}
				memcpy(dest, bounce, ATA_SECTOR_SIZE);
			}
			else if (!ata_read_one_sector_pio(drive, sector_lba, dest, s, 1))
			{
				failed = 1;
				break;
			}
		}
		irq_restore(irq_flags);
		if (failed)
			return false;
	}

	return true;
}

static bool ata_write_one_sector_pio(uint8_t drive, uint32_t lba, const void *buffer)
{
	uint16_t drive_head_port = ata_get_drive_head_port(drive);
	uint16_t sector_count_port = ata_get_sector_count_port(drive);
	uint16_t lba_low_port = ata_get_lba_low_port(drive);
	uint16_t lba_mid_port = ata_get_lba_mid_port(drive);
	uint16_t lba_high_port = ata_get_lba_high_port(drive);
	uint16_t command_port = ata_get_command_port(drive);
	uint16_t data_port = ata_get_port_base(drive);
	uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
	const uint16_t *buffer16 = (const uint16_t *)buffer;
	int i;

	outb(drive_head_port, drive_select | 0x40);
	if (!ata_wait_ready(drive))
		return false;

	outb(sector_count_port, 1);
	outb(lba_low_port, lba & 0xFF);
	outb(lba_mid_port, (lba >> 8) & 0xFF);
	outb(lba_high_port, (lba >> 16) & 0xFF);
	outb(drive_head_port, drive_select | 0x40 | ((lba >> 24) & 0x0F));
	outb(command_port, ATA_CMD_WRITE_SECTORS);

	if (!ata_wait_drq(drive))
		return false;

	for (i = 0; i < 256; i++)
		outw(data_port, buffer16[i]);

	return ata_wait_ready(drive);
}

bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
		       const void *buffer)
{
	uint8_t s;
	int need_bounce;
	unsigned long irq_flags;
	bool ok = false;

	if (!ata_drives_present[drive] || !buffer || num_sectors == 0)
		return false;

	need_bounce = (((uintptr_t)buffer & 0x1U) != 0U);
	if (need_bounce && !ata_buffer_align_warned)
	{
		ata_buffer_align_warned = 1;
		ata_emit_classify("ATA_BUFFER_ALIGNMENT_SUSPECT");
	}

	if (num_sectors > 1 && !ata_multi_sector_warned)
	{
		ata_multi_sector_warned = 1;
		ata_emit_classify("ATA_MULTI_SECTOR_UNSAFE");
	}

	irq_flags = irq_save();

	/*
	 * Odd caller buffers: PIO one sector through an aligned bounce.
	 * Aligned multi-sector keeps the historical single-command path.
	 */
	if (need_bounce)
	{
		for (s = 0; s < num_sectors; s++)
		{
			uint8_t bounce[ATA_SECTOR_SIZE] __attribute__((aligned(2)));
			const uint8_t *src =
				(const uint8_t *)buffer + (size_t)s * ATA_SECTOR_SIZE;

			memcpy(bounce, src, ATA_SECTOR_SIZE);
			if (!ata_write_one_sector_pio(drive, lba + (uint32_t)s, bounce))
				goto out;
		}
		ok = true;
		goto out;
	}

	{
		uint16_t drive_head_port = ata_get_drive_head_port(drive);
		uint16_t sector_count_port = ata_get_sector_count_port(drive);
		uint16_t lba_low_port = ata_get_lba_low_port(drive);
		uint16_t lba_mid_port = ata_get_lba_mid_port(drive);
		uint16_t lba_high_port = ata_get_lba_high_port(drive);
		uint16_t command_port = ata_get_command_port(drive);
		uint16_t data_port = ata_get_port_base(drive);
		uint8_t drive_select =
			(drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
		const uint16_t *buffer16 = (const uint16_t *)buffer;
		int sector;

		outb(drive_head_port, drive_select | 0x40);
		if (!ata_wait_ready(drive))
			goto out;

		outb(sector_count_port, num_sectors);
		outb(lba_low_port, lba & 0xFF);
		outb(lba_mid_port, (lba >> 8) & 0xFF);
		outb(lba_high_port, (lba >> 16) & 0xFF);
		outb(drive_head_port, drive_select | 0x40 | ((lba >> 24) & 0x0F));
		outb(command_port, ATA_CMD_WRITE_SECTORS);

		for (sector = 0; sector < num_sectors; sector++)
		{
			int i;

			if (!ata_wait_drq(drive))
				goto out;

			for (i = 0; i < 256; i++)
				outw(data_port, buffer16[sector * 256 + i]);
		}

		/*
		 * Do not FLUSH_CACHE after every PIO write — that serializes bulk
		 * MINIX zone growth (FASE52D ~500KB) into multi-minute hangs.
		 * Callers that need durability should use ir0_block_flush / fsync.
		 */
		ok = ata_wait_ready(drive);
	}

out:
	irq_restore(irq_flags);
	return ok;
}

