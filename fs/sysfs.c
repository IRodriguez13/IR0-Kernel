/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sysfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - SYSFS (System Filesystem)
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Simple /sys filesystem similar to Linux sysfs
 * Exposes kernel configuration and device information
 * 
 * Linux sysfs design principles we follow:
 * - Hierarchical device/driver representation
 * - Kernel parameters as writable files
 * - Standard file operations (read/write) for configuration
 * - Clean separation: /proc = stats, /sys = configuration
 * 
 * Implementation Notes:
 * - Uses FD range 3000-3999 for /sys file descriptors
 * - Reuses procfs offset tracking mechanism
 * - Files are dynamically generated on access
 * - Supports both read and write operations
 */

#include "procfs.h"
#include <ir0/sysfs.h>
#include <ir0/stat.h>
#include <ir0/kmem.h>
#include <ir0/version.h>
#include <ir0/console_backend.h>
#include <string.h>
#include <ir0/errno.h>
#include <config.h>
#include <ir0/partition.h>
#include <ir0/blockdev.h>
#include <ir0/video_backend.h>
#include <ir0/pseudo_fs.h>
#include <ir0/oops.h>
#include <ir0/arch_port.h>
#include <ir0/mm_port.h>
#include <ir0/ktm/stack_watch.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

#define SYS_BUFFER_SIZE 4096
#define SYS_FD_BASE 3000  /* sysfs uses FD range 3000-3999 */
#define SYS_DEFAULT_FILE_SIZE 256
#define SYS_MAX_CPUS 16

static char sys_kernel_hostname[64] = "unix";

/**
 * Offset tracking for /sys files
 * 
 * Reuses procfs offset tracking mechanism.
 * sysfs uses FD range 3000-3999, procfs uses 1000-1999,
 * but both can use the same tracking mechanism.
 */

/* Check if path is in /sys (mount root or under it). */
bool is_sys_path(const char *path)
{
    if (!path)
        return false;
    if (strcmp(path, "/sys") == 0 || strcmp(path, "/sys/") == 0)
        return true;
    return strncmp(path, "/sys/", 5) == 0;
}

/**
 * sys_kernel_version_read - Read kernel version from /sys/kernel/version
 * @buf: Buffer to write version string
 * @count: Size of buffer
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
int sys_kernel_version_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%s\n", IR0_VERSION_STRING ? IR0_VERSION_STRING : "unknown");
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

/* /sys/kernel/osrelease — Linux-shaped single-field release string. */
int sys_kernel_osrelease_read_reg(char *buf, size_t count)
{
    return sys_kernel_version_read_reg(buf, count);
}

/**
 * sys_kernel_hostname_read_reg - Hostname read for sysfs and pseudo_fs hooks.
 */
int sys_kernel_hostname_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%s\n", sys_kernel_hostname);
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

/**
 * sys_kernel_hostname_write_reg - Hostname write for sysfs and pseudo_fs hooks.
 */
int sys_kernel_hostname_write_reg(const char *buf, size_t count)
{
    if (!buf || count == 0)
        return 0;

    size_t copy_len = (count < sizeof(sys_kernel_hostname) - 1) ? count : (sizeof(sys_kernel_hostname) - 1);
    memcpy(sys_kernel_hostname, buf, copy_len);
    sys_kernel_hostname[copy_len] = '\0';

    while (copy_len > 0 &&
           (sys_kernel_hostname[copy_len - 1] == '\n' ||
            sys_kernel_hostname[copy_len - 1] == '\r' ||
            sys_kernel_hostname[copy_len - 1] == ' ' ||
            sys_kernel_hostname[copy_len - 1] == '\t'))
    {
        copy_len--;
        sys_kernel_hostname[copy_len] = '\0';
    }

    if (copy_len == 0)
        return -EINVAL;

    return (int)count;
}

/**
 * sys_kernel_panic_read_reg - help text for /sys/kernel/panic.
 *
 * cat shows how to use it; the actual crash happens on write. Modeled on
 * Linux /proc/sysrq-trigger (echo c) but exposed under /sys as requested.
 */
int sys_kernel_panic_read_reg(char *buf, size_t count)
{
    int len;

    if (!buf || count == 0)
        return -EINVAL;
    len = snprintf(buf, count,
                   "write any byte to force a kernel panic (test/debug)\n");
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return len;
}

/**
 * sys_kernel_panic_write_reg - force a kernel panic on demand.
 *
 * Any non-empty write triggers panicex() at TESTING level. This is the only
 * on-demand panic path (no NMI handler, no boot self-test), so it also lets
 * smokes exercise the panic banner / FB screen path. Never returns.
 */
int sys_kernel_panic_write_reg(const char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;

    panicex("forced panic via /sys/kernel/panic", TESTING,
            __FILE__, __LINE__, __func__);
    return (int)count; /* not reached */
}

/**
 * sys_max_processes - Maximum number of processes allowed
 * 
 * Default value is 1024. Can be modified via /sys/kernel/max_processes
 */
static uint32_t sys_max_processes = 1024;

/**
 * sys_kernel_max_processes_read - Read maximum processes from /sys/kernel/max_processes
 * @buf: Buffer to write value
 * @count: Size of buffer
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
int sys_kernel_max_processes_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%u\n", (unsigned)sys_max_processes);
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

/**
 * sys_kernel_max_processes_write - Write maximum processes to /sys/kernel/max_processes
 * @buf: Buffer containing new value
 * @count: Size of buffer
 * 
 * Validates and sets new maximum process limit.
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
int sys_kernel_max_processes_write_reg(const char *buf, size_t count)
{
    if (!buf || count == 0)
        return 0;
    
    /* Parse number from buffer */
    char value_buf[32];
    size_t copy_len = (count < sizeof(value_buf) - 1) ? count : (sizeof(value_buf) - 1);
    memcpy(value_buf, buf, copy_len);
    value_buf[copy_len] = '\0';
    
    /* Remove trailing whitespace */
    while (copy_len > 0 && (value_buf[copy_len - 1] == '\n' || value_buf[copy_len - 1] == '\r' || value_buf[copy_len - 1] == ' '))
    {
        copy_len--;
        value_buf[copy_len] = '\0';
    }
    
    uint32_t new_value = 0;
    for (size_t i = 0; i < copy_len; i++)
    {
        if (value_buf[i] >= '0' && value_buf[i] <= '9')
        {
            new_value = new_value * 10 + (value_buf[i] - '0');
        }
        else
        {
            return -EINVAL;  /* Invalid character */
        }
    }
    
    /* Validate range */
    if (new_value < 1 || new_value > 65535)
        return -EINVAL;
    
    sys_max_processes = new_value;
    return (int)count;
}

/**
 * sys_console_mode_read - Read console backend from /sys/console/mode
 * Returns "framebuffer WxH" or "vga" for verification
 */
int sys_console_mode_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;

    memset(buf, 0, count);

    int len;
    if (console_backend_uses_framebuffer())
    {
#if CONFIG_ENABLE_VBE
        uint32_t w, h, bpp;
        if (video_backend_get_info(&w, &h, &bpp))
            len = snprintf(buf, count, "framebuffer %ux%ux%u\n", (unsigned)w, (unsigned)h, (unsigned)bpp);
        else
            len = snprintf(buf, count, "framebuffer\n");
#else
        len = snprintf(buf, count, "framebuffer\n");
#endif
    }
    else
    {
        len = snprintf(buf, count, "vga\n");
    }
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return len;
}

static int sys_devices_cpu_online[SYS_MAX_CPUS] = { 1 };

static uint32_t sys_sysfs_cpu_count(void)
{
    uint32_t n = get_cpu_count();

    if (n == 0)
        n = 1;
    if (n > SYS_MAX_CPUS)
        n = SYS_MAX_CPUS;
    return n;
}

int sys_devices_cpu_online_count(void)
{
	uint32_t n = sys_sysfs_cpu_count();
	uint32_t i;
	int online = 0;

	for (i = 0; i < n; i++)
	{
		if (sys_devices_cpu_online[i])
			online++;
	}
	return online > 0 ? online : 1;
}

/* /sys/devices/system — one cpuN entry per logical CPU. */
int sys_devices_system_read_reg(char *buf, size_t count)
{
    uint32_t cpus;
    size_t off = 0;
    int n;

    if (!buf || count == 0)
        return -EINVAL;

    memset(buf, 0, count);
    cpus = sys_sysfs_cpu_count();
    for (uint32_t i = 0; i < cpus && off < count; i++)
    {
        n = snprintf(buf + off, (off < count) ? (count - off) : 0, "cpu%u\n", (unsigned)i);
        if (n <= 0 || n >= (int)(count - off))
            break;
        off += (size_t)n;
    }

    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/* /sys/devices/system/cpuN — lists child attributes. */
int sys_devices_cpu_read_reg(char *buf, size_t count, unsigned cpu)
{
    size_t off = 0;
    int n;

    if (!buf || count == 0)
        return -EINVAL;
    if (cpu >= sys_sysfs_cpu_count())
        return -EINVAL;

    memset(buf, 0, count);
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "online\n");
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

int sys_devices_cpu_online_read_reg(char *buf, size_t count, unsigned cpu)
{
    int len;

    if (!buf || count == 0)
        return -EINVAL;
    if (cpu >= SYS_MAX_CPUS)
        return -EINVAL;

    memset(buf, 0, count);
    len = snprintf(buf, count, "%d\n", sys_devices_cpu_online[cpu]);
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return len;
}

int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count)
{
    char value_buf[32];
    size_t copy_len;

    if (!buf || count == 0)
        return 0;
    if (cpu >= SYS_MAX_CPUS)
        return -EINVAL;

    copy_len = (count < sizeof(value_buf) - 1) ? count : (sizeof(value_buf) - 1);
    memcpy(value_buf, buf, copy_len);
    value_buf[copy_len] = '\0';

    while (copy_len > 0 && (value_buf[copy_len - 1] == '\n' ||
                            value_buf[copy_len - 1] == '\r' ||
                            value_buf[copy_len - 1] == ' '))
    {
        copy_len--;
        value_buf[copy_len] = '\0';
    }

    if (copy_len == 1 && value_buf[0] == '0')
    {
        sys_devices_cpu_online[cpu] = 0;
        return (int)count;
    }
    if (copy_len == 1 && value_buf[0] == '1')
    {
        sys_devices_cpu_online[cpu] = 1;
        return (int)count;
    }

    return -EINVAL;
}

/*
 * Generate /sys/devices/block content.
 * Lists only present ATA disks (hda, hdb, hdc, hdd) and their partitions (hdX1, ...).
 */
int sys_devices_block_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    memset(buf, 0, count);
    size_t off = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        const char *disk_name = ir0_block_legacy_name(i);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%s\n", disk_name);
        if (n <= 0 || n >= (int)(count - off))
            break;
        off += (size_t)n;
        int part_count = get_partition_count(i);
        for (int p = 0; p < part_count && off < count; p++)
        {
            partition_info_t pinfo;
            if (partition_nth_on_disk(i, (unsigned)p, &pinfo) != 0)
                continue;
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "hd%c%d\n", 'a' + (int)i, (int)pinfo.partition_number + 1);
            if (n <= 0 || n >= (int)(count - off))
                break;
            off += (size_t)n;
        }
    }
    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

int sys_kernel_build_read_reg(char *buf, size_t count)
{
	int len;

	if (!buf || count == 0)
		return -EINVAL;
	len = snprintf(buf, count, "%s %s %s@%s %s\n",
		       IR0_BUILD_DATE, IR0_BUILD_TIME, IR0_BUILD_USER,
		       IR0_BUILD_HOST, IR0_BUILD_CC);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_kernel_features_read_reg(char *buf, size_t count)
{
	int len;

	if (!buf || count == 0)
		return -EINVAL;
	/* Honest compile-time feature list — not aspirational. */
	len = snprintf(buf, count,
		       "procfs\nsysfs\nheartfs\n"
#if CONFIG_ENABLE_NETWORKING
		       "networking\n"
#endif
		       "minix\ntmpfs\n");
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

/*
 * /sys/kernel/mm — subset of /proc/meminfo (same PMM + heap sources).
 */
int sys_kernel_mm_read_reg(char *buf, size_t count)
{
	size_t total_frames = 0;
	size_t used_frames = 0;
	size_t free_frames = 0;
	size_t heap_total = 0;
	size_t heap_used = 0;
	size_t heap_allocs = 0;
	uint64_t total_kb;
	uint64_t used_kb;
	uint64_t free_kb;
	int len;

	if (!buf || count == 0)
		return -EINVAL;

	ir0_mm_pmm_stats(&total_frames, &used_frames, &free_frames);
	total_kb = ((uint64_t)total_frames * (uint64_t)IR0_MM_PAGE_SIZE) / 1024ULL;
	used_kb = ((uint64_t)used_frames * (uint64_t)IR0_MM_PAGE_SIZE) / 1024ULL;
	free_kb = ((uint64_t)free_frames * (uint64_t)IR0_MM_PAGE_SIZE) / 1024ULL;
	ir0_mm_alloc_stats(&heap_total, &heap_used, &heap_allocs);

	len = snprintf(buf, count,
		       "MemTotal:       %llu kB\n"
		       "MemFree:        %llu kB\n"
		       "MemAvailable:   %llu kB\n"
		       "MemUsed:        %llu kB\n"
		       "PmmTotalFrames: %llu\n"
		       "PmmUsedFrames:  %llu\n"
		       "PmmFreeFrames:  %llu\n"
		       "Slab:           %llu kB\n"
		       "SlabTotal:      %llu kB\n"
		       "SlabAllocs:     %llu\n"
		       "PageSize:       %u kB\n"
		       "KStackMinFree:  %llu\n"
		       "IrqNestMax:     %u\n"
		       "KStackPeak:     %llu\n",
		       (unsigned long long)total_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)used_kb,
		       (unsigned long long)(uint64_t)total_frames,
		       (unsigned long long)(uint64_t)used_frames,
		       (unsigned long long)(uint64_t)free_frames,
		       (unsigned long long)((uint64_t)heap_used / 1024ULL),
		       (unsigned long long)((uint64_t)heap_total / 1024ULL),
		       (unsigned long long)(uint64_t)heap_allocs,
		       (unsigned)(IR0_MM_PAGE_SIZE / 1024U),
		       (unsigned long long)ktm_stack_min_headroom_get(),
		       ktm_irq_nest_max_get(),
		       (unsigned long long)ktm_stack_peak_used());
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

#if CONFIG_ENABLE_NETWORKING
int sys_class_net_list_read_reg(char *buf, size_t count)
{
	struct net_device *dev;
	size_t off = 0;

	if (!buf || count == 0)
		return -EINVAL;
	memset(buf, 0, count);
	dev = net_get_devices();
	while (dev && off + 2 < count)
	{
		int n = snprintf(buf + off, count - off, "%s\n",
				 dev->name ? dev->name : "?");
		if (n <= 0)
			break;
		if ((size_t)n >= count - off)
		{
			off = count - 1;
			break;
		}
		off += (size_t)n;
		dev = dev->next;
	}
	return (int)off;
}

static struct net_device *sys_class_net_find_dev(const char *ifname)
{
	struct net_device *dev;

	if (!ifname || !ifname[0])
		return NULL;
	dev = net_get_devices();
	while (dev)
	{
		if (dev->name && strcmp(dev->name, ifname) == 0)
			return dev;
		dev = dev->next;
	}
	return NULL;
}

int sys_class_net_find_name(const char *ifname)
{
	return sys_class_net_find_dev(ifname) ? 0 : -ENOENT;
}

int sys_class_net_parse_attr(const char *attr_name)
{
	if (!attr_name)
		return -ENOENT;
	if (strcmp(attr_name, "name") == 0)
		return SYS_NET_ATTR_NAME;
	if (strcmp(attr_name, "address") == 0)
		return SYS_NET_ATTR_ADDRESS;
	if (strcmp(attr_name, "mtu") == 0)
		return SYS_NET_ATTR_MTU;
	if (strcmp(attr_name, "state") == 0)
		return SYS_NET_ATTR_STATE;
	if (strcmp(attr_name, "rx_packets") == 0)
		return SYS_NET_ATTR_RX_PACKETS;
	if (strcmp(attr_name, "tx_packets") == 0)
		return SYS_NET_ATTR_TX_PACKETS;
	if (strcmp(attr_name, "rx_bytes") == 0)
		return SYS_NET_ATTR_RX_BYTES;
	if (strcmp(attr_name, "tx_bytes") == 0)
		return SYS_NET_ATTR_TX_BYTES;
	return -ENOENT;
}

int sys_class_net_attr_read_named(char *buf, size_t count, const char *ifname,
				  unsigned attr)
{
	struct net_device *dev;
	uint64_t rxp = 0, txp = 0, rxe = 0, txe = 0;
	uint64_t rxb = 0, txb = 0;
	int len = 0;

	if (!buf || count == 0 || !ifname)
		return -EINVAL;
	memset(buf, 0, count);
	dev = sys_class_net_find_dev(ifname);
	if (!dev)
		return -ENOENT;

	if (dev->get_stats)
		dev->get_stats(dev, &rxp, &txp, &rxe, &txe);
	if (dev->get_byte_stats)
		dev->get_byte_stats(dev, &rxb, &txb);

	switch (attr)
	{
	case SYS_NET_ATTR_NAME:
		len = snprintf(buf, count, "%s\n", dev->name ? dev->name : "");
		break;
	case SYS_NET_ATTR_ADDRESS:
		len = snprintf(buf, count, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			       (unsigned)dev->mac[0], (unsigned)dev->mac[1],
			       (unsigned)dev->mac[2], (unsigned)dev->mac[3],
			       (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
		break;
	case SYS_NET_ATTR_MTU:
		len = snprintf(buf, count, "%u\n", (unsigned)dev->mtu);
		break;
	case SYS_NET_ATTR_STATE:
		len = snprintf(buf, count, "%s\n",
			       (dev->flags & IFF_UP) ? "up" : "down");
		break;
	case SYS_NET_ATTR_RX_PACKETS:
		len = snprintf(buf, count, "%llu\n", (unsigned long long)rxp);
		break;
	case SYS_NET_ATTR_TX_PACKETS:
		len = snprintf(buf, count, "%llu\n", (unsigned long long)txp);
		break;
	case SYS_NET_ATTR_RX_BYTES:
		len = snprintf(buf, count, "%llu\n", (unsigned long long)rxb);
		break;
	case SYS_NET_ATTR_TX_BYTES:
		len = snprintf(buf, count, "%llu\n", (unsigned long long)txb);
		break;
	default:
		return -ENOENT;
	}
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_class_net_attr_read_reg(char *buf, size_t count, unsigned attr)
{
	struct net_device *dev = net_get_devices();

	if (!dev || !dev->name)
		return 0;
	return sys_class_net_attr_read_named(buf, count, dev->name, attr);
}

static int sys_net_dirent_exists(struct vfs_dirent *entries, int n,
				 const char *name)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if (strcmp(entries[i].name, name) == 0)
			return 1;
	}
	return 0;
}

int sys_class_net_collect_children(const char *dir_path,
				   struct vfs_dirent *entries, int max_entries,
				   int start_n)
{
	char norm[256];
	size_t plen;
	int n = start_n;
	struct net_device *dev;
	static const char *const attrs[] = {
		"name",	      "address",    "mtu",	"state",
		"rx_packets", "tx_packets", "rx_bytes",	"tx_bytes",
	};
	unsigned i;

	if (!dir_path || !entries || max_entries <= 0 || start_n < 0)
		return -EINVAL;

	/* Normalize: strip trailing slash except root. */
	plen = strlen(dir_path);
	if (plen >= sizeof(norm))
		return -ENAMETOOLONG;
	memcpy(norm, dir_path, plen + 1);
	if (plen > 1 && norm[plen - 1] == '/')
	{
		norm[plen - 1] = '\0';
		plen--;
	}

	if (strcmp(norm, "/sys/class/net") == 0)
	{
		dev = net_get_devices();
		while (dev && n < max_entries)
		{
			if (dev->name && !sys_net_dirent_exists(entries, n, dev->name))
			{
				strncpy(entries[n].name, dev->name,
					sizeof(entries[n].name) - 1);
				entries[n].name[sizeof(entries[n].name) - 1] = '\0';
				entries[n].type = DT_DIR;
				n++;
			}
			dev = dev->next;
		}
		return n;
	}

	if (strncmp(norm, "/sys/class/net/", 15) == 0)
	{
		const char *ifname = norm + 15;
		const char *slash = strchr(ifname, '/');

		if (slash)
			return n;
		if (sys_class_net_find_dev(ifname) == NULL)
			return n;
		for (i = 0; i < sizeof(attrs) / sizeof(attrs[0]) && n < max_entries; i++)
		{
			if (sys_net_dirent_exists(entries, n, attrs[i]))
				continue;
			strncpy(entries[n].name, attrs[i],
				sizeof(entries[n].name) - 1);
			entries[n].name[sizeof(entries[n].name) - 1] = '\0';
			entries[n].type = DT_REG;
			n++;
		}
	}
	return n;
}

int sys_class_net_path_has_children(const char *path)
{
	char norm[256];
	size_t plen;
	struct vfs_dirent tmp[8];
	int n;

	if (!path)
		return 0;
	plen = strlen(path);
	if (plen >= sizeof(norm))
		return 0;
	memcpy(norm, path, plen + 1);
	if (plen > 1 && norm[plen - 1] == '/')
		norm[plen - 1] = '\0';

	n = sys_class_net_collect_children(norm, tmp, 8, 0);
	return (n > 0);
}
#else
int sys_class_net_list_read_reg(char *buf, size_t count)
{
	if (!buf || count == 0)
		return -EINVAL;
	buf[0] = '\0';
	return 0;
}

int sys_class_net_attr_read_reg(char *buf, size_t count, unsigned attr)
{
	(void)attr;
	if (!buf || count == 0)
		return -EINVAL;
	buf[0] = '\0';
	return 0;
}

int sys_class_net_attr_read_named(char *buf, size_t count, const char *ifname,
				  unsigned attr)
{
	(void)ifname;
	(void)attr;
	if (!buf || count == 0)
		return -EINVAL;
	buf[0] = '\0';
	return 0;
}

int sys_class_net_parse_attr(const char *attr_name)
{
	(void)attr_name;
	return -ENOENT;
}

int sys_class_net_find_name(const char *ifname)
{
	(void)ifname;
	return -ENOENT;
}

int sys_class_net_collect_children(const char *dir_path,
				   struct vfs_dirent *entries, int max_entries,
				   int start_n)
{
	(void)dir_path;
	(void)entries;
	(void)max_entries;
	return start_n;
}

int sys_class_net_path_has_children(const char *path)
{
	(void)path;
	return 0;
}
#endif

/* Open /sys — no virtual fds; callers must use pseudo_bind_file_fd. */
int sysfs_open(const char *path, int flags)
{
    (void)flags;

    if (!is_sys_path(path))
        return -EINVAL;

    pseudo_fs_nodes_register_all();
    return -ENOENT;
}

/* Read from /sys — LEGACY global virtual fd only; syscall uses fd_table binds. */
int sysfs_read(int fd, char *buf, size_t count, off_t offset)
{
    int64_t pr;

    if (!buf || count == 0)
        return 0;

    if (pseudo_fs_find_by_fd(fd))
    {
        pr = pseudo_fs_read_fd(fd, buf, count, offset);
        return (int)pr;
    }

    return -EBADF;
}

/* Write to /sys — LEGACY global virtual fd only; syscall uses fd_table binds. */
int sysfs_write(int fd, const char *buf, size_t count)
{
    int64_t pw;

    if (!buf || count == 0)
        return 0;

    if (pseudo_fs_find_by_fd(fd))
    {
        pw = pseudo_fs_write_fd(fd, buf, count);
        return (int)pw;
    }

    return -EBADF;
}

/* Get stat for /sys file */
int sysfs_stat(const char *path, stat_t *st)
{
    int rc;

    if (!st || !is_sys_path(path))
        return -EINVAL;

    if (strcmp(path, "/sys") == 0 || strcmp(path, "/sys/") == 0)
    {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        pseudo_fs_stat_now(st);
        return 0;
    }

    /*
     * Use exact + dynamic resolution (same as open). Do not apply prefix
     * static entries (e.g. /sys/class/net) to longer paths.
     */
    pseudo_fs_nodes_register_all();
    rc = pseudo_fs_stat_path(path, st);
    if (rc == 0)
        return 0;

    if (pseudo_fs_path_has_children(path) ||
	sys_class_net_path_has_children(path))
    {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        pseudo_fs_stat_now(st);
        return 0;
    }

    return (rc < 0) ? rc : -ENOENT;
}

int sysfs_is_virtual_subdir(const char *path)
{
    if (!path)
        return 0;
    if (strcmp(path, "/sys") == 0 || strcmp(path, "/sys/") == 0)
        return 1;
    if (pseudo_fs_path_has_children(path))
	return 1;
    return sys_class_net_path_has_children(path);
}