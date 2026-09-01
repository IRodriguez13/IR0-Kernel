/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_log_hostshare.c
 * Description: Export logging ring buffer to QEMU virtio-9p (opt-in).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_log_hostshare.h>
#include <config.h>
#include <ir0/virtio_9p.h>
#include <ir0/version.h>
#include <ir0/ktm/klog.h>

#include <stddef.h>
#include <string.h>

#ifndef CONFIG_BOOT_LOG_HOSTSHARE
#define CONFIG_BOOT_LOG_HOSTSHARE 0
#endif

/* Cap dump size — enough for early boot; not a full Linux dmesg. */
#define IR0_BOOT_LOG_DUMP_MAX (48 * 1024)

static char g_boot_log_dump[IR0_BOOT_LOG_DUMP_MAX];

int ir0_boot_log_hostshare_try(void)
{
	int n;
	int rc;
	size_t off = 0;
	const char *hdr;

	if (!CONFIG_BOOT_LOG_HOSTSHARE)
	{
		return 1;
	}

	if (!virtio_9p_ready())
	{
		klog_smoke("BOOT_LOG_HOSTSHARE_SKIP");
		return 1;
	}

	hdr = "IR0 kernel " IR0_VERSION_STRING " boot log (hostshare)\n"
	      "---\n";
	while (hdr[off] && off + 1 < sizeof(g_boot_log_dump))
	{
		g_boot_log_dump[off] = hdr[off];
		off++;
	}

	n = klog_read_records(g_boot_log_dump + off,
			      sizeof(g_boot_log_dump) - off);
	if (n < 0)
	{
		klog_smoke("BOOT_LOG_HOSTSHARE_FAIL");
		return -1;
	}
	off += (size_t)n;
	if (off >= sizeof(g_boot_log_dump))
		off = sizeof(g_boot_log_dump) - 1;
	g_boot_log_dump[off] = '\0';

	rc = virtio_9p_write_file(IR0_BOOT_LOG_HOSTSHARE_NAME, g_boot_log_dump, off);
	if (rc < 0)
	{
		klog_smoke("BOOT_LOG_HOSTSHARE_FAIL");
		return rc;
	}

	klog_info("BOOT", "boot log written to hostshare " IR0_BOOT_LOG_HOSTSHARE_NAME);
	klog_smoke("BOOT_LOG_HOSTSHARE_OK");
	return 0;
}
