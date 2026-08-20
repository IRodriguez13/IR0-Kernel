/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: devfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Virtual Device Filesystem (/dev)
 * Copyright (C) 2025 Iván Rodriguez
 *
 */

#include "devfs.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/errno.h>
#include <config.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#if CONFIG_ENABLE_SOUND
#include <ir0/audio_backend.h>
#endif
#include <ir0/input_backend.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif
#include <ir0/blockdev.h>
#include <ir0/partition.h>
#include <string.h>
#include <ir0/clock.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/klog.h>
#include <ir0/serial_io.h>
#include <ir0/ipc.h>
#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif
#include <ir0/video_backend.h>
#include <ir0/copy_user.h>
#include <ir0/input.h>
#include <ir0/credentials.h>
#include <ir0/fb.h>
#include <ir0/ktm/userdev.h>
#include "vfs.h"

static pid_t devfs_current_pid(void)
{
    return ir0_current_pid();
}

/* Device registry — room for builtins + dense disk/part topology */
#define MAX_DEV_NODES 224
static devfs_node_t *dev_nodes[MAX_DEV_NODES];
static int num_dev_nodes = 0;

devfs_node_t *devfs_find_node_by_id(uint32_t device_id);

#if CONFIG_ENABLE_NETWORKING
static int dev_net_pid_has_ready_ping(pid_t pid)
{
    uint16_t id;

    if (pid <= 0)
        return 0;
    id = (uint16_t)(pid & 0xFFFF);
    return icmp_has_ready_echo_result(id) ? 1 : 0;
}
#endif

static int devfs_console_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return ir0_console_poll();
}

#if CONFIG_ENABLE_NETWORKING
static int devfs_net_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    return dev_net_pid_has_ready_ping(pid);
}
#endif

static int devfs_serial_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 0;
}

static int devfs_serial_can_write(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 1;
}

static int devfs_events0_can_write(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 0;
}

static int devfs_events0_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return ir0_input_poll() ? 1 : 0;
}

static int devfs_poll_default(int (*hook)(devfs_entry_t *, pid_t),
                              devfs_node_t *node, pid_t pid)
{
    if (node && node->ops && hook)
        return hook(&node->entry, pid);
    return 1;
}

int devfs_fd_can_read(uint32_t device_id, pid_t pid)
{
    devfs_node_t *node = devfs_find_node_by_id(device_id);

    return devfs_poll_default(
        node && node->ops ? node->ops->can_read : NULL, node, pid);
}

int devfs_fd_can_write(uint32_t device_id, pid_t pid)
{
    devfs_node_t *node = devfs_find_node_by_id(device_id);

    return devfs_poll_default(
        node && node->ops ? node->ops->can_write : NULL, node, pid);
}

#define DEVFS_DISK_AGGREGATE_ID  9
#define DEVFS_DISK_BASE_ID       20

typedef struct devfs_disk_ctx {
    uint8_t disk_id;
    uint8_t is_whole;
    uint8_t partition_number;
} devfs_disk_ctx_t;

/*
 * Resolve whole-disk vs partition extents for disk_ops (/dev/hd*).
 */
static int devfs_resolve_disk_geo(const devfs_entry_t *entry, uint8_t *disk_out,
				  partition_info_t *part_out, int *whole_out)
{
    if (!entry || entry->device_id == DEVFS_DISK_AGGREGATE_ID)
        return -ENODEV;

    devfs_disk_ctx_t *cx = entry->driver_data;
    if (!cx)
        return -ENODEV;

    *disk_out = cx->disk_id;

    if (cx->is_whole)
    {
        *whole_out = 1;
        return 0;
    }
    if (!part_out || !whole_out)
        return -EINVAL;

    if (get_partition_info(cx->disk_id, cx->partition_number, part_out) != 0)
        return -ENODEV;
    *whole_out = 0;
    return 0;
}

static int g_devfs_read_nonblock;

void devfs_set_read_nonblock(int nonblock)
{
    g_devfs_read_nonblock = nonblock ? 1 : 0;
}

int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Always returns EOF */
    return 0;
}

int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    /* Accepts all data, discards it */
    return count;
}

int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    memset(buf, 0, count);
    return count;
}

int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    return count;
}

int64_t dev_console_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
    /* Honor O_NONBLOCK from the active sys_read (see devfs_set_read_nonblock). */
    return ir0_console_read(buf, count, g_devfs_read_nonblock);
}

static int64_t dev_console_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_termios termios;
    int ret;

    (void)entry;

    /*
     * Minimal Linux VT/KD stubs for TinyX/Xfbdev (kdrive/linux/linux.c).
     * Single synthetic VT1: OPENQRY→1, GETSTATE active=1, mode get/set no-op.
     */
#define IR0_VT_OPENQRY     0x5600U
#define IR0_VT_GETMODE     0x5601U
#define IR0_VT_SETMODE     0x5602U
#define IR0_VT_GETSTATE    0x5603U
#define IR0_VT_RELDISP     0x5605U
#define IR0_VT_ACTIVATE    0x5606U
#define IR0_VT_WAITACTIVE  0x5607U
#define IR0_VT_DISALLOCATE 0x5608U
#define IR0_KDSETMODE      0x4B3AU
#define IR0_KDGETMODE      0x4B3BU
#define IR0_KDGKBMODE      0x4B44U
#define IR0_KDSKBMODE      0x4B45U
#define IR0_KDSETLED       0x4B32U
#define IR0_KDMKTONE       0x4B30U

    if (request == IR0_VT_OPENQRY)
    {
        int vtno = 1;

        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &vtno, sizeof(vtno)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_VT_GETSTATE)
    {
        struct
        {
            uint16_t v_active;
            uint16_t v_signal;
            uint16_t v_state;
        } st;

        if (!arg)
            return -EINVAL;
        st.v_active = 1;
        st.v_signal = 0;
        st.v_state = (uint16_t)(1U << 1); /* VT1 occupied */
        if (copy_to_user(arg, &st, sizeof(st)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_VT_GETMODE)
    {
        struct
        {
            char mode;
            char waitv;
            int16_t relsig;
            int16_t acqsig;
            int16_t frsig;
        } mode;

        if (!arg)
            return -EINVAL;
        memset(&mode, 0, sizeof(mode));
        mode.mode = 0; /* VT_AUTO */
        if (copy_to_user(arg, &mode, sizeof(mode)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_VT_SETMODE || request == IR0_VT_ACTIVATE ||
        request == IR0_VT_WAITACTIVE || request == IR0_VT_DISALLOCATE ||
        request == IR0_VT_RELDISP || request == IR0_KDSETMODE ||
        request == IR0_KDSKBMODE || request == IR0_KDSETLED ||
        request == IR0_KDMKTONE)
        return 0;

    if (request == IR0_KDGKBMODE)
    {
        int kbmode = 0; /* K_RAW-compatible default; TinyX restores later */

        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &kbmode, sizeof(kbmode)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_KDGETMODE)
    {
        int kdmode = 0; /* KD_TEXT */

        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &kdmode, sizeof(kdmode)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_CONSOLE_TCGETS)
    {
        if (!arg)
            return -EINVAL;
        ret = tty_ioctl_termios_kernel(IR0_CONSOLE_TCGETS, &termios);
        if (ret != 0)
            return ret;
        if (copy_to_user(arg, &termios, sizeof(termios)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_CONSOLE_TCSETS ||
        request == IR0_CONSOLE_TCSETSW ||
        request == IR0_CONSOLE_TCSETSF)
    {
        if (!arg)
            return -EINVAL;
        if (copy_from_user(&termios, arg, sizeof(termios)) != 0)
            return -EFAULT;
        return tty_ioctl_termios_kernel(request, &termios);
    }

    if (request == IR0_CONSOLE_TIOCGWINSZ)
        return ir0_console_ioctl_winsize(arg);
    if (request == IR0_CONSOLE_TIOCSWINSZ)
        return ir0_console_ioctl_winsize_set(arg);

    if (request == IR0_CONSOLE_TCFLSH)
    {
        /* 0=IFLUSH, 1=OFLUSH, 2=IOFLUSH — input-only console. */
        tty_flush_input();
        return 0;
    }

    if (request == IR0_CONSOLE_FIONREAD)
    {
        int avail;

        if (!arg)
            return -EINVAL;
        avail = tty_input_bytes_available();
        if (copy_to_user(arg, &avail, sizeof(avail)) != 0)
            return -EFAULT;
        return 0;
    }

    /*
     * Job-control ioctls on /dev/console. Without these, BusyBox ash may
     * disable job control; stubs keep interactive read path alive.
     */
    if (request == IR0_TIOCSCTTY)
	return 0;
    if (request == IR0_TIOCSPGRP)
    {
	pid_t pg;

	if (!arg)
	    return -EINVAL;
	if (copy_from_user(&pg, arg, sizeof(pg)) != 0)
	    return -EFAULT;
	return ir0_console_set_fg_pgid((int32_t)pg);
    }
    if (request == IR0_TIOCGPGRP)
    {
	pid_t pg;

	if (!arg)
	    return -EINVAL;
	pg = (pid_t)ir0_console_get_fg_pgid();
	if (copy_to_user(arg, &pg, sizeof(pg)) != 0)
	    return -EFAULT;
	return 0;
    }

    return -ENOTTY;
}

static void devfs_console_diag_once(devfs_entry_t *entry)
{
    static int diag_done;

    if (diag_done)
        return;
    diag_done = 1;

    klog_smoke("DEV_CONSOLE_NODE_OK");
    klog_info_fmt("DEVFS", "CONSOLE name=%s device_id=0x%x ops=console",
		  entry && entry->name ? entry->name : "(null)",
		  entry ? (unsigned)entry->device_id : 0u);
    klog_smoke("DEV_CONSOLE_OPEN_OK");
}

static int64_t dev_console_open(devfs_entry_t *entry, int flags)
{
    (void)flags;
    devfs_console_diag_once(entry);
    ir0_console_on_userspace_attach();
    return 0;
}

int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
    return ir0_console_write(buf, count, 0x07);
}

int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    char message[97];
    size_t n;

    (void)entry;
    (void)offset;
    if (!buf)
        return -EINVAL;
    n = count < sizeof(message) - 1 ? count : sizeof(message) - 1;
    memcpy(message, buf, n);
    message[n] = '\0';
    while (n > 0 && (message[n - 1] == '\n' || message[n - 1] == '\r'))
        message[--n] = '\0';
    klog_info("USER", message);
    return (int64_t)count;
}

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
	/*
	 * Legacy ops path — preferred open path uses per-open text snap.
	 * Keep bounce+slice here so a direct ops->read still reaches EOF.
	 */
	char full[4096];
	int64_t n;

	(void)entry;
	n = (int64_t)klog_read_records(full, sizeof(full));
	if (n < 0)
		return n;
	if (offset < 0)
		return -EINVAL;
	if ((uint64_t)offset >= (uint64_t)n)
		return 0;
	{
		size_t avail = (size_t)n - (size_t)offset;
		size_t to_copy = avail < count ? avail : count;

		if (!buf)
			return -EFAULT;
		memcpy(buf, full + (size_t)offset, to_copy);
		return (int64_t)to_copy;
	}
}

#if CONFIG_ENABLE_SOUND
/*
 * PCM format state for /dev/audio. Default: Doom-compatible 11025 Hz, 8-bit mono.
 * Use ioctl(AUDIO_SET_FORMAT) before write() to change.
 */
static struct audio_format audio_pcm_format = {
    .sample_rate = 11025,
    .channels = 1,
    .bits_per_sample = 8
};
#endif

int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
#if CONFIG_ENABLE_SOUND
    if (!audio_backend_is_available())
    {
        /* Sound Blaster not available, accept data but don't process */
        return (int64_t)count;
    }
    if (!buf || count == 0)
        return (int64_t)count;

    return (int64_t)audio_backend_play_pcm(buf, count,
                                           audio_pcm_format.sample_rate,
                                           audio_pcm_format.channels,
                                           audio_pcm_format.bits_per_sample);
#else
    (void)buf; (void)count;
    return -ENODEV;
#endif
}

int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
#if CONFIG_ENABLE_SOUND
    if (!audio_backend_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case AUDIO_SET_VOLUME:
            if (arg)
            {
                uint8_t volume = *(uint8_t *)arg;
                if (volume > 100)
                    volume = 100;  /* Clamp to 0-100 */
                /* Convert 0-100 to 0x00-0xFF mixer value */
                uint8_t mixer_vol = (volume * 255) / 100;
                audio_backend_set_master_volume(mixer_vol);
                return 0;
            }
            return -1;
            
        case AUDIO_GET_VOLUME:
            if (arg)
            {
                uint8_t mixer_vol = audio_backend_get_master_volume();
                /* Mixer value format: bits 7-4 = left, bits 3-0 = right */
                uint8_t left = (mixer_vol >> 4) & 0x0F;
                uint8_t right = mixer_vol & 0x0F;
                uint8_t avg = (left + right) / 2;
                uint8_t volume = (avg * 100) / 15;
                *(uint8_t *)arg = volume;
                return 0;
            }
            return -1;
            
        case AUDIO_PLAY:
            audio_backend_speaker_on();
            return 0;
            
        case AUDIO_STOP:
            audio_backend_speaker_off();
            return 0;
            
        case AUDIO_SET_FORMAT:
            if (arg)
            {
                struct audio_format fmt;
                if (copy_from_user(&fmt, arg, sizeof(fmt)) != 0)
                    return -1;
                /* SB16 8-bit mono: 4000-45454 Hz. Doom uses 11025. */
                if (fmt.sample_rate < 4000 || fmt.sample_rate > 45454)
                    return -1;
                if (fmt.channels != 1 || fmt.bits_per_sample != 8)
                    return -1;  /* 16-bit/stereo not yet implemented */
                audio_pcm_format = fmt;
                return 0;
            }
            return -1;
            
        case AUDIO_GET_FORMAT:
            if (arg)
            {
                if (copy_to_user(arg, &audio_pcm_format, sizeof(audio_pcm_format)) != 0)
                    return -1;
                return 0;
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
#else
    (void)request; (void)arg;
    return -ENODEV;
#endif
}

int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Audio input not implemented yet */
    return 0;
}

#if CONFIG_ENABLE_MOUSE
static int dev_mouse_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    /*
     * Absolute (x,y,buttons) ioctl device — not a PS/2 byte stream.
     * Always-ready + 12-byte reads made TinyX MouseRead overrun
     * event[MAX_MOUSE] (stack canary → abort).
     */
    return 0;
}
#endif

int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_MOUSE
    int mouse_data[3];
    ir0_mouse_state_t st;

    (void)entry;
    (void)offset;
    /*
     * Legacy absolute-state ABI for IR0 tools. Never pretend to be a
     * streaming PS/2 mouse: return 0 so poll/read clients idle.
     * Use ioctl(MOUSE_GET_STATE) or /dev/input/event0 for input.
     */
    if (count < sizeof(mouse_data))
        return 0;
    if (!input_mouse_get_state(&st))
        return 0;
    (void)buf;
    (void)mouse_data;
    (void)st;
    return 0;
#else
    (void)entry; (void)buf; (void)count; (void)offset;
    return -ENODEV;
#endif
}

int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
#if CONFIG_ENABLE_MOUSE
    (void)entry;
    
    if (!input_mouse_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case MOUSE_GET_STATE:
            if (arg)
            {
                ir0_mouse_state_t st;
                if (input_mouse_get_state(&st))
                {
                    ir0_mouse_state_t *out = (ir0_mouse_state_t *)arg;
                    *out = st;  /* Copy state */
                    return 0;
                }
            }
            return -1;
            
        case MOUSE_SET_SENSITIVITY:
            if (arg)
            {
                uint8_t sensitivity = *(uint8_t *)arg;
                /* Sensitivity maps to sample rate: higher = more sensitive */
                /* Typical range: 10-200 samples/sec, default 100 */
                if (sensitivity < 10)
                    sensitivity = 10;
                if (sensitivity > 200)
                    sensitivity = 200;
                if (input_mouse_set_sensitivity(sensitivity))
                {
                    return 0;
                }
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
#else
    (void)entry; (void)request; (void)arg;
    return -ENODEV;
#endif
}

int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_NETWORKING
    (void)entry; (void)offset;
    if (!buf || count == 0)
        return -EINVAL;

    /*
     * write(2) buffers are not guaranteed to be null-terminated.
     * Always copy and terminate before parsing as command text.
     */
    char cmd_local[256];
    size_t copy_len = (count < sizeof(cmd_local) - 1) ? count : (sizeof(cmd_local) - 1);
    memcpy(cmd_local, buf, copy_len);
    cmd_local[copy_len] = '\0';
    const char *cmd = cmd_local;
    
    /* Parse network commands (ping, ifconfig, etc.) */
    if (strncmp(cmd, "ping ", 5) == 0)
    {
        /* Parse IP address or hostname from command */
        const char *host_str = cmd + 5;
        while (*host_str == ' ' || *host_str == '\t')
            host_str++;
        
        /* Extract hostname/IP string (terminate at whitespace) */
        char hostname[256];
        size_t hostname_len = 0;
        const char *p = host_str;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && hostname_len < sizeof(hostname) - 1)
        {
            hostname[hostname_len++] = *p++;
        }
        hostname[hostname_len] = '\0';
        
        /* Try to parse as IP address first */
        ip4_addr_t dest_ip = 0;
        {
            uint8_t octets[4] = {0, 0, 0, 0};
            int octet_idx = 0;
            int value = 0;
            p = hostname;
            
            while (*p && octet_idx < 4)
            {
                if (*p >= '0' && *p <= '9')
                {
                    value = value * 10 + (*p - '0');
                    if (value > 255)
                        break;
                }
                else if (*p == '.')
                {
                    if (octet_idx >= 4)
                        break;
                    octets[octet_idx++] = (uint8_t)value;
                    value = 0;
                }
                else
                {
                    /* Not an IP, might be a hostname */
                    dest_ip = 0;
                    break;
                }
                p++;
            }
            
            if (octet_idx == 3 && value <= 255 && *p == '\0')
            {
                octets[octet_idx] = (uint8_t)value;
                dest_ip = htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
            }
        }
        
        /* If not an IP address, try DNS resolution */
        if (dest_ip == 0)
        {
            
            /*
             * Prefer runtime DNS from stack configuration (DHCP can update it).
             * Keep protocol-specific fallbacks for static configurations.
             */
            ip4_addr_t dns_server = dns_get_default_server();
            if (dns_server == 0)
            {
#ifdef IR0_TAP_NETWORKING
                dns_server = htonl((8U << 24) | (8U << 16) | (8U << 8) | 8U); /* 8.8.8.8 */
#else
                dns_server = htonl((10U << 24) | (0U << 16) | (2U << 8) | 3U); /* 10.0.2.3 */
#endif
            }
            {
                uint32_t dns_h = ntohl(dns_server);
                LOG_INFO_FMT("DEVNET", "Attempting DNS resolution for '%s' using DNS server %d.%d.%d.%d",
                             hostname,
                             (int)((dns_h >> 24) & 0xFF),
                             (int)((dns_h >> 16) & 0xFF),
                             (int)((dns_h >> 8) & 0xFF),
                             (int)(dns_h & 0xFF));
            }
            
            /* In QEMU user-mode, try gateway first (10.0.2.2) as it might forward DNS */
            if (ip_gateway != 0)
            {
                LOG_INFO_FMT("DEVNET", "Trying DNS via gateway first");
                dest_ip = dns_resolve(hostname, ip_gateway);
                if (dest_ip == 0)
                {
                    LOG_INFO_FMT("DEVNET", "Gateway DNS failed, trying direct DNS server");
                    dest_ip = dns_resolve(hostname, dns_server);
                }
                else
                {
                    LOG_INFO_FMT("DEVNET", "DNS resolution via gateway successful");
                }
            }
            else
            {
                LOG_INFO_FMT("DEVNET", "No gateway, using direct DNS server");
                dest_ip = dns_resolve(hostname, dns_server);
            }
            
            if (dest_ip == 0)
            {
                /* DNS resolution failed */
                LOG_INFO_FMT("DEVNET", "DNS resolution failed for '%s'", hostname);
                return -EIO;
            }
            else
            {
                LOG_INFO_FMT("DEVNET", "DNS resolution successful: '%s' -> resolved IP", hostname);
            }
        }
        
        /* Send ping via ioctl */
        {
            int64_t rc = dev_net_ioctl(entry, NET_SEND_PING, &dest_ip);
            return (rc < 0) ? rc : (int64_t)count;
        }
    }
    else if (strncmp(cmd, "dhcp", 4) == 0 &&
             (cmd[4] == '\0' || cmd[4] == '\n' || cmd[4] == '\r' ||
              cmd[4] == ' ' || cmd[4] == '\t'))
    {
        /*
         * Trigger DHCP only on explicit request from userspace/debug shell.
         * Boot keeps static network defaults unless this command is invoked.
         */
        int ret = net_stack_post_irq_init();
        if (ret < 0)
            return ret;
        return (int64_t)count;
    }
    else if (strncmp(cmd, "ifconfig", 8) == 0)
    {
        /* Parse ifconfig command: "ifconfig <ip> [netmask] [gateway]" */
        const char *config_str = cmd + 8;  /* Skip "ifconfig" */
        while (*config_str == ' ' || *config_str == '\t')
            config_str++;
        
        if (*config_str == '\0' || *config_str == '\n')
        {
            /* No arguments: show current config via ioctl */
            typedef struct {
                ip4_addr_t *ip;
                ip4_addr_t *netmask;
                ip4_addr_t *gateway;
            } net_config_t;
            
            ip4_addr_t ip, netmask, gateway;
            net_config_t config = { &ip, &netmask, &gateway };
            
            if (dev_net_ioctl(entry, NET_GET_CONFIG, &config) == 0)
            {
                /* Format and display configuration */
                char buf[256];
                
                /* Format IP addresses */
                uint32_t ip_h = ntohl(ip);
                uint32_t netmask_h = ntohl(netmask);
                uint32_t gateway_h = ntohl(gateway);
                
                snprintf(buf, sizeof(buf), 
                        "IP: %d.%d.%d.%d\n"
                        "Netmask: %d.%d.%d.%d\n"
                        "Gateway: %d.%d.%d.%d\n",
                        (int)((ip_h >> 24) & 0xFF), (int)((ip_h >> 16) & 0xFF),
                        (int)((ip_h >> 8) & 0xFF), (int)(ip_h & 0xFF),
                        (int)((netmask_h >> 24) & 0xFF), (int)((netmask_h >> 16) & 0xFF),
                        (int)((netmask_h >> 8) & 0xFF), (int)(netmask_h & 0xFF),
                        (int)((gateway_h >> 24) & 0xFF), (int)((gateway_h >> 16) & 0xFF),
                        (int)((gateway_h >> 8) & 0xFF), (int)(gateway_h & 0xFF));
                
                /* Write to stdout */
                console_backend_write(buf, strlen(buf), 0x0F);
            }
        }
        else
        {
            /* Parse IP, netmask, gateway */
            char config_copy[256];
            size_t i = 0;
            const char *p = config_str;
            while (i < sizeof(config_copy) - 1 && *p && *p != '\n' && *p != '\r')
                config_copy[i++] = *p++;
            config_copy[i] = '\0';
            
            /* Parse IP address */
            char *ip_str = config_copy;
            char *netmask_str = NULL;
            char *gateway_str = NULL;
            
            /* Find netmask */
            char *q = ip_str;
            while (*q && *q != ' ' && *q != '\t')
                q++;
            if (*q)
            {
                *q++ = '\0';
                netmask_str = q;
                while (*netmask_str == ' ' || *netmask_str == '\t')
                    netmask_str++;
                
                /* Find gateway */
                q = netmask_str;
                while (*q && *q != ' ' && *q != '\t')
                    q++;
                if (*q)
                {
                    *q++ = '\0';
                    gateway_str = q;
                    while (*gateway_str == ' ' || *gateway_str == '\t')
                        gateway_str++;
                    if (*gateway_str == '\0')
                        gateway_str = NULL;
                }
                
                /* Check if netmask is empty */
                if (netmask_str[0] == '\0')
                    netmask_str = NULL;
            }
            
            /* Parse IP addresses */
            uint8_t ip_octets[4] = {0};
            uint8_t netmask_octets[4] = {0};
            uint8_t gateway_octets[4] = {0};
            
            /* Parse IP */
            int octet_idx = 0;
            int value = 0;
            const char *parse_ptr = ip_str;
            while (*parse_ptr && octet_idx < 4)
            {
                if (*parse_ptr >= '0' && *parse_ptr <= '9')
                {
                    value = value * 10 + (*parse_ptr - '0');
                    if (value > 255)
                        return -EINVAL;
                }
                else if (*parse_ptr == '.')
                {
                    ip_octets[octet_idx++] = (uint8_t)value;
                    value = 0;
                }
                else
                    return -EINVAL;
                parse_ptr++;
            }
            if (octet_idx == 3)
                ip_octets[octet_idx] = (uint8_t)value;
            else
                return -EINVAL;
            
            ip4_addr_t new_ip = htonl((ip_octets[0] << 24) | (ip_octets[1] << 16) | 
                                      (ip_octets[2] << 8) | ip_octets[3]);
            ip4_addr_t new_netmask = 0;
            ip4_addr_t new_gateway = 0;
            
            /* Parse netmask if provided */
            if (netmask_str)
            {
                octet_idx = 0;
                value = 0;
                parse_ptr = netmask_str;
                while (*parse_ptr && octet_idx < 4)
                {
                    if (*parse_ptr >= '0' && *parse_ptr <= '9')
                    {
                        value = value * 10 + (*parse_ptr - '0');
                        if (value > 255)
                            return -EINVAL;
                    }
                    else if (*parse_ptr == '.')
                    {
                        netmask_octets[octet_idx++] = (uint8_t)value;
                        value = 0;
                    }
                    else
                        return -EINVAL;
                    parse_ptr++;
                }
                if (octet_idx == 3)
                    netmask_octets[octet_idx] = (uint8_t)value;
                else
                    return -EINVAL;
                
                new_netmask = htonl((netmask_octets[0] << 24) | (netmask_octets[1] << 16) | 
                                    (netmask_octets[2] << 8) | netmask_octets[3]);
            }
            
            /* Parse gateway if provided */
            if (gateway_str)
            {
                octet_idx = 0;
                value = 0;
                parse_ptr = gateway_str;
                while (*parse_ptr && octet_idx < 4)
                {
                    if (*parse_ptr >= '0' && *parse_ptr <= '9')
                    {
                        value = value * 10 + (*parse_ptr - '0');
                        if (value > 255)
                            return -EINVAL;
                    }
                    else if (*parse_ptr == '.')
                    {
                        gateway_octets[octet_idx++] = (uint8_t)value;
                        value = 0;
                    }
                    else
                        return -EINVAL;
                    parse_ptr++;
                }
                if (octet_idx == 3)
                    gateway_octets[octet_idx] = (uint8_t)value;
                else
                    return -EINVAL;
                
                new_gateway = htonl((gateway_octets[0] << 24) | (gateway_octets[1] << 16) | 
                                    (gateway_octets[2] << 8) | gateway_octets[3]);
            }
            
            /* Set configuration via ioctl */
            typedef struct {
                ip4_addr_t ip;
                ip4_addr_t netmask;
                ip4_addr_t gateway;
            } net_set_config_t;
            
            net_set_config_t config = {
                .ip = new_ip,
                .netmask = new_netmask,
                .gateway = new_gateway
            };
            
            return dev_net_ioctl(entry, NET_SET_CONFIG, &config);
        }
        return (int64_t)count;
    }
    
    return -EINVAL;
#else
    (void)entry; (void)buf; (void)count; (void)offset;
    return -ENODEV;
#endif
}

#if CONFIG_ENABLE_NETWORKING
/*
 * Build one /dev/net text payload into @out (ping_result or snapshot).
 * Returns byte length (not including a forced NUL).
 */
static size_t dev_net_build_text(char *out, size_t out_len)
{
	size_t off = 0;
	int n;
	pid_t pid;
	uint16_t id;
	uint16_t seq = 0;
	uint64_t rtt = 0;
	uint8_t ttl = 0;
	size_t payload_bytes = 0;
	ip4_addr_t reply_ip = 0;
	struct net_device *dev;

	if (!out || out_len == 0)
		return 0;

	net_poll();
	pid = devfs_current_pid();
	id = (uint16_t)(pid & 0xFFFF);

	if (icmp_get_next_echo_result(id, &seq, &rtt, &ttl, &payload_bytes, &reply_ip))
	{
		uint32_t ip = ntohl(reply_ip);

		n = snprintf(out, out_len,
			     "type=ping_result\nsuccess=1\nseq=%u\nrtt_ms=%u\nttl=%u\npayload_bytes=%u\nip=%u.%u.%u.%u\n",
			     (unsigned)seq, (unsigned)rtt, (unsigned)ttl,
			     (unsigned)payload_bytes,
			     (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
			     (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
		if (n <= 0)
			return 0;
		if ((size_t)n >= out_len)
			return out_len - 1;
		return (size_t)n;
	}

	n = snprintf(out + off, out_len - off, "type=snapshot\n");
	if (n > 0 && (size_t)n < out_len - off)
		off += (size_t)n;
	n = snprintf(out + off, out_len - off, "iface\tmtu\tflags\tmac\n");
	if (n > 0 && (size_t)n < out_len - off)
		off += (size_t)n;

	dev = net_get_devices();
	while (dev && off < out_len - 1)
	{
		char flags[32];
		size_t foff = 0;

		flags[0] = '\0';
		if (dev->flags & IFF_UP)
			foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "UP");
		if (dev->flags & IFF_RUNNING)
			foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff,
						 "%sRUNNING", foff ? "," : "");
		if (dev->flags & IFF_BROADCAST)
			foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff,
						 "%sBROADCAST", foff ? "," : "");
		if (foff == 0)
			snprintf(flags, sizeof(flags), "-");

		n = snprintf(out + off, out_len - off,
			     "%s\t%u\t%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
			     dev->name ? dev->name : "", (unsigned)dev->mtu, flags,
			     (unsigned)dev->mac[0], (unsigned)dev->mac[1],
			     (unsigned)dev->mac[2], (unsigned)dev->mac[3],
			     (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
		if (n <= 0 || (size_t)n >= out_len - off)
			break;
		off += (size_t)n;
		dev = dev->next;
	}

	if (off < out_len)
	{
		uint32_t ip_h = ntohl(ip_local_addr);
		uint32_t nm_h = ntohl(ip_netmask);
		uint32_t gw_h = ntohl(ip_gateway);

		n = snprintf(out + off, out_len - off,
			     "ip=%u.%u.%u.%u\nnetmask=%u.%u.%u.%u\ngateway=%u.%u.%u.%u\n",
			     (unsigned)((ip_h >> 24) & 0xFF), (unsigned)((ip_h >> 16) & 0xFF),
			     (unsigned)((ip_h >> 8) & 0xFF), (unsigned)(ip_h & 0xFF),
			     (unsigned)((nm_h >> 24) & 0xFF), (unsigned)((nm_h >> 16) & 0xFF),
			     (unsigned)((nm_h >> 8) & 0xFF), (unsigned)(nm_h & 0xFF),
			     (unsigned)((gw_h >> 24) & 0xFF), (unsigned)((gw_h >> 16) & 0xFF),
			     (unsigned)((gw_h >> 8) & 0xFF), (unsigned)(gw_h & 0xFF));
		if (n > 0 && (size_t)n < out_len - off)
			off += (size_t)n;
	}

	return off;
}
#endif /* CONFIG_ENABLE_NETWORKING */

int devfs_node_wants_text_snap(uint32_t device_id)
{
	return device_id == DEVFS_ID_NET || device_id == DEVFS_ID_KMSG;
}

int64_t devfs_text_snap_read(const devfs_text_snap_t *snap, void *buf,
			     size_t count, off_t offset)
{
	size_t avail;
	size_t to_copy;

	if (!snap || !snap->buf)
		return -EINVAL;
	if (!buf)
		return -EFAULT;
	if (offset < 0)
		return -EINVAL;
	if ((uint64_t)offset >= (uint64_t)snap->len)
		return 0;
	avail = snap->len - (size_t)offset;
	to_copy = avail < count ? avail : count;
	memcpy(buf, snap->buf + (size_t)offset, to_copy);
	return (int64_t)to_copy;
}

void devfs_text_snap_acquire(devfs_text_snap_t *snap)
{
	if (snap)
		snap->refs++;
}

void devfs_text_snap_release(devfs_text_snap_t *snap)
{
	if (!snap)
		return;
	if (snap->refs > 0)
		snap->refs--;
	if (snap->refs == 0)
	{
		if (snap->buf)
			kfree(snap->buf);
		kfree(snap);
	}
}

devfs_text_snap_t *devfs_text_snap_capture(uint32_t device_id)
{
	devfs_text_snap_t *snap;
	char tmp[2048];
	size_t len = 0;

	if (device_id == DEVFS_ID_KMSG)
	{
		int n = klog_read_records(tmp, sizeof(tmp));

		if (n < 0)
			return NULL;
		len = (size_t)n;
	}
#if CONFIG_ENABLE_NETWORKING
	else if (device_id == DEVFS_ID_NET)
		len = dev_net_build_text(tmp, sizeof(tmp));
#endif
	else
		return NULL;

	snap = (devfs_text_snap_t *)kmalloc(sizeof(*snap));
	if (!snap)
		return NULL;
	snap->buf = (char *)kmalloc(len + 1);
	if (!snap->buf)
	{
		kfree(snap);
		return NULL;
	}
	memcpy(snap->buf, tmp, len);
	snap->buf[len] = '\0';
	snap->len = len;
	snap->refs = 1;
	return snap;
}

int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_NETWORKING
	char full[2048];
	size_t len;
	size_t avail;
	size_t to_copy;

	(void)entry;
	if (!buf)
		return -EFAULT;
	if (count == 0)
		return 0;
	if (offset < 0)
		return -EINVAL;

	/*
	 * Bounce+slice fallback when no per-open snap is attached (ops path).
	 * Preferred: open captures snap; sys_read uses devfs_text_snap_read.
	 */
	len = dev_net_build_text(full, sizeof(full));
	if ((uint64_t)offset >= (uint64_t)len)
		return 0;
	avail = len - (size_t)offset;
	to_copy = avail < count ? avail : count;
	memcpy(buf, full + (size_t)offset, to_copy);
	return (int64_t)to_copy;
#else
	(void)entry;
	(void)buf;
	(void)count;
	(void)offset;
	return -ENODEV;
#endif
}

int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
#if CONFIG_ENABLE_NETWORKING
    (void)entry;
    
    /* Network device may not be available, return -1 if needed */
    
    switch (request)
    {
        case NET_SEND_PING:
            if (arg)
            {
                /* arg points to ip4_addr_t */
                ip4_addr_t dest_ip = *(ip4_addr_t *)arg;
                struct net_device *dev = net_get_devices();
                
                if (dev)
                {
                    
                    /* Use process ID as identifier and a monotonic sequence. */
                    pid_t pid = devfs_current_pid();
                    uint16_t id = (uint16_t)(pid & 0xFFFF);
                    uint16_t seq = icmp_allocate_echo_seq();
                    
                    int ret = icmp_send_echo_request(dev, dest_ip, id, seq, NULL, 0);
                    return (ret == 0) ? 0 : -EIO;
                }
                return -ENODEV;
            }
            return -EINVAL;
            
        case NET_GET_CONFIG:
            if (arg)
            {
                /* arg points to: { ip4_addr_t *ip; ip4_addr_t *netmask; ip4_addr_t *gateway; } */
                typedef struct {
                    ip4_addr_t *ip;
                    ip4_addr_t *netmask;
                    ip4_addr_t *gateway;
                } net_config_t;
                
                net_config_t *config = (net_config_t *)arg;
                if (config)
                {
                    if (config->ip)
                        *config->ip = ip_local_addr;
                    if (config->netmask)
                        *config->netmask = ip_netmask;
                    if (config->gateway)
                        *config->gateway = ip_gateway;
                    return 0;
                }
            }
            return -EINVAL;
            
        case NET_SET_CONFIG:
            if (arg)
            {
                /* arg points to: { ip4_addr_t ip; ip4_addr_t netmask; ip4_addr_t gateway; } */
                typedef struct {
                    ip4_addr_t ip;
                    ip4_addr_t netmask;
                    ip4_addr_t gateway;
                } net_config_t;
                
                net_config_t *config = (net_config_t *)arg;
                if (config)
                {
                    ip_local_addr = config->ip;
                    ip_netmask = config->netmask;
                    ip_gateway = config->gateway;
                    arp_set_my_ip(config->ip);  /* Update ARP cache */
                    {
                        struct net_device *dev = net_get_devices();
                        while (dev)
                        {
                            arp_set_interface_ip(dev, config->ip);
                            dev = dev->next;
                        }
                    }
                    (void)ip_routes_seed_from_globals();
                    return 0;
                }
            }
            return -EINVAL;
            
        case NET_GET_PING_RESULT:
            if (arg)
            {
                /* arg points to: { int success; uint64_t rtt; uint8_t ttl; size_t payload_bytes; ip4_addr_t reply_ip; } */
                struct ping_result *result = (struct ping_result *)arg;
                if (!result)
                    return -EINVAL;
                
                /* Get PID to use as ICMP ID (matches NET_SEND_PING behavior) */
                pid_t pid = devfs_current_pid();
                uint16_t id = (uint16_t)(pid & 0xFFFF);

                /* Try to get next completed echo result for this pid. */
                if (icmp_get_next_echo_result(id, &result->seq, &result->rtt, &result->ttl,
                                              &result->payload_bytes, &result->reply_ip))
                {
                    result->success = 1;
                    return 0;
                }
                else
                {
                    result->success = 0;
                    result->seq = 0;
                    return 0;  /* Still pending, but not an error */
                }
            }
            return -EINVAL;
            
        default:
            return -ENOTTY;  /* Invalid request */
    }
#else
    (void)entry; (void)request; (void)arg;
    return -ENODEV;
#endif
}

int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    if (!buf)
        return -1;
    if (entry->device_id != DEVFS_DISK_AGGREGATE_ID)
    {
        uint8_t disk_id;
        partition_info_t pinfo;
        int is_whole = 0;

        if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
            return -ENODEV;
        const char *disk_name = ir0_block_legacy_name(disk_id);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            return -ENODEV;
        if (offset < 0)
            return -1;
        uint64_t sector_off = (uint64_t)((unsigned long)offset / 512);
        size_t num_sectors = (count + 511) / 512;
        if (num_sectors == 0)
            return 0;
        uint64_t start_lba = sector_off;
        if (!is_whole)
        {
            if (sector_off >= pinfo.total_sectors)
                return 0;
            start_lba = pinfo.start_lba + sector_off;
            if (num_sectors > pinfo.total_sectors - sector_off)
                num_sectors = (size_t)(pinfo.total_sectors - sector_off);
        }
        else
        {
            uint64_t disk_sectors = ir0_block_sector_count_by_name(disk_name);
            if (sector_off >= disk_sectors)
                return 0;
            if (sector_off + num_sectors > disk_sectors)
                num_sectors = (size_t)(disk_sectors - sector_off);
        }
        char *dst = (char *)buf;
        size_t bytes_done = 0;
        while (num_sectors > 0)
        {
            uint8_t n = (num_sectors > 255) ? 255 : (uint8_t)num_sectors;

            /* cat /dev/hda must be interruptible (Ctrl+C → EINTR). */
            if (current_process &&
                signals_pause_should_interrupt(current_process))
            {
                handle_signals();
                if (bytes_done > 0)
                    return (int64_t)bytes_done;
                return -EINTR;
            }
            if (ir0_block_read_by_name(disk_name, (uint32_t)start_lba, n, dst))
                return (int64_t)bytes_done;
            bytes_done += (size_t)n * 512;
            start_lba += n;
            num_sectors -= n;
            dst += (size_t)n * 512;
        }
        return (int64_t)((bytes_done < count) ? bytes_done : count);
    }
    /* Aggregate /dev/disk: generate listing */
    
    /* Generate df-like output: Filesystem          Size */
    /* Support offset for seeking within output */
    char output[1024];
    size_t output_len = 0;
    
    /* Validate offset */
    if (offset < 0)
        return -1;
    
    int n = snprintf(output + output_len, sizeof(output) - output_len,
                     "Filesystem          Size\n");
    if (n > 0 && (size_t)n < sizeof(output) - output_len)
        output_len += (size_t)n;
    
    n = snprintf(output + output_len, sizeof(output) - output_len,
                 "----------------------------------\n");
    if (n > 0 && (size_t)n < sizeof(output) - output_len)
        output_len += (size_t)n;
    
    int found_drives = 0;
    int total_devs = ir0_block_count();
    for (int i = 0; i < total_devs; i++)
    {
        const char *disk_name = ir0_block_name_at(i);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;
        
        found_drives++;
        char devname[16];
        int len = snprintf(devname, sizeof(devname), "/dev/%s", disk_name);
        if (len < 0 || len >= (int)sizeof(devname))
            continue;
        
        /* ir0_block_sector_count_by_name() returns size in 512-byte sectors */
        uint64_t size = ir0_block_sector_count_by_name(disk_name);
        if (size == 0)
        {
            n = snprintf(output + output_len, sizeof(output) - output_len,
                        "%-20s (empty)\n", devname);
        }
        else
        {
            char size_str[32];
            /* Same calculation: sectors / (2 * 1024 * 1024) = GB */
            uint64_t size_gb = size / (2 * 1024 * 1024);
            if (size_gb > 0)
            {
                /* Convert uint64_t to string manually since snprintf doesn't support %llu */
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_gb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sG", num_str);
            }
            else
            {
                /* sectors / (2 * 1024) = MB */
                uint64_t size_mb = size / (2 * 1024);
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_mb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sM", num_str);
            }
            
            if (len > 0 && len < (int)sizeof(size_str))
            {
                n = snprintf(output + output_len, sizeof(output) - output_len,
                            "%-20s %s\n", devname, size_str);
            }
            else
            {
                n = 0;
            }
        }
        
        if (n > 0 && (size_t)n < sizeof(output) - output_len)
            output_len += (size_t)n;
    }
    
    if (found_drives == 0)
    {
        n = snprintf(output + output_len, sizeof(output) - output_len,
                    "No drives detected\n");
        if (n > 0 && (size_t)n < sizeof(output) - output_len)
            output_len += (size_t)n;
    }
    
    /* Support offset: skip bytes if offset is beyond start */
    size_t start_pos = (size_t)offset;
    if (start_pos >= output_len)
    {
        /* Offset beyond end of output - return empty */
        return 0;
    }
    
    /* Copy to user buffer (respecting offset and count) */
    size_t available = output_len - start_pos;
    size_t copy_size = (available < count) ? available : count;
    if (copy_size > 0)
    {
        memcpy(buf, output + start_pos, copy_size);
    }
    
    return (int64_t)copy_size;
}

int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    if (entry->device_id == DEVFS_DISK_AGGREGATE_ID)
        return count;
    if (!buf)
        return -1;
    uint8_t disk_id;
    partition_info_t pinfo;
    int is_whole = 0;

    if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
        return -ENODEV;
    const char *disk_name = ir0_block_legacy_name(disk_id);
    if (!disk_name || !ir0_block_name_is_present(disk_name))
        return -ENODEV;
    if (offset < 0)
        return -1;
    uint64_t sector_off = (uint64_t)((unsigned long)offset / 512);
    size_t num_sectors = (count + 511) / 512;
    if (num_sectors == 0)
        return 0;
    uint64_t start_lba = sector_off;
    if (!is_whole)
    {
        if (sector_off >= pinfo.total_sectors)
            return 0;
        start_lba = pinfo.start_lba + sector_off;
        if (num_sectors > pinfo.total_sectors - sector_off)
            num_sectors = (size_t)(pinfo.total_sectors - sector_off);
    }
    else
    {
        uint64_t disk_sectors = ir0_block_sector_count_by_name(disk_name);
        if (sector_off >= disk_sectors)
            return 0;
        if (sector_off + num_sectors > disk_sectors)
            num_sectors = (size_t)(disk_sectors - sector_off);
    }
    const char *src = (const char *)buf;
    size_t bytes_done = 0;
    while (num_sectors > 0)
    {
        uint8_t n = (num_sectors > 255) ? 255 : (uint8_t)num_sectors;
        if (ir0_block_write_by_name(disk_name, (uint32_t)start_lba, n, src))
            return (int64_t)bytes_done;
        bytes_done += (size_t)n * 512;
        start_lba += n;
        num_sectors -= n;
        src += (size_t)n * 512;
    }
    return (int64_t)((bytes_done < count) ? bytes_done : count);
}

int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    uint8_t disk_id = 0;
    partition_info_t pinfo;
    memset(&pinfo, 0, sizeof(pinfo));

    int is_whole = 0;

    uint64_t part_start_lba = 0;
    uint64_t part_sectors = 0;

    int have_disk_ctx = (entry &&
        entry->device_id != DEVFS_DISK_AGGREGATE_ID &&
        entry->driver_data != NULL);

    if (have_disk_ctx)
    {
        if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
            have_disk_ctx = 0;
        else if (!is_whole)
        {
            part_start_lba = pinfo.start_lba;
            part_sectors = pinfo.total_sectors;
        }
    }

    switch (request)
    {
        case DISK_READ_SECTOR:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint32_t lba;
                    void *buffer;
                } disk_sector_req_t;
                disk_sector_req_t *req = (disk_sector_req_t *)arg;
                if (req && req->buffer)
                {
                    uint8_t d = have_disk_ctx ? disk_id : req->drive;
                    const char *disk_name = ir0_block_legacy_name(d);
                    uint32_t lba = (uint32_t)req->lba;
                    if (have_disk_ctx && !is_whole)
                        lba = (uint32_t)(part_start_lba + req->lba);
                    if (disk_name && ir0_block_name_is_present(disk_name) &&
                        ir0_block_read_by_name(disk_name, lba, 1, req->buffer) == 0)
                        return 512;
                }
            }
            return -1;

        case DISK_WRITE_SECTOR:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint32_t lba;
                    const void *buffer;
                } disk_sector_req_t;
                disk_sector_req_t *req = (disk_sector_req_t *)arg;
                if (req && req->buffer)
                {
                    uint8_t d = have_disk_ctx ? disk_id : req->drive;
                    const char *disk_name = ir0_block_legacy_name(d);
                    uint32_t lba = (uint32_t)req->lba;
                    if (have_disk_ctx && !is_whole)
                        lba = (uint32_t)(part_start_lba + req->lba);
                    if (disk_name && ir0_block_name_is_present(disk_name) &&
                        ir0_block_write_by_name(disk_name, lba, 1, req->buffer) == 0)
                        return 512;
                }
            }
            return -1;

        case DISK_GET_GEOMETRY:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint64_t *size_sectors;
                    uint64_t *size_bytes;
                } disk_geometry_t;
                disk_geometry_t *geom = (disk_geometry_t *)arg;
                if (geom)
                {
                    uint8_t d = have_disk_ctx ? disk_id : geom->drive;
                    const char *disk_name = ir0_block_legacy_name(d);
                    if (!disk_name || !ir0_block_name_is_present(disk_name))
                        return -1;
                    uint64_t sectors =
                        (have_disk_ctx && !is_whole) ? part_sectors : ir0_block_sector_count_by_name(disk_name);
                    if (geom->size_sectors)
                        *geom->size_sectors = sectors;
                    if (geom->size_bytes)
                        *geom->size_bytes = sectors * 512;
                    return 0;
                }
            }
            return -1;

        default:
            return -1;
    }
}
/**
 * /dev/random and /dev/urandom - Random number generators
 * 
 * Simple PRNG based on timer ticks and interrupt counters
 * For production, should use hardware RNG or better entropy source
 */
static uint32_t random_seed = 0;

static uint32_t simple_rand(void)
{
    /* Linear congruential generator (LCG) */
    random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return random_seed;
}

int64_t dev_random_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Initialize seed with timer if not set */
    if (random_seed == 0)
    {
        random_seed = (uint32_t)(clock_get_uptime_milliseconds() & 0xFFFFFFFF);
    }
    
    /* Fill buffer with random bytes */
    uint8_t *buffer = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
    {
        buffer[i] = (uint8_t)(simple_rand() & 0xFF);
    }
    
    return (int64_t)count;
}

int64_t dev_random_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    /*
     * /dev/random accepts writes to add entropy to the pool.
     * Mix written bytes with random_seed via XOR and LCG feedback.
     */
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        random_seed ^= (uint32_t)p[i] << (i % 24);
        random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    }
    return (int64_t)count;
}

int64_t dev_urandom_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* /dev/urandom: Non-blocking random number generator
     * Unlike /dev/random which may block when entropy is low,
     * /dev/urandom never blocks and continues generating pseudo-random data.
     * 
     * In this implementation, both use the same LCG-based generator,
     * but /dev/urandom explicitly never blocks and always returns immediately.
     */
    
    /* Initialize seed with timer if not set */
    if (random_seed == 0)
    {
        random_seed = (uint32_t)(clock_get_uptime_milliseconds() & 0xFFFFFFFF);
    }
    
    /* Fill buffer with random bytes - non-blocking, always succeeds */
    uint8_t *buffer = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
    {
        /* Use LCG with different multiplier for urandom to differentiate streams */
        random_seed = (random_seed * 1664525 + 1013904223) & 0x7FFFFFFF;
        buffer[i] = (uint8_t)(random_seed & 0xFF);
    }
    
    return (int64_t)count;
}

int64_t dev_urandom_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    /* Same as /dev/random */
    return dev_random_write(entry, buf, count, offset);
}

/**
 * /dev/full - Device that always returns ENOSPC on write
 */
int64_t dev_full_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Reading from /dev/full returns \0 (null bytes) */
    memset(buf, 0, count);
    return (int64_t)count;
}

int64_t dev_full_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Writing to /dev/full always fails with ENOSPC */
    return -ENOSPC;  /* No space left on device */
}

static const devfs_ops_t null_ops = {
    .read = dev_null_read,
    .write = dev_null_write,
};

static const devfs_ops_t zero_ops = {
    .read = dev_zero_read,
    .write = dev_zero_write,
};

static const devfs_ops_t console_ops = {
    .read = dev_console_read,
    .write = dev_console_write,
    .ioctl = dev_console_ioctl,
    .open = dev_console_open,
    .can_read = devfs_console_can_read,
};

static const devfs_ops_t kmsg_ops = {
    .read = dev_kmsg_read,
    .write = dev_kmsg_write,
};

static const devfs_ops_t audio_ops = {
    .read = dev_audio_read,
    .write = dev_audio_write,
    .ioctl = dev_audio_ioctl,
};

static const devfs_ops_t mouse_ops = {
    .read = dev_mouse_read,
    .ioctl = dev_mouse_ioctl,
#if CONFIG_ENABLE_MOUSE
    .can_read = dev_mouse_can_read,
#endif
};

static const devfs_ops_t net_ops = {
    .read = dev_net_read,
    .write = dev_net_write,
    .ioctl = dev_net_ioctl,
#if CONFIG_ENABLE_NETWORKING
    .can_read = devfs_net_can_read,
#endif
};

static const devfs_ops_t disk_ops = {
    .read = dev_disk_read,
    .write = dev_disk_write,
    .ioctl = dev_disk_ioctl,
};

static const devfs_ops_t random_ops = {
    .read = dev_random_read,
    .write = dev_random_write,
};

static const devfs_ops_t urandom_ops = {
    .read = dev_urandom_read,
    .write = dev_urandom_write,
};

static const devfs_ops_t full_ops = {
    .read = dev_full_read,
    .write = dev_full_write,
};

/*
 * /dev/fb0 - Framebuffer (OSDev / Linux fbdev)
 * write: copy pixels to framebuffer
 * ioctl(FBIOGET_VSCREENINFO): get width, height, bpp, pitch
 */
static int64_t dev_fb0_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;
}

static int64_t dev_fb0_open(devfs_entry_t *entry, int flags)
{
    (void)entry;
    (void)flags;
    if (!ir0_fb_is_available())
        return -ENODEV;
    return 0;
}

static int64_t dev_fb0_write_simple(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    return ir0_fb_write_bytes((size_t)offset, buf, count);
}

static int64_t dev_fb0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_fb_info info;

    (void)entry;
    if (!ir0_fb_get_info(&info))
        return -ENODEV;
    if (request == FBIOGET_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo var_info;

        memset(&var_info, 0, sizeof(var_info));
        var_info.xres = info.width;
        var_info.yres = info.height;
        var_info.xres_virtual = info.width;
        var_info.yres_virtual = info.height;
        var_info.bits_per_pixel = info.bpp;
        /*
         * Default matches vbe_rgb_to_pixel() BGR888 + alpha in high byte
         * (common QEMU/PC framebuffer layout).
         */
        if (info.bpp == 32 || info.bpp == 24)
        {
            var_info.red.offset = 16;
            var_info.red.length = 8;
            var_info.green.offset = 8;
            var_info.green.length = 8;
            var_info.blue.offset = 0;
            var_info.blue.length = 8;
            if (info.bpp == 32)
            {
                var_info.transp.offset = 24;
                var_info.transp.length = 8;
            }
        }
        else if (info.bpp == 16)
        {
            var_info.red.offset = 11;
            var_info.red.length = 5;
            var_info.green.offset = 5;
            var_info.green.length = 6;
            var_info.blue.offset = 0;
            var_info.blue.length = 5;
        }
        if (copy_to_user(arg, &var_info, sizeof(var_info)) != 0)
            return -EFAULT;
        return 0;
    }
    /*
     * Fixed VBE/QEMU framebuffer: no mode switching. Accept PUT so TinyX
     * KdFindMode probes do not abort; subsequent FBIOGET_* report hardware.
     */
    if (request == FBIOPUT_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo var;

        if (copy_from_user(&var, arg, sizeof(var)) != 0)
            return -EFAULT;
        (void)var;
        (void)info;
        return 0;
    }
    if (request == FBIOGET_FSCREENINFO && arg)
    {
        struct fb_fix_screeninfo fix;

        memset(&fix, 0, sizeof(fix));
        strncpy(fix.id, "IR0 FB", sizeof(fix.id) - 1);
        /*
         * IR0 mmap(/dev/fb0) maps fb_phys at offset 0 of the returned VA
         * (see sys_mmap fb path). TinyX then does
         *   fb = mmap_base + (smem_start % pagesize).
         * Reporting the raw physical address when it is not page-aligned
         * makes TinyX skip the first bytes and write past the mapping.
         * Use 0 so the userspace offset is always 0.
         */
        fix.smem_start = 0;
        fix.smem_len = info.fb_size;
        fix.type = FB_TYPE_PACKED_PIXELS;
        fix.visual = FB_VISUAL_TRUECOLOR;
        fix.line_length = info.pitch;
        fix.accel = FB_ACCEL_NONE;
        /* filled on VGET; here only fix */
        if (copy_to_user(arg, &fix, sizeof(fix)) != 0)
            return -EFAULT;
        return 0;
    }
    /* No hardware pan/blank — succeed so KDrive enable path continues. */
    if (request == FBIOPAN_DISPLAY || request == FBIOBLANK)
        return 0;
    /*
     * Colormap put/get: TrueColor FB ignores palette; accept so TinyX
     * fbdevUpdateFbColormap does not abort enable.
     */
    if (request == FBIOPUTCMAP || request == FBIOGETCMAP)
        return 0;
    return -EINVAL;
}

static const devfs_ops_t fb0_ops = {
    .read = dev_fb0_read,
    .write = dev_fb0_write_simple,
    .ioctl = dev_fb0_ioctl,
    .open = dev_fb0_open,
};

static devfs_node_t dev_fb0 = {
    .entry = { .name = "fb0", .mode = 0660, .device_id = 15 },
    .ops = &fb0_ops,
    .ref_count = 0,
};

/*
 * /dev/events0 - Linux evdev input events (keyboard for Doom)
 * read: struct input_event (type, code, value)
 */
static int64_t dev_events0_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    struct input_event ev_buf[16];
    size_t max_ev;
    size_t n;
    size_t i;

    (void)entry;
    (void)offset;
    if (count < sizeof(struct input_event))
        return 0;

    max_ev = count / sizeof(struct input_event);
    if (max_ev > 16)
        max_ev = 16;

    n = 0;
    for (i = 0; i < max_ev; i++)
    {
        struct ir0_input_event ir0_ev;
        int rc = ir0_input_read_event(&ir0_ev);

        if (rc <= 0)
            break;
        ev_buf[i].time.tv_sec = (time_t)(ir0_ev.timestamp_ms / 1000);
        ev_buf[i].time.tv_usec = (suseconds_t)((ir0_ev.timestamp_ms % 1000) * 1000);
        ev_buf[i].type = ir0_ev.type;
        ev_buf[i].code = ir0_ev.code;
        ev_buf[i].value = ir0_ev.value;
        n++;
    }

    if (n == 0)
        return 0;

    {
        size_t bytes = n * sizeof(struct input_event);
        memcpy(buf, ev_buf, bytes);
        return (int64_t)bytes;
    }
}

static void evdev_set_bit(unsigned long *bits, size_t nlongs, unsigned int bit)
{
    unsigned int idx = bit / (8u * sizeof(unsigned long));
    unsigned int off = bit % (8u * sizeof(unsigned long));

    if (idx >= nlongs)
        return;
    bits[idx] |= (1UL << off);
}

static int64_t dev_events0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_input_caps caps;
    unsigned dir;
    unsigned type;
    unsigned nr;
    unsigned size;

    (void)entry;

    if (request == IR0_INPUT_IOCTL_GET_CAPS)
    {
        if (!arg)
            return -EFAULT;
        ir0_input_get_caps(&caps);
        if (copy_to_user(arg, &caps, sizeof(caps)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_INPUT_IOCTL_INJECT)
    {
#if CONFIG_TEST_INPUT_INJECT
        struct ir0_input_event ev;

        if (!arg)
            return -EFAULT;
        if (copy_from_user(&ev, arg, sizeof(ev)) != 0)
            return -EFAULT;
        {
            int rc = ir0_input_inject_event(ev.type, ev.code, ev.value);

            if (rc < 0)
                return rc;
            /* Linux clients expect a SYN_REPORT marker after injected edges. */
            if (ev.type != EV_SYN)
                (void)ir0_input_inject_event(EV_SYN, SYN_REPORT, 0);
            return rc;
        }
#else
        (void)arg;
        return -ENOTTY;
#endif
    }

    dir = (unsigned)((request >> EVIOC_DIR_SHIFT) & 0x3u);
    type = (unsigned)((request >> EVIOC_TYPE_SHIFT) & 0xffu);
    nr = (unsigned)(request & EVIOC_NR_MASK);
    size = (unsigned)((request >> EVIOC_SIZE_SHIFT) & EVIOC_SIZE_MASK);

    if (type == (unsigned)'E' && dir == EVIOC_READ && nr == 0x01u)
    {
        int version = EV_VERSION;

        if (!arg || size < sizeof(int))
            return -EINVAL;
        if (copy_to_user(arg, &version, sizeof(version)) != 0)
            return -EFAULT;
        return 0;
    }

    if (type == (unsigned)'E' && dir == EVIOC_READ && nr >= EVIOCGBIT_BASE &&
        nr < EVIOCGBIT_BASE + EV_CNT)
    {
        unsigned long bits[8];
        unsigned ev = nr - EVIOCGBIT_BASE;
        size_t nlongs;
        size_t copy_n;

        if (!arg || size == 0)
            return -EINVAL;
        memset(bits, 0, sizeof(bits));
        nlongs = sizeof(bits) / sizeof(bits[0]);

        if (ev == 0)
        {
            /* Bitmap of supported event types. */
            evdev_set_bit(bits, nlongs, EV_SYN);
            evdev_set_bit(bits, nlongs, EV_KEY);
            evdev_set_bit(bits, nlongs, EV_REL);
        }
        else if (ev == EV_KEY)
        {
            unsigned k;

            for (k = 1; k < (unsigned)KEY_MAX && k < 256u; k++)
                evdev_set_bit(bits, nlongs, k);
            evdev_set_bit(bits, nlongs, BTN_LEFT);
            evdev_set_bit(bits, nlongs, BTN_RIGHT);
            evdev_set_bit(bits, nlongs, BTN_MIDDLE);
            evdev_set_bit(bits, nlongs, BTN_SIDE);
            evdev_set_bit(bits, nlongs, BTN_EXTRA);
        }
        else if (ev == EV_REL)
        {
            evdev_set_bit(bits, nlongs, REL_X);
            evdev_set_bit(bits, nlongs, REL_Y);
            evdev_set_bit(bits, nlongs, REL_WHEEL);
        }
        else if (ev == EV_SYN)
        {
            evdev_set_bit(bits, nlongs, SYN_REPORT);
        }
        /* other EV_* → empty bitmap (supported) */

        copy_n = size;
        if (copy_n > sizeof(bits))
            copy_n = sizeof(bits);
        if (copy_to_user(arg, bits, copy_n) != 0)
            return -EFAULT;
        return 0;
    }

    return -EINVAL;
}

static int64_t dev_events0_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	input_events_reader_open();
	return 0;
}

static int64_t dev_events0_close(devfs_entry_t *entry)
{
	(void)entry;
	input_events_reader_close();
	return 0;
}

static const devfs_ops_t events0_ops = {
    .read = dev_events0_read,
    .write = NULL,
    .ioctl = dev_events0_ioctl,
    .open = dev_events0_open,
    .close = dev_events0_close,
    .can_read = devfs_events0_can_read,
    .can_write = devfs_events0_can_write,
};

static devfs_node_t dev_events0 = {
    .entry = { .name = "events0", .mode = 0660, .device_id = 16 },
    .ops = &events0_ops,
    .ref_count = 0,
};

/* Linux path alias for Xfbdev (-mouse evdev) */
static devfs_node_t dev_input_event0 = {
    .entry = { .name = "input/event0", .mode = 0660, .device_id = 45 },
    .ops = &events0_ops,
    .ref_count = 0,
};

/*
 * /dev/serial - Write-only debug output to COM1 (for debug bins via syscalls)
 */
static int64_t dev_serial_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;
}

static int64_t dev_serial_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (!buf)
        return -EFAULT;
    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++)
        serial_putchar(p[i]);
    return (int64_t)count;
}

static const devfs_ops_t serial_ops = {
    .read = dev_serial_read,
    .write = dev_serial_write,
    .can_read = devfs_serial_can_read,
    .can_write = devfs_serial_can_write,
};

static devfs_node_t dev_serial = {
    .entry = { .name = "serial", .mode = 0220, .device_id = 42 },
    .ops = &serial_ops,
    .ref_count = 0,
};

/*
 * Minimal PTY pair: /dev/ptmx (master) + /dev/pts/0 (slave).
 * One global pair; ring buffers each direction; TIOCGWINSZ + TIOCGPTN.
 */
#define PTY_BUF_SIZE 1024

struct pty_ring
{
	char buf[PTY_BUF_SIZE];
	unsigned int head;
	unsigned int tail;
	unsigned int count;
};

static struct
{
	struct pty_ring m2s;
	struct pty_ring s2m;
	int master_open;
	int slave_open;
	int locked;
	int ctty_set;
	pid_t fg_pgid;
	pid_t session_sid;
	struct ir0_winsize winsz;
} g_pty;

static void pty_hangup_fg(void)
{
	if (!g_pty.ctty_set || g_pty.fg_pgid <= 0)
		return;
	klog_print("PTY_SIGHUP_PGRP\n");
	(void)send_signal_pgrp(g_pty.fg_pgid, SIGHUP);
	g_pty.ctty_set = 0;
	g_pty.fg_pgid = 0;
	g_pty.session_sid = 0;
}

static int pty_ring_push(struct pty_ring *r, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (r->count >= PTY_BUF_SIZE)
			break;
		r->buf[r->head] = src[i];
		r->head = (r->head + 1) % PTY_BUF_SIZE;
		r->count++;
	}
	return (int)i;
}

static int pty_ring_pop(struct pty_ring *r, char *dst, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (r->count == 0)
			break;
		dst[i] = r->buf[r->tail];
		r->tail = (r->tail + 1) % PTY_BUF_SIZE;
		r->count--;
	}
	return (int)i;
}

static int64_t dev_ptmx_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	if (g_pty.master_open)
		return -EBUSY;
	g_pty.master_open = 1;
	g_pty.locked = 0;
	return 0;
}

static int64_t dev_pts_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	if (g_pty.locked)
		return -EIO;
	if (!g_pty.master_open)
		return -EIO;
	g_pty.slave_open = 1;
	return 0;
}

static int64_t dev_ptmx_close(devfs_entry_t *entry)
{
	(void)entry;
	g_pty.master_open = 0;
	/* Master close → hangup controlling session (Linux PTY semantics). */
	pty_hangup_fg();
	return 0;
}

static int64_t dev_pts_close(devfs_entry_t *entry)
{
	(void)entry;
	g_pty.slave_open = 0;
	return 0;
}

static int64_t dev_ptmx_read(devfs_entry_t *entry, void *buf, size_t count,
			     off_t offset)
{
	(void)entry;
	(void)offset;
	if (!buf)
		return -EFAULT;
	return pty_ring_pop(&g_pty.s2m, (char *)buf, count);
}

static int64_t dev_ptmx_write(devfs_entry_t *entry, const void *buf,
			      size_t count, off_t offset)
{
	(void)entry;
	(void)offset;
	if (!buf)
		return -EFAULT;
	return pty_ring_push(&g_pty.m2s, (const char *)buf, count);
}

static int64_t dev_pts_read(devfs_entry_t *entry, void *buf, size_t count,
			    off_t offset)
{
	(void)entry;
	(void)offset;
	if (!buf)
		return -EFAULT;
	return pty_ring_pop(&g_pty.m2s, (char *)buf, count);
}

static int64_t dev_pts_write(devfs_entry_t *entry, const void *buf,
			     size_t count, off_t offset)
{
	(void)entry;
	(void)offset;
	if (!buf)
		return -EFAULT;
	return pty_ring_push(&g_pty.s2m, (const char *)buf, count);
}

static int64_t dev_pty_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
	(void)entry;

	if (request == IR0_CONSOLE_TIOCGWINSZ)
	{
		struct ir0_winsize win;

		if (!arg)
			return -EINVAL;
		if (g_pty.winsz.ws_row != 0 || g_pty.winsz.ws_col != 0)
			win = g_pty.winsz;
		else
		{
			int wret = ir0_console_ioctl_winsize(arg);

			return wret;
		}
		if (copy_to_user(arg, &win, sizeof(win)) != 0)
			return -EFAULT;
		return 0;
	}
	if (request == IR0_CONSOLE_TIOCSWINSZ)
	{
		struct ir0_winsize win;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&win, arg, sizeof(win)) != 0)
			return -EFAULT;
		if (win.ws_row == 0 || win.ws_col == 0)
			return -EINVAL;
		g_pty.winsz = win;
		if (g_pty.ctty_set && g_pty.fg_pgid > 0)
			(void)send_signal_pgrp(g_pty.fg_pgid, SIGWINCH);
		else if (current_process)
			(void)send_signal((int)current_process->task.pid, SIGWINCH);
		klog_print("PTY_WINCH_SENT\n");
		handle_signals();
		klog_print("PTY_TIOCSWINSZ_OK\n");
		return 0;
	}
	if (request == IR0_TIOCGPTN)
	{
		unsigned int n = 0;

		if (!arg)
			return -EINVAL;
		if (copy_to_user(arg, &n, sizeof(n)) != 0)
			return -EFAULT;
		return 0;
	}
	if (request == IR0_TIOCSPTLCK)
	{
		int lock = 0;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&lock, arg, sizeof(lock)) != 0)
			return -EFAULT;
		g_pty.locked = lock ? 1 : 0;
		return 0;
	}
	if (request == IR0_TIOCSCTTY)
	{
		if (!current_process)
			return -ESRCH;
		/* Session leader only (Linux-like). */
		if (current_process->sid != current_process->task.pid)
			return -EPERM;
		g_pty.ctty_set = 1;
		g_pty.session_sid = current_process->sid;
		g_pty.fg_pgid = current_process->pgid;
		klog_print("PTY_TIOCSCTTY_OK\n");
		return 0;
	}
	if (request == IR0_TIOCSPGRP)
	{
		pid_t pg;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&pg, arg, sizeof(pg)) != 0)
			return -EFAULT;
		if (pg <= 0)
			return -EINVAL;
		if (!g_pty.ctty_set)
			return -ENOTTY;
		g_pty.fg_pgid = pg;
		return 0;
	}
	if (request == IR0_TIOCGPGRP)
	{
		pid_t pg;

		if (!arg)
			return -EINVAL;
		if (!g_pty.ctty_set)
			return -ENOTTY;
		pg = g_pty.fg_pgid;
		if (copy_to_user(arg, &pg, sizeof(pg)) != 0)
			return -EFAULT;
		return 0;
	}
	if (request == IR0_CONSOLE_TCGETS || request == IR0_CONSOLE_TCSETS ||
	    request == IR0_CONSOLE_TCSETSW || request == IR0_CONSOLE_TCSETSF)
		return dev_console_ioctl(entry, request, arg);
	return -ENOTTY;
}

static int dev_ptmx_can_read(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return g_pty.s2m.count > 0 ? 1 : 0;
}

static int dev_ptmx_can_write(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return g_pty.m2s.count < PTY_BUF_SIZE ? 1 : 0;
}

static int dev_pts_can_read(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return g_pty.m2s.count > 0 ? 1 : 0;
}

static int dev_pts_can_write(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return g_pty.s2m.count < PTY_BUF_SIZE ? 1 : 0;
}

static const devfs_ops_t ptmx_ops = {
	.read = dev_ptmx_read,
	.write = dev_ptmx_write,
	.ioctl = dev_pty_ioctl,
	.open = dev_ptmx_open,
	.close = dev_ptmx_close,
	.can_read = dev_ptmx_can_read,
	.can_write = dev_ptmx_can_write,
};

static const devfs_ops_t pts_ops = {
	.read = dev_pts_read,
	.write = dev_pts_write,
	.ioctl = dev_pty_ioctl,
	.open = dev_pts_open,
	.close = dev_pts_close,
	.can_read = dev_pts_can_read,
	.can_write = dev_pts_can_write,
};

static devfs_node_t dev_ptmx = {
	.entry = { .name = "ptmx", .mode = 0666, .device_id = 43 },
	.ops = &ptmx_ops,
	.ref_count = 0,
};

static devfs_node_t dev_pts0 = {
	.entry = { .name = "pts/0", .mode = 0620, .device_id = 44 },
	.ops = &pts_ops,
	.ref_count = 0,
};

/* IPC device operations */
int64_t dev_ipc_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Extract channel ID from driver_data (set in open/ioctl) */
    ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
    if (!channel)
        return -1;
    
    /* Read from IPC channel (blocking) */
    return ipc_channel_read(channel, buf, count);
}

int64_t dev_ipc_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Extract channel ID from driver_data (set in open/ioctl) */
    ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
    if (!channel)
        return -1;
    
    /* Write to IPC channel (blocking) */
    return ipc_channel_write(channel, buf, count);
}

int64_t dev_ipc_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    if (!entry)
        return -1;
    
    switch (request)
    {
        case IPC_CREATE_CHANNEL:
            if (arg) {
                /* arg points to uint32_t *channel_id (input/output) */
                uint32_t *channel_id_ptr = (uint32_t *)arg;
                uint32_t channel_id = *channel_id_ptr;
                
                /* If channel_id is 0, allocate a new one */
                if (channel_id == 0) {
                    channel_id = ipc_allocate_channel_id();
                }
                
                /* Get or create channel */
                ipc_channel_t *channel = ipc_channel_get_or_create(channel_id);
                if (!channel)
                    return -1;
                
                /* Store channel pointer in driver_data */
                entry->driver_data = (void *)channel;
                
                /* Return channel ID */
                *channel_id_ptr = channel_id;
                
                /* Increment reference count */
                ipc_channel_ref(channel);
                
                return 0;
            }
            return -1;
            
        case IPC_DESTROY_CHANNEL:
            if (entry->driver_data) {
                ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
                ipc_channel_unref(channel);
                entry->driver_data = NULL;
                return 0;
            }
            return -1;
            
        case IPC_GET_CHANNEL_ID:
            if (arg && entry->driver_data) {
                ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
                uint32_t *channel_id_ptr = (uint32_t *)arg;
                *channel_id_ptr = channel->id;
                return 0;
            }
            return -1;
            
        default:
            return -1;
    }
}

int64_t dev_ipc_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    /* Channel will be created/opened via ioctl */
    return 0;
}

int64_t dev_ipc_close(devfs_entry_t *entry)
{
    if (!entry)
        return -1;
    
    /* If channel was opened, release reference */
    if (entry->driver_data) {
        ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
        ipc_channel_unref(channel);
        entry->driver_data = NULL;
    }
    
    return 0;
}

static const devfs_ops_t ipc_ops = {
    .read = dev_ipc_read,
    .write = dev_ipc_write,
    .ioctl = dev_ipc_ioctl,
    .open = dev_ipc_open,
    .close = dev_ipc_close,
};

#if CONFIG_ENABLE_BLUETOOTH
/* Bluetooth HCI device operations */
int64_t dev_bluetooth_hci_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return ir0_bt_hci_read((char *)buf, count);
}

int64_t dev_bluetooth_hci_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return ir0_bt_hci_write((const char *)buf, count);
}

int64_t dev_bluetooth_hci_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    return ir0_bt_hci_open();
}

int64_t dev_bluetooth_hci_close(devfs_entry_t *entry)
{
    (void)entry;
    return ir0_bt_hci_close();
}

int64_t dev_bluetooth_hci_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    return ir0_bt_hci_ioctl((unsigned int)request, (unsigned long)arg);
}

static const devfs_ops_t bluetooth_hci_ops = {
    .read = dev_bluetooth_hci_read,
    .write = dev_bluetooth_hci_write,
    .open = dev_bluetooth_hci_open,
    .close = dev_bluetooth_hci_close,
    .ioctl = dev_bluetooth_hci_ioctl
};
#endif

devfs_node_t dev_null = {
    .entry = { .name = "null", .mode = 0666, .device_id = 1 },
    .ops = &null_ops,
    .ref_count = 0
};

devfs_node_t dev_zero = {
    .entry = { .name = "zero", .mode = 0666, .device_id = 2 },
    .ops = &zero_ops,
    .ref_count = 0
};

devfs_node_t dev_console = {
    .entry = { .name = "console", .mode = 0620, .device_id = 3 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_tty = {
    .entry = { .name = "tty", .mode = 0620, .device_id = 4 },
    .ops = &console_ops,
    .ref_count = 0
};

/* Synthetic VTs for TinyX/Xfbdev LinuxInit (VT_OPENQRY → tty1). */
devfs_node_t dev_tty0 = {
    .entry = { .name = "tty0", .mode = 0620, .device_id = 46 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_tty1 = {
    .entry = { .name = "tty1", .mode = 0620, .device_id = 47 },
    .ops = &console_ops,
    .ref_count = 0
};

/*
 * POSIX stream aliases: stdin was 16 and collided with events0 (evdev).
 * stdin uses 17; stdout/stderr/serial use 40–42 to stay clear of disk IDs 20–39.
 */
devfs_node_t dev_stdin = {
    .entry = { .name = "stdin", .mode = 0620, .device_id = 17 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_stdout = {
    .entry = { .name = "stdout", .mode = 0620, .device_id = 40 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_stderr = {
    .entry = { .name = "stderr", .mode = 0620, .device_id = 41 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_kmsg = {
    .entry = { .name = "kmsg", .mode = 0600, .device_id = 5 },
    .ops = &kmsg_ops,
    .ref_count = 0
};

devfs_node_t dev_audio = {
    .entry = { .name = "audio", .mode = 0660, .device_id = 6 },
    .ops = &audio_ops,
    .ref_count = 0
};

devfs_node_t dev_mouse = {
    .entry = { .name = "mouse", .mode = 0660, .device_id = 7 },
    .ops = &mouse_ops,
    .ref_count = 0
};

devfs_node_t dev_net = {
    .entry = { .name = "net", .mode = 0660, .device_id = 8 },
    .ops = &net_ops,
    .ref_count = 0
};

devfs_node_t dev_disk = {
    .entry = { .name = "disk", .mode = 0660, .device_id = 9 },
    .ops = &disk_ops,
    .ref_count = 0
};

/*
 * Registers /dev/hda..hdc only when a disk is present, and hdX{N} partitions
 * only when the partition table exposes them (never a fixed hdX4 grid).
 */

static devfs_disk_ctx_t disk_ctx_whole_slots[MAX_DISKS];
static devfs_node_t disk_node_whole_slots[MAX_DISKS];

#define DEV_PART_MAX_NODES 148
static devfs_disk_ctx_t disk_part_ctx[DEV_PART_MAX_NODES];
static devfs_node_t disk_part_nodes[DEV_PART_MAX_NODES];

static unsigned disk_part_ctx_used;

static const char *const disk_whole_name[MAX_DISKS] = { "hda", "hdb", "hdc", "hdd" };

static void devfs_register_disk_topology(void)
{
    uint32_t next_dyn_dev = DEVFS_DISK_BASE_ID + (uint32_t)MAX_DISKS;

    disk_part_ctx_used = 0;
    memset(disk_part_ctx, 0, sizeof(disk_part_ctx));
    memset(disk_part_nodes, 0, sizeof(disk_part_nodes));

    /*
     * Present whole-disk nodes only (/dev/hda …) — stable IDs remain 20..23.
     */
    for (unsigned d = 0; d < (unsigned)MAX_DISKS; d++)
    {
        const char *dn = ir0_block_legacy_name((uint8_t)d);
        if (!dn || !ir0_block_name_is_present(dn))
            continue;

        memset(&disk_ctx_whole_slots[d], 0, sizeof(disk_ctx_whole_slots[d]));
        disk_ctx_whole_slots[d].disk_id = (uint8_t)d;
        disk_ctx_whole_slots[d].is_whole = 1;
        memset(&disk_node_whole_slots[d], 0, sizeof(disk_node_whole_slots[d]));
        disk_node_whole_slots[d].entry.name = disk_whole_name[d];
        disk_node_whole_slots[d].entry.mode = 0660;
        disk_node_whole_slots[d].entry.device_id = (uint32_t)(DEVFS_DISK_BASE_ID + d);
        disk_node_whole_slots[d].entry.driver_data = &disk_ctx_whole_slots[d];
        disk_node_whole_slots[d].ops = &disk_ops;
        devfs_register_node(&disk_node_whole_slots[d]);

        int pc = get_partition_count((uint8_t)d);
        if (pc <= 0)
            continue;

        for (unsigned ord = 0; ord < (unsigned)pc; ord++)
        {
            partition_info_t pi;
            if (partition_nth_on_disk((uint8_t)d, ord, &pi) != 0)
                break;
            if (disk_part_ctx_used >= DEV_PART_MAX_NODES)
                return;

            char *nm = kmalloc(28);
            if (!nm)
                break;
            snprintf(nm, 28u, "hd%c%u",
                     'a' + (int)d, (int)(pi.partition_number + 1u));

            unsigned ix = disk_part_ctx_used;
            memset(&disk_part_ctx[ix], 0, sizeof(disk_part_ctx[ix]));
            disk_part_ctx[ix].disk_id = (uint8_t)d;
            disk_part_ctx[ix].is_whole = 0;
            disk_part_ctx[ix].partition_number = pi.partition_number;

            memset(&disk_part_nodes[ix], 0, sizeof(disk_part_nodes[ix]));
            disk_part_nodes[ix].entry.name = nm;
            disk_part_nodes[ix].entry.mode = 0660;
            disk_part_nodes[ix].entry.device_id = next_dyn_dev++;
            disk_part_nodes[ix].entry.driver_data = &disk_part_ctx[ix];
            disk_part_nodes[ix].ops = &disk_ops;
            devfs_register_node(&disk_part_nodes[ix]);
            disk_part_ctx_used++;
        }
    }
}

devfs_node_t dev_random = {
    .entry = { .name = "random", .mode = 0644, .device_id = 10 },
    .ops = &random_ops,
    .ref_count = 0
};

devfs_node_t dev_urandom = {
    .entry = { .name = "urandom", .mode = 0644, .device_id = 11 },
    .ops = &urandom_ops,
    .ref_count = 0
};

devfs_node_t dev_full = {
    .entry = { .name = "full", .mode = 0666, .device_id = 12 },
    .ops = &full_ops,
    .ref_count = 0
};

devfs_node_t dev_ipc = {
    .entry = { .name = "ipc", .mode = 0666, .device_id = 13 },
    .ops = &ipc_ops,
    .ref_count = 0
};

#if CONFIG_ENABLE_BLUETOOTH
devfs_node_t dev_bluetooth_hci0 = {
    .entry = { .name = "bluetooth/hci0", .mode = 0660, .device_id = 14 },
    .ops = &bluetooth_hci_ops,
    .ref_count = 0
};
#endif

/**
 * devfs_register_node - Registra un nodo pre-asignado en devfs
 * @node: Nodo con entry y ops ya configurados
 *
 * Para dispositivos con struct estática. Para dispositivos dinámicos usar
 * devfs_register_device.
 */
int devfs_register_node(devfs_node_t *node)
{
    if (!node || num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}

int devfs_init(void)
{
    devfs_register_node(&dev_null);
    devfs_register_node(&dev_zero);
    devfs_register_node(&dev_console);
    devfs_register_node(&dev_tty);
    devfs_register_node(&dev_tty0);
    devfs_register_node(&dev_tty1);
    devfs_register_node(&dev_stdin);
    devfs_register_node(&dev_stdout);
    devfs_register_node(&dev_stderr);
    devfs_register_node(&dev_kmsg);
    devfs_register_node(&dev_audio);
    devfs_register_node(&dev_mouse);
    devfs_register_node(&dev_net);
    devfs_register_node(&dev_disk);
    devfs_register_disk_topology();
    devfs_register_node(&dev_random);
    devfs_register_node(&dev_urandom);
    devfs_register_node(&dev_full);
    devfs_register_node(&dev_fb0);
    devfs_register_node(&dev_events0);
    devfs_register_node(&dev_input_event0);
    devfs_register_node(&dev_serial);
    devfs_register_node(&dev_ptmx);
    devfs_register_node(&dev_pts0);
    devfs_register_node(&dev_ipc);
#if CONFIG_ENABLE_BLUETOOTH
    devfs_register_node(&dev_bluetooth_hci0);
#endif
    ktm_userdev_register();

    return 0;
}

int64_t devfs_open_node(devfs_node_t *node, int flags)
{
    int64_t rc;

    if (!node)
        return -EINVAL;

    if (node->ops && node->ops->open)
    {
        rc = node->ops->open(&node->entry, flags);
        if (rc < 0)
            return rc;
    }

    node->ref_count++;
    return 0;
}

int64_t devfs_close_node(devfs_node_t *node)
{
    int64_t rc;

    if (!node)
        return -EINVAL;

    if (node->ref_count == 0)
        return -EBADF;

    node->ref_count--;
    rc = 0;
    if (node->ops && node->ops->close)
    {
        /*
         * events0: per-fd release (reader divert count).
         * Other devices: last-close teardown only.
         */
        if (node == &dev_events0 || node == &dev_input_event0 ||
            node->ref_count == 0)
            rc = node->ops->close(&node->entry);
    }

    return rc;
}

devfs_node_t *devfs_find_node(const char *path)
{
    if (!path || strncmp(path, "/dev/", 5) != 0)
        return NULL;
    
    const char *name = path + 5;  /* Skip "/dev/" */
    
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (strcmp(dev_nodes[i]->entry.name, name) == 0)
        {
            return dev_nodes[i];
        }
    }
    
    return NULL;
}

int devfs_stat_path(const char *path, stat_t *buf)
{
    devfs_node_t *node;

    if (!path || !buf)
        return -EINVAL;

    if (strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0)
    {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 2;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_blksize = 512;
        return 0;
    }

    if (devfs_is_virtual_subdir(path))
    {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 2;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_blksize = 512;
        return 0;
    }

    node = devfs_find_node(path);
    if (!node)
        return -ENOENT;

    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFCHR | (node->entry.mode & 0777);
    buf->st_rdev = node->entry.device_id;
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_blksize = 512;
    return 0;
}

static const char *devfs_virtual_subdir_prefix(const char *path)
{
    if (!path)
        return NULL;

    if (strcmp(path, "/dev/pts") == 0 || strcmp(path, "/dev/pts/") == 0)
        return "pts";
    if (strcmp(path, "/dev/input") == 0 || strcmp(path, "/dev/input/") == 0)
        return "input";
    if (strcmp(path, "/dev/bluetooth") == 0 ||
        strcmp(path, "/dev/bluetooth/") == 0)
        return "bluetooth";

    return NULL;
}

int devfs_is_virtual_subdir(const char *path)
{
    return devfs_virtual_subdir_prefix(path) != NULL;
}

int devfs_readdir_subdir(const char *path, struct vfs_dirent *entries,
                         int max_entries)
{
    const char *prefix;
    size_t prefix_len;
    int n = 0;
    int i;
    int j;

    prefix = devfs_virtual_subdir_prefix(path);
    if (!prefix || !entries || max_entries <= 0)
        return -EINVAL;

    prefix_len = strlen(prefix);
    memset(entries, 0, (size_t)max_entries * sizeof(struct vfs_dirent));

    strncpy(entries[n].name, ".", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;
    if (n >= max_entries)
        return n;

    strncpy(entries[n].name, "..", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;

    for (i = 0; i < num_dev_nodes && n < max_entries; i++)
    {
        const char *name;
        const char *leaf;
        const char *slash;
        int exists = 0;

        if (!dev_nodes[i] || !dev_nodes[i]->entry.name)
            continue;

        name = dev_nodes[i]->entry.name;
        if (strncmp(name, prefix, prefix_len) != 0 || name[prefix_len] != '/')
            continue;

        leaf = name + prefix_len + 1;
        slash = strchr(leaf, '/');
        if (slash)
            continue;

        for (j = 0; j < n; j++)
        {
            if (strcmp(entries[j].name, leaf) == 0)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;

        strncpy(entries[n].name, leaf, sizeof(entries[n].name) - 1);
        entries[n].type = DT_UNKNOWN;
        n++;
    }

    return n;
}

int devfs_readdir_root(struct vfs_dirent *entries, int max_entries)
{
    int n = 0;
    int i;
    int j;

    if (!entries || max_entries <= 0)
        return -EINVAL;

    memset(entries, 0, (size_t)max_entries * sizeof(struct vfs_dirent));

    strncpy(entries[n].name, ".", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;
    if (n >= max_entries)
        return n;

    strncpy(entries[n].name, "..", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;

    for (i = 0; i < num_dev_nodes && n < max_entries; i++)
    {
        const char *name;
        char top[VFS_PATH_MAX];
        const char *slash;
        size_t len;
        int exists = 0;

        if (!dev_nodes[i] || !dev_nodes[i]->entry.name)
            continue;

        name = dev_nodes[i]->entry.name;
        slash = strchr(name, '/');
        len = slash ? (size_t)(slash - name) : strlen(name);
        if (len == 0 || len >= sizeof(top))
            continue;

        memcpy(top, name, len);
        top[len] = '\0';

        for (j = 0; j < n; j++)
        {
            if (strcmp(entries[j].name, top) == 0)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;

        strncpy(entries[n].name, top, sizeof(entries[n].name) - 1);
        entries[n].type = slash ? DT_DIR : DT_UNKNOWN;
        n++;
    }

    return n;
}

devfs_node_t *devfs_find_node_by_id(uint32_t device_id)
{
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (dev_nodes[i] && dev_nodes[i]->entry.device_id == device_id)
        {
            return dev_nodes[i];
        }
    }
    return NULL;
}

int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode)
{
    if (!name || !ops)
        return -1;

    if (num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    
    devfs_node_t *node = kmalloc(sizeof(devfs_node_t));
    if (!node)
        return -1;

    size_t name_len = strlen(name);
    char *name_copy = kmalloc(name_len + 1);
    if (!name_copy)
    {
        kfree(node);
        return -1;
    }
    memcpy(name_copy, name, name_len + 1);
    
    node->entry.name = name_copy;
    node->entry.mode = mode;
    node->entry.device_id = num_dev_nodes + 100;  /* Dynamic IDs */
    node->ops = ops;
    node->ref_count = 0;
    
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}

int devfs_unregister_device(const char *name)
{
    if (!name)
        return -1;

    for (int i = 0; i < num_dev_nodes; i++)
    {
        devfs_node_t *node = dev_nodes[i];
        if (!node || !node->entry.name)
            continue;
        if (strcmp(node->entry.name, name) != 0)
            continue;

        /*
         * Dynamic devices registered via devfs_register_device use IDs >= 100.
         * Built-in static nodes are not owned by this unregistration path.
         */
        if (node->entry.device_id < 100)
            return -1;

        if (node->entry.name)
            kfree((void *)node->entry.name);
        kfree(node);

        for (int j = i; j < num_dev_nodes - 1; j++)
            dev_nodes[j] = dev_nodes[j + 1];
        dev_nodes[num_dev_nodes - 1] = NULL;
        num_dev_nodes--;
        return 0;
    }

    return -1;
}
