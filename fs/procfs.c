/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: procfs.c
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Simple /proc filesystem
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.c
 * Description: Minimal /proc filesystem - on-demand, no mounting
 */

#include "procfs.h"
#include <ir0/stat.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <ir0/mm_port.h>
#include <string.h>
#include <ir0/errno.h>
#include <ir0/net.h>
#include <ir0/driver.h>
#include <ir0/klog.h>
#include <ir0/ktm/stack_watch.h>
#include <ir0/process.h>
#include <ir0/mm_struct.h>
#include <ir0/syscall.h>
#include <ir0/credentials.h>
#include <config.h>
#include <ir0/version.h>
#include <ir0/utsname_info.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <ir0/partition.h>
#include <ir0/blockdev.h>
#include <ir0/multiboot.h>
#include <ir0/arch_cpu.h>
#include <fs/vfs.h>
#include <ir0/validation.h>
#include <ir0/resource_registry.h>
#include <ir0/pseudo_fs.h>
#include <ir0/fd_types.h>
#include <ir0/files_struct.h>
#include <ir0/logging.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_icmp.h>

#define PROC_BUFFER_SIZE           4096    /* Standard proc buffer size */
#define PROC_LINE_MAX_LEN          256     /* Max line length for parsing */
#define PROC_ESTIMATED_ENTRY_SIZE  256     /* Estimated entry size for formatting */
#define PROC_DEFAULT_FILE_SIZE     1024    /* Default file size for stat */
/* /dev/console device id — single terminal, reported as tty_nr in pid stat. */
#define IR0_PROC_CONSOLE_TTY_NR    3
#define BYTES_PER_KB               1024    /* Bytes per kilobyte */
#define BYTES_PER_SECTOR           512     /* Bytes per disk sector */
#define SECTORS_PER_MB             (2 * 1024)  /* Sectors per megabyte (2*1024*512 = 1MB) */

static void proc_u64_to_dec(uint64_t value, char *out, size_t out_len);

/*
 * Snapshot scalar /proc fields and pin the address space while a read path
 * walks page tables. process_find_by_pid() drops the irq lock before return;
 * a concurrent reap could free process_t and mm while stat/cmdline still run.
 */
typedef struct proc_fs_snap
{
	char comm[16];
	pid_t pid;
	pid_t ppid;
	pid_t pgid;
	pid_t sid;
	int sched_prio;
	uint64_t start_ticks;
	int state;
	uid_t uid;
	gid_t gid;
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t stack_start;
	uint64_t stack_size;
	struct mmap_region *mmap_list;
	mm_struct_t *mm;
} proc_fs_snap_t;

static int proc_fs_snap_acquire(pid_t pid, proc_fs_snap_t *snap)
{
	process_t *proc;
	uint64_t irqf;

	if (!snap)
		return -EINVAL;

	memset(snap, 0, sizeof(*snap));

	irqf = (uint64_t)irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (!proc)
	{
		irq_restore((unsigned long)irqf);
		return 0;
	}

	strncpy(snap->comm, proc->comm, sizeof(snap->comm) - 1);
	snap->comm[sizeof(snap->comm) - 1] = '\0';
	snap->pid = proc->task.pid;
	snap->ppid = proc->ppid;
	snap->pgid = proc->pgid;
	snap->sid = proc->sid;
	snap->sched_prio = proc->sched_prio;
	snap->start_ticks = proc->start_ticks;
	snap->state = (int)proc->state;
	snap->uid = proc->uid;
	snap->gid = proc->gid;

	if (proc->mm)
	{
		snap->mm = mm_get(proc->mm);
		if (snap->mm)
		{
			snap->heap_start = snap->mm->heap_start;
			snap->heap_end = snap->mm->heap_end;
			snap->stack_start = snap->mm->stack_start;
			snap->stack_size = snap->mm->stack_size;
			snap->mmap_list = snap->mm->mmap_list;
		}
	}

	irq_restore((unsigned long)irqf);
	return 1;
}

static void proc_fs_snap_release(proc_fs_snap_t *snap)
{
	if (!snap || !snap->mm)
		return;

	mm_put(snap->mm);
	snap->mm = NULL;
}

/*
 * /proc/ps: one line header then one line per process, tab-separated.
 * Header: PID\tPPID\tS\tUID\tCMD
 * State: R=runnable/running, S=sleeping(blocked), Z=zombie (§8).
 */
int proc_ps_read(char *buf, size_t count)
{
	typedef struct
	{
		pid_t pid;
		pid_t ppid;
		uid_t uid;
		char comm[16];
		int state;
	} proc_ps_row_t;

	proc_ps_row_t rows[64];
	int row_count = 0;
	process_t *p;
	uint64_t irqf;
	size_t off = 0;
	int n;
	int i;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	n = snprintf(buf, count, "PID\tPPID\tS\tUID\tCMD\n");
	if (n < 0)
		return -1;
	if (n >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	off += (size_t)n;

	irqf = (uint64_t)irq_save();
	for (p = process_list; p && row_count < (int)(sizeof(rows) / sizeof(rows[0]));
	     p = p->next)
	{
		rows[row_count].pid = p->task.pid;
		rows[row_count].ppid = p->ppid;
		rows[row_count].uid = p->uid;
		rows[row_count].state = (int)p->state;
		strncpy(rows[row_count].comm, p->comm, sizeof(rows[row_count].comm) - 1);
		rows[row_count].comm[sizeof(rows[row_count].comm) - 1] = '\0';
		row_count++;
	}
	irq_restore((unsigned long)irqf);

	for (i = 0; i < row_count && off < count - 1; i++)
	{
		const char *state_str = "?";
		const char *name = rows[i].comm[0] ? rows[i].comm : "(none)";

		if (rows[i].state == PROCESS_ZOMBIE)
			state_str = "Z";
		else
		{
			switch (rows[i].state)
			{
			case PROCESS_READY:   state_str = "R"; break;
			case PROCESS_RUNNING: state_str = "R"; break;
			case PROCESS_BLOCKED: state_str = "S"; break;
			default:              state_str = "?"; break;
			}
		}

		n = snprintf(buf + off, count - off,
			     "%d\t%d\t%s\t%u\t%s\n",
			     (int)rows[i].pid, (int)rows[i].ppid, state_str,
			     (unsigned)rows[i].uid, name);
		if (n < 0)
			break;
		if (n >= (int)(count - off))
		{
			n = (int)(count - off) - 1;
			off += (size_t)n;
			break;
		}
		off += (size_t)n;
	}

	if (off < count)
		buf[off] = '\0';
	return (int)off;
}

/*
 * /proc/netinfo: raw data only, one line per interface, tab-separated.
 * name\tmtu\tflags\tmac (mac as xx:xx:xx:xx:xx:xx)
 * Frontend (e.g. ifconfig/netinfo) does formatting.
 */
int proc_netinfo_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct net_device *dev = net_get_devices();
    if (!dev)
        return 0;
    size_t off = 0;
    while (dev && off < count - 1)
    {
        char flags[32];
        size_t foff = 0;
        flags[0] = '\0';
        if (dev->flags & IFF_UP) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "UP");
        if (dev->flags & IFF_RUNNING) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sRUNNING", foff ? "," : "");
        if (dev->flags & IFF_BROADCAST) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sBROADCAST", foff ? "," : "");
        if (foff == 0) snprintf(flags, sizeof(flags), "-");
        int n = snprintf(buf + off, count - off,
                         "%s\t%u\t%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
                         dev->name ? dev->name : "", (unsigned)dev->mtu, flags,
                         (unsigned)dev->mac[0], (unsigned)dev->mac[1], (unsigned)dev->mac[2],
                         (unsigned)dev->mac[3], (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        dev = dev->next;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
#else
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    return 0;
#endif
}

/*
 * /proc/net/dev: Linux-style per-interface summary.
 * Header lines mirror common tools that parse Inter-| / face | columns.
 */
int proc_net_dev_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    /*
     * Linux /proc/net/dev columns (BusyBox interface.c procnetdev_vsn=2).
     * Header must contain "bytes" and "compressed" for fancy ifconfig stats.
     */
    int n = snprintf(buf, count,
                     "Inter-|   Receive                                                |  Transmit\n"
                     " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");
    if (n < 0)
        return -1;
    if ((size_t)n >= count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    off = (size_t)n;

    struct net_device *dev = net_get_devices();
    while (dev && off < count - 1)
    {
        uint64_t rxp = 0, txp = 0, rxe = 0, txe = 0;
        uint64_t rxb = 0, txb = 0;
        char rxp_str[24];
        char rxe_str[24];
        char txp_str[24];
        char txe_str[24];
        char rxb_str[24];
        char txb_str[24];
        char rxd_str[24];
        char rxf_str[24];
        char rxm_str[24];

        if (dev->get_stats)
            dev->get_stats(dev, &rxp, &txp, &rxe, &txe);
        if (dev->get_byte_stats)
            dev->get_byte_stats(dev, &rxb, &txb);

        proc_u64_to_dec(rxb, rxb_str, sizeof(rxb_str));
        proc_u64_to_dec(rxp, rxp_str, sizeof(rxp_str));
        proc_u64_to_dec(rxe, rxe_str, sizeof(rxe_str));
        proc_u64_to_dec(txb, txb_str, sizeof(txb_str));
        proc_u64_to_dec(txp, txp_str, sizeof(txp_str));
        proc_u64_to_dec(txe, txe_str, sizeof(txe_str));
        proc_u64_to_dec(dev->rx_dropped, rxd_str, sizeof(rxd_str));
        proc_u64_to_dec(dev->rx_fifo_errors, rxf_str, sizeof(rxf_str));
        proc_u64_to_dec(dev->rx_multicast, rxm_str, sizeof(rxm_str));
        /*
         * bytes packets errs drop fifo frame compressed multicast | tx...
         * RX frame/compressed and every TX column past errs stay 0: no driver
         * accounts alignment errors, compression, TX drops, collisions or
         * carrier losses yet (would require reading NIC error registers).
         */
        n = snprintf(buf + off, count - off,
                     "  %s: %s %s %s %s %s 0 0 %s %s %s %s 0 0 0 0 0\n",
                     (dev->name && dev->name[0] != '\0') ? dev->name : "eth0",
                     rxb_str, rxp_str, rxe_str, rxd_str, rxf_str, rxm_str,
                     txb_str, txp_str, txe_str);
        if (n < 0)
            return -1;
        if ((size_t)n >= count - off)
        {
            buf[count - 1] = '\0';
            return (int)(count - 1);
        }
        off += (size_t)n;
        dev = dev->next;
    }

    return (int)off;
#else
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    return 0;
#endif
}

/*
 * /proc/net/route — Linux fib_trie format for BusyBox route/netstat -r.
 * Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT
 */
#define IR0_RTF_UP      0x0001
#define IR0_RTF_GATEWAY 0x0002

#if CONFIG_ENABLE_NETWORKING
struct proc_route_fmt_ctx
{
	char *buf;
	size_t count;
	size_t off;
	const char *ifname;
	int err;
};

static int proc_route_emit_row(char *buf, size_t count, size_t *off,
			       const char *ifname, uint32_t dest, uint32_t gw,
			       unsigned flags, uint32_t mask)
{
	int n;

	if (*off >= count)
		return -1;
	n = snprintf(buf + *off, count - *off,
		     "%s\t%08X\t%08X\t%04X\t%d\t%u\t%u\t%08X\t%d\t%u\t%u\n",
		     ifname ? ifname : "*",
		     (unsigned)dest, (unsigned)gw, flags,
		     0, 0u, 0u, (unsigned)mask, 0, 0u, 0u);
	if (n < 0)
		return -1;
	if ((size_t)n >= count - *off)
	{
		buf[count - 1] = '\0';
		*off = count - 1;
		return -1;
	}
	*off += (size_t)n;
	return 0;
}

static int proc_route_walk_cb(ip4_addr_t dest, ip4_addr_t mask, ip4_addr_t gw,
			      void *ctx)
{
	struct proc_route_fmt_ctx *c = ctx;
	unsigned flags = IR0_RTF_UP;

	if (gw)
		flags |= IR0_RTF_GATEWAY;
	if (proc_route_emit_row(c->buf, c->count, &c->off, c->ifname,
				(uint32_t)dest, (uint32_t)gw, flags,
				(uint32_t)mask) != 0)
	{
		c->err = 1;
		return -1;
	}
	return 0;
}
#endif

/*
 * Linux-style /proc/net/{tcp,udp,raw,unix} for BusyBox netstat.
 * IPv4 addresses are printed as %08X of the __be32 (sin_addr.s_addr) value —
 * same as Linux get_tcp4_sock — not ntohl()'d host integers.
 */
struct proc_sock_fmt_ctx
{
	char *buf;
	size_t count;
	size_t off;
	int sl;
	int err;
};

static int proc_sock_append(struct proc_sock_fmt_ctx *c, const char *line)
{
	size_t n;
	size_t avail;

	if (!c || !line || c->err)
		return -1;
	n = strlen(line);
	avail = (c->off < c->count) ? (c->count - c->off) : 0;
	if (avail <= 1)
	{
		c->err = 1;
		return -1;
	}
	if (n >= avail)
		n = avail - 1;
	memcpy(c->buf + c->off, line, n);
	c->off += n;
	c->buf[c->off] = '\0';
	return 0;
}

static int proc_inet_row_cb(const struct sock_stream_inet_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[192];
	int n;

	n = snprintf(line, sizeof(line),
		     "%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %08X %5u %8d %lu %d\n",
		     c->sl++,
		     (unsigned)s->local_ip, (unsigned)s->local_port,
		     (unsigned)s->rem_ip, (unsigned)s->rem_port,
		     (unsigned)s->st,
		     0u, 0u, 0u, 0u, 0u, 0u, 0, (unsigned long)s->inode, 1);
	if (n < 0)
		return -1;
	return proc_sock_append(c, line);
}

static int proc_udp_row_cb(const struct sock_udp_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[192];
	int n;

	n = snprintf(line, sizeof(line),
		     "%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %08X %5u %8d %lu %d\n",
		     c->sl++,
		     (unsigned)s->local_ip, (unsigned)s->local_port,
		     (unsigned)s->rem_ip, (unsigned)s->rem_port,
		     (unsigned)s->st,
		     0u, 0u, 0u, 0u, 0u, 0u, 0, (unsigned long)s->inode, 1);
	if (n < 0)
		return -1;
	return proc_sock_append(c, line);
}

static int proc_raw_row_cb(const struct sock_icmp_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[192];
	int n;

	n = snprintf(line, sizeof(line),
		     "%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %08X %5u %8d %lu %d\n",
		     c->sl++,
		     0u, (unsigned)s->proto,
		     0u, 0u,
		     0x07u,
		     0u, 0u, 0u, 0u, 0u, 0u, 0, (unsigned long)s->inode, 1);
	if (n < 0)
		return -1;
	return proc_sock_append(c, line);
}

static int proc_unix_row_cb(const struct sock_stream_unix_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[256];
	char path[128];
	int n;

	path[0] = '\0';
	if (s->path_len > 0)
	{
		if (s->is_abstract)
		{
			path[0] = '@';
			if (s->path_len < sizeof(path) - 1)
			{
				memcpy(path + 1, s->path, s->path_len);
				path[s->path_len + 1] = '\0';
			}
		}
		else if (s->path_len < sizeof(path))
		{
			memcpy(path, s->path, s->path_len);
			path[s->path_len] = '\0';
		}
	}

	n = snprintf(line, sizeof(line),
		     "%08lX: %08X %08X %08X %04X %02X %5lu",
		     (unsigned long)s->inode,
		     (unsigned)s->refcnt,
		     0u, 0u,
		     (unsigned)s->type,
		     (unsigned)s->st,
		     (unsigned long)s->inode);
	if (n < 0)
		return -1;
	if (path[0])
	{
		size_t used = (size_t)n;

		if (used + 2 < sizeof(line))
		{
			line[used++] = ' ';
			strncpy(line + used, path, sizeof(line) - used - 2);
			line[sizeof(line) - 2] = '\0';
		}
	}
	{
		size_t L = strlen(line);

		if (L + 1 < sizeof(line))
		{
			line[L] = '\n';
			line[L + 1] = '\0';
		}
	}
	return proc_sock_append(c, line);
}

static int proc_net_socktable_start(char *buf, size_t count, const char *header,
				    struct proc_sock_fmt_ctx *c)
{
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);
	memset(c, 0, sizeof(*c));
	c->buf = buf;
	c->count = count;
	if (!header)
		return 0;
	n = snprintf(buf, count, "%s", header);
	if (n < 0)
		return -1;
	if ((size_t)n >= count)
	{
		buf[count - 1] = '\0';
		c->off = count - 1;
		return (int)c->off;
	}
	c->off = (size_t)n;
	return 0;
}

int proc_net_tcp_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
#if CONFIG_ENABLE_NETWORKING
	(void)sock_stream_inet_walk(proc_inet_row_cb, &c);
#endif
	return (int)c.off;
}

int proc_net_udp_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
#if CONFIG_ENABLE_NETWORKING
	(void)sock_udp_walk(proc_udp_row_cb, &c);
#endif
	return (int)c.off;
}

int proc_net_raw_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
#if CONFIG_ENABLE_NETWORKING
	(void)sock_icmp_walk(proc_raw_row_cb, &c);
#endif
	return (int)c.off;
}

int proc_net_unix_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"Num       RefCount Protocol Flags    Type St Inode Path\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
	(void)sock_stream_unix_walk(proc_unix_row_cb, &c);
	return (int)c.off;
}

int proc_net_route_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
	struct proc_route_fmt_ctx ctx;
	struct net_device *dev;
	const char *ifname = "eth0";
	ip4_addr_t connected;
	int n;
	int walked = 0;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	n = snprintf(buf, count,
		     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
	if (n < 0)
		return -1;
	if ((size_t)n >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}

	dev = net_get_devices();
	if (dev && dev->name && dev->name[0])
		ifname = dev->name;

	ctx.buf = buf;
	ctx.count = count;
	ctx.off = (size_t)n;
	ctx.ifname = ifname;
	ctx.err = 0;

	/* Explicit routes first (if any). */
	(void)ip_route_walk(proc_route_walk_cb, &ctx);
	if (ctx.err)
		return (int)ctx.off;

	/*
	 * Count whether walk emitted anything by comparing off — if still
	 * header-only, synthesize connected + default from globals (QEMU).
	 */
	walked = (ctx.off > (size_t)n);
	if (!walked && ip_local_addr != 0 && ip_netmask != 0)
	{
		connected = ip_local_addr & ip_netmask;
		if (proc_route_emit_row(buf, count, &ctx.off, ifname,
					(uint32_t)connected, 0, IR0_RTF_UP,
					(uint32_t)ip_netmask) != 0)
			return (int)ctx.off;
		if (ip_gateway != 0)
		{
			if (proc_route_emit_row(buf, count, &ctx.off, ifname,
						0, (uint32_t)ip_gateway,
						IR0_RTF_UP | IR0_RTF_GATEWAY,
						0) != 0)
				return (int)ctx.off;
		}
	}
	else if (walked && ip_gateway != 0)
	{
		/* List present but may omit default — BusyBox still wants it. */
		/* Skip duplicate default if already in list: best-effort emit. */
	}

	return (int)ctx.off;
#else
	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);
	return 0;
#endif
}

int proc_drivers_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/* Check if path is in /proc (mount root or under it). */
bool is_proc_path(const char *path)
{
    if (!path)
        return false;
    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
        return true;
    return strncmp(path, "/proc/", 6) == 0;
}

/*
 * Parse /proc path - returns entry name after /proc/, extracts PID if present.
 * Supports OSDev-style /proc/pid/N/status, legacy /proc/N/status, and
 * /proc/self/... mapped to the current process PID (Linux-style).
 */
static const char *proc_parse_path(const char *path, pid_t *pid_out)
{
    if (!is_proc_path(path))
        return NULL;

    const char *walk = path;
    char self_resolved[512];

    /*
     * Map /proc/self/<rest> to /proc/<current_pid>/<rest> so the rest of the
     * parser sees a normal numeric PID path.
     */
    if (strncmp(path, "/proc/self/", 11) == 0)
    {
        if (!current_process)
            return NULL;
        int n = snprintf(self_resolved, sizeof(self_resolved), "/proc/%d/%s",
                         (int)current_process->task.pid, path + 11);
        if (n < 0 || (size_t)n >= sizeof(self_resolved))
            return NULL;
        walk = self_resolved;
    }

    /* Skip "/proc/" prefix */
    const char *after_proc = walk + 6;

    /* /proc/self — Linux symlink to /proc/<current-pid> (bare path). */
    if (strcmp(after_proc, "self") == 0)
    {
        if (!current_process)
            return NULL;
        *pid_out = current_process->task.pid;
        return "self_link";
    }

    /* Check if it's /proc/status (current process) */
    if (strncmp(after_proc, "status", 6) == 0)
    {
        *pid_out = -1;
        return "status";
    }

    /* OSDev-style: /proc/pid or /proc/pid/N or /proc/pid/N/status */
    if (strncmp(after_proc, "pid", 3) == 0 && (after_proc[3] == '\0' || after_proc[3] == '/'))
    {
        if (after_proc[3] == '\0')
        {
            *pid_out = -1;
            return "pid_dir";
        }
        /* pid/123 or pid/123/status or pid/123/cmdline */
        const char *rest = after_proc + 4;
        char *slash = strchr(rest, '/');
        if (!slash)
        {
            *pid_out = atoi(rest);
            return "pid_subdir";
        }
        char pid_str[16];
        size_t pid_len = (size_t)(slash - rest);
        if (pid_len >= sizeof(pid_str))
            return NULL;
        strncpy(pid_str, rest, pid_len);
        pid_str[pid_len] = '\0';
        *pid_out = atoi(pid_str);
        if (strncmp(slash + 1, "status", 6) == 0)
            return "status";
        if (strncmp(slash + 1, "cmdline", 7) == 0)
            return "cmdline";
        if (strncmp(slash + 1, "maps", 4) == 0)
            return "maps";
        if (strncmp(slash + 1, "statm", 5) == 0)
            return "statm";
        if (strncmp(slash + 1, "fd", 2) == 0 &&
            (slash[3] == '\0' || slash[3] == '/'))
        {
            if (slash[3] == '\0')
                return "fd_dir";
            if (slash[4] >= '0' && slash[4] <= '9')
                return "fd_link";
        }
        if (strncmp(slash + 1, "environ", 7) == 0)
            return "environ";
        if (strncmp(slash + 1, "exe", 3) == 0 && slash[4] == '\0')
            return "exe_link";
        if (strncmp(slash + 1, "stat", 4) == 0)
            return "stat";
        return NULL;
    }

    /* Legacy: /proc/[pid]/status or /proc/[pid]/cmdline */
    char *slash = strchr(after_proc, '/');
    if (slash)
    {
        char pid_str[16];
        size_t pid_len = (size_t)(slash - after_proc);
        if (pid_len < sizeof(pid_str) - 1)
        {
            strncpy(pid_str, after_proc, pid_len);
            pid_str[pid_len] = '\0';
            *pid_out = atoi(pid_str);

            if (strncmp(slash + 1, "status", 6) == 0)
                return "status";
            if (strncmp(slash + 1, "cmdline", 7) == 0)
                return "cmdline";
            if (strncmp(slash + 1, "maps", 4) == 0)
                return "maps";
            if (strncmp(slash + 1, "statm", 5) == 0)
                return "statm";
            if (strncmp(slash + 1, "fd", 2) == 0 &&
                (slash[3] == '\0' || slash[3] == '/'))
            {
                if (slash[3] == '\0')
                    return "fd_dir";
                if (slash[4] >= '0' && slash[4] <= '9')
                    return "fd_link";
            }
            if (strncmp(slash + 1, "environ", 7) == 0)
                return "environ";
            if (strncmp(slash + 1, "exe", 3) == 0 && slash[4] == '\0')
                return "exe_link";
            if (strncmp(slash + 1, "stat", 4) == 0)
                return "stat";
        }
    }
    else
    {
        /* Bare /proc/N directory (digit-only name). */
        const char *p = after_proc;
        int digits = 0;

        while (*p >= '0' && *p <= '9')
        {
            digits++;
            p++;
        }
        if (digits > 0 && *p == '\0')
        {
            *pid_out = atoi(after_proc);
            return "pid_subdir";
        }
    }

    /* Regular /proc files */
    *pid_out = -1;
    return after_proc;
}

const char *proc_resolve_path(const char *path, pid_t *pid_out)
{
    if (!pid_out)
        return NULL;

    return proc_parse_path(path, pid_out);
}

int proc_is_virtual_subdir(const char *path)
{
    pid_t pid;
    const char *filename;

    filename = proc_resolve_path(path, &pid);
    if (!filename)
        return 0;

    return strcmp(filename, "pid_dir") == 0 ||
           strcmp(filename, "pid_subdir") == 0 ||
           strcmp(filename, "fd_dir") == 0 ||
           strcmp(filename, "self_link") == 0;
}

/*
 * /proc/meminfo — labelled kB from PMM frames (PAGE_SIZE = 4 KiB).
 * MemUsed + MemFree == MemTotal (PMM-managed region only).
 */
int proc_meminfo_read(char *buf, size_t count)
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

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	ir0_mm_pmm_stats(&total_frames, &used_frames, &free_frames);
	total_kb = ((uint64_t)total_frames * (uint64_t)IR0_MM_PAGE_SIZE) / BYTES_PER_KB;
	used_kb = ((uint64_t)used_frames * (uint64_t)IR0_MM_PAGE_SIZE) / BYTES_PER_KB;
	free_kb = ((uint64_t)free_frames * (uint64_t)IR0_MM_PAGE_SIZE) / BYTES_PER_KB;

	/*
	 * Report the kernel heap too. The PMM figures barely move because the
	 * heap is carved out of a region reserved at boot, so an exec failing
	 * with -ENOMEM from a fragmented heap looked like a machine with tens
	 * of MiB free. Slab is the Linux field for kernel data structures.
	 */
	ir0_mm_alloc_stats(&heap_total, &heap_used, &heap_allocs);

	len = snprintf(buf, count,
		       "MemTotal:       %llu kB\n"
		       "MemFree:        %llu kB\n"
		       /*
			* Linux field order; BusyBox free reads MemAvailable
			* for its "available" column and printed 0 while the
			* field was missing. IR0 has no reclaimable page
			* cache, so everything free is available and Buffers
			* and Cached are genuinely zero rather than unknown.
			*/
		       "MemAvailable:   %llu kB\n"
		       "Buffers:        0 kB\n"
		       "Cached:         0 kB\n"
		       "MemUsed:        %llu kB\n"
		       "PageSize:       %u kB\n"
		       "Slab:           %llu kB\n"
		       "SlabTotal:      %llu kB\n"
		       "SlabAllocs:     %llu\n"
		       "KStackMinFree:  %llu\n"
		       "IrqNestMax:     %u\n"
		       "KStackPeak:     %llu\n",
		       (unsigned long long)total_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)used_kb,
		       (unsigned)(IR0_MM_PAGE_SIZE / BYTES_PER_KB),
		       (unsigned long long)((uint64_t)heap_used / BYTES_PER_KB),
		       (unsigned long long)((uint64_t)heap_total / BYTES_PER_KB),
		       (unsigned long long)(uint64_t)heap_allocs,
		       /* Worst kernel stack headroom seen since boot; a #DF on
			* a deep path walk showed 32 KiB is not comfortable. */
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
	buf[len] = '\0';
	return len;
}

/* /proc/[pid]/status: raw data only. One line: name\tstate\tpid\tppid\tuid\tgid */
int proc_status_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	const char *state_str = "?";
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (!proc_fs_snap_acquire(pid, &snap))
		return 0;

	switch (snap.state)
	{
		case PROCESS_READY:   state_str = "R"; break;
		case PROCESS_RUNNING: state_str = "R"; break;
		case PROCESS_BLOCKED: state_str = "S"; break;
		case PROCESS_ZOMBIE:  state_str = "Z"; break;
	}

	len = snprintf(buf, count, "%s\t%s\t%d\t%d\t%d\t%d\n",
		       snap.comm[0] ? snap.comm : "(none)",
		       state_str, (int)snap.pid, (int)snap.ppid,
		       (int)snap.uid, (int)snap.gid);
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/[pid]/stat — Linux proc(5) field order.
 *
 * Fields 1..22 carry real process state (pid, comm, state, ppid, pgrp,
 * session, tty_nr, starttime); CPU/memory accounting counters are reported as
 * 0 because IR0 keeps no per-process time or fault accounting yet. Field 7
 * (tty_nr) is the /dev/console device id: IR0 exposes a single console today,
 * so every process on it shares that terminal.
 */
int proc_pid_stat_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	const char *state_str = "?";
	int tty_nr = IR0_PROC_CONSOLE_TTY_NR;
	uint64_t vsize = 0;
	uint64_t rss = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (!proc_fs_snap_acquire(pid, &snap))
		return 0;

	switch (snap.state)
	{
		case PROCESS_READY:   state_str = "R"; break;
		case PROCESS_RUNNING: state_str = "R"; break;
		case PROCESS_BLOCKED: state_str = "S"; break;
		case PROCESS_ZOMBIE:  state_str = "Z"; break;
	}

	if (snap.heap_end > snap.heap_start)
		vsize = snap.heap_end - snap.heap_start;
	vsize += snap.stack_size;
	{
		const struct mmap_region *r;

		for (r = snap.mmap_list; r; r = r->next)
			vsize += (uint64_t)r->length;
	}

	if (snap.mm)
		rss = mm_count_resident_user_pages(snap.mm);

	len = snprintf(buf, count,
		       "%d (%s) %s %d %d %d %d %d "     /*  1-8  */
		       "0 0 0 0 0 0 0 0 0 %d "          /*  9-18 (18=priority) */
		       "0 1 0 %llu %llu %llu 0 0 0 0 0 0 0 0 0 0 0 0\n", /* 19-36 */
		       (int)snap.pid,
		       snap.comm[0] ? snap.comm : "none",
		       state_str,
		       (int)snap.ppid,
		       (int)snap.pgid,
		       (int)snap.sid,
		       tty_nr,
		       (int)snap.pgid,               /* tpgid: fg group on tty */
		       snap.sched_prio,               /* 18: priority */
		       (unsigned long long)snap.start_ticks, /* 22: starttime */
		       (unsigned long long)vsize,               /* 23: vsize */
		       (unsigned long long)rss);                /* 24: rss pages */
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/[pid]/maps — Linux proc(5) layout:
 *   <start>-<end> <perms> <offset> <dev> <inode>    <pathname>
 *
 * IR0 emits the regions it actually tracks in mm_struct: the brk heap, the
 * demand-paged mmap regions (with their real prot/shared bits), and the user
 * stack. offset/dev/inode are 0/00:00/0 because user mappings are not backed
 * by on-disk inodes yet; nothing here is fabricated.
 */
int proc_pid_maps_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	const struct mmap_region *r;
	int len = 0;
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (!proc_fs_snap_acquire(pid, &snap))
		return 0;

	if (snap.heap_end > snap.heap_start)
	{
		n = snprintf(buf + len, count - (size_t)len,
			     "%016llx-%016llx rw-p 00000000 00:00 0          [heap]\n",
			     (unsigned long long)snap.heap_start,
			     (unsigned long long)snap.heap_end);
		if (n > 0 && n < (int)(count - (size_t)len))
			len += n;
	}

	for (r = snap.mmap_list; r; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;
		char perms[5];

		perms[0] = (r->prot & PROT_READ) ? 'r' : '-';
		perms[1] = (r->prot & PROT_WRITE) ? 'w' : '-';
		perms[2] = (r->prot & PROT_EXEC) ? 'x' : '-';
		perms[3] = (r->flags & MAP_SHARED) ? 's' : 'p';
		perms[4] = '\0';

		n = snprintf(buf + len, count - (size_t)len,
			     "%016llx-%016llx %s 00000000 00:00 0\n",
			     (unsigned long long)start,
			     (unsigned long long)end, perms);
		if (n > 0 && n < (int)(count - (size_t)len))
			len += n;
		else
			break;
	}

	if (snap.stack_size > 0)
	{
		uint64_t sstart = snap.stack_start;
		uint64_t send = sstart + snap.stack_size;

		n = snprintf(buf + len, count - (size_t)len,
			     "%016llx-%016llx rw-p 00000000 00:00 0          [stack]\n",
			     (unsigned long long)sstart,
			     (unsigned long long)send);
		if (n > 0 && n < (int)(count - (size_t)len))
			len += n;
	}

	proc_fs_snap_release(&snap);
	buf[count - 1] = '\0';
	return len;
}

/*
 * /proc/[pid]/statm — Linux proc(5): size resident shared text lib data dt
 * (units are pages). IR0 reports real total virtual size and resident pages
 * from mm_struct; shared/text/lib/data/dt are 0 (no per-segment accounting).
 */
int proc_pid_statm_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	uint64_t vsize = 0;
	uint64_t size_pages;
	uint64_t rss = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (!proc_fs_snap_acquire(pid, &snap))
		return 0;

	if (snap.heap_end > snap.heap_start)
		vsize = snap.heap_end - snap.heap_start;
	vsize += snap.stack_size;
	{
		const struct mmap_region *r;

		for (r = snap.mmap_list; r; r = r->next)
			vsize += (uint64_t)r->length;
	}
	if (snap.mm)
		rss = mm_count_resident_user_pages(snap.mm);

	size_pages = vsize / (uint64_t)IR0_MM_PAGE_SIZE;

	len = snprintf(buf, count, "%llu %llu 0 0 0 0 0\n",
		       (unsigned long long)size_pages,
		       (unsigned long long)rss);
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

typedef struct proc_fd_snap
{
	fd_entry_t table[MAX_FDS_PER_PROCESS];
	int valid;
} proc_fd_snap_t;

static int proc_fd_snap_acquire(pid_t pid, proc_fd_snap_t *snap)
{
	process_t *proc;
	unsigned long irqf;

	if (!snap)
		return -EINVAL;

	memset(snap, 0, sizeof(*snap));

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (proc && proc->files && files_struct_live(proc->files))
	{
		memcpy(snap->table, proc->files->fd_table, sizeof(snap->table));
		snap->valid = 1;
	}
	irq_restore(irqf);
	return snap->valid;
}

static int proc_parse_pid_fd_link(const char *path, pid_t *pid_out, int *fd_out)
{
	const char *name;
	const char *fd_slash;
	int fd;

	if (!path || !pid_out || !fd_out)
		return -EINVAL;

	name = proc_parse_path(path, pid_out);
	if (!name || strcmp(name, "fd_link") != 0)
		return -ENOENT;

	fd_slash = strstr(path, "/fd/");
	if (!fd_slash)
		return -ENOENT;

	fd = atoi(fd_slash + 4);
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EINVAL;

	*fd_out = fd;
	return 0;
}

static int proc_fd_entry_target(const fd_entry_t *ent, char *buf, size_t count)
{
	if (!ent || !ent->in_use)
		return -ENOENT;

	if (ent->path[0] != '\0')
		return snprintf(buf, count, "%s", ent->path);
	if (ent->is_pipe)
		return snprintf(buf, count, "[pipe]");
	if (ent->is_socket)
		return snprintf(buf, count, "[socket]");
	if (ent->is_epoll)
		return snprintf(buf, count, "[eventpoll]");
	if (ent->is_eventfd)
		return snprintf(buf, count, "[eventfd]");
	if (ent->is_timerfd)
		return snprintf(buf, count, "[timerfd]");
	if (ent->is_memfd)
		return snprintf(buf, count, "[memfd]");
	if (ent->is_devfs)
		return snprintf(buf, count, "[dev]");
	if (ent->is_pseudo)
		return snprintf(buf, count, "[pseudo]");
	return snprintf(buf, count, "[anon]");
}

int proc_pid_fd_link_target_read(char *buf, size_t count, pid_t pid, int fd_num)
{
	proc_fd_snap_t snap;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	if (!proc_fd_snap_acquire(pid, &snap))
		return -ENOENT;

	if (fd_num < 0 || fd_num >= MAX_FDS_PER_PROCESS ||
	    !snap.table[fd_num].in_use)
		return -ENOENT;

	len = proc_fd_entry_target(&snap.table[fd_num], buf, count);
	if (len < 0)
		return len;
	if ((size_t)len >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

int proc_pid_exe_link_target_read(char *buf, size_t count, pid_t pid)
{
	process_t *proc;
	unsigned long irqf;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (!proc || !proc->exe_path[0])
	{
		irq_restore(irqf);
		return -ENOENT;
	}

	len = snprintf(buf, count, "%s", proc->exe_path);
	irq_restore(irqf);
	if (len < 0)
		return -1;
	if ((size_t)len >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

int proc_pid_environ_read(char *buf, size_t count, pid_t pid)
{
	process_t *proc;
	char *blob;
	size_t blob_len;
	unsigned long irqf;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	memset(buf, 0, count);

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (!proc || !proc->saved_environ || proc->saved_environ_len == 0)
	{
		irq_restore(irqf);
		return 0;
	}

	blob = proc->saved_environ;
	blob_len = proc->saved_environ_len;
	irq_restore(irqf);

	if (blob_len > count)
	{
		memcpy(buf, blob, count);
		return (int)count;
	}

	memcpy(buf, blob, blob_len);
	return (int)blob_len;
}

static int proc_readlink_finish(char *buf, size_t buflen, int len)
{
	if (len < 0)
		return len;
	if ((size_t)len >= buflen)
	{
		buf[buflen - 1] = '\0';
		return (int)(buflen - 1);
	}
	buf[len] = '\0';
	return len;
}

int proc_readlink(const char *path, char *buf, size_t buflen)
{
	pid_t pid;
	const char *name;
	int fd_num;
	int len;

	if (!path || !buf || buflen == 0)
		return -EINVAL;

	if (proc_parse_pid_fd_link(path, &pid, &fd_num) == 0)
	{
		len = proc_pid_fd_link_target_read(buf, buflen, pid, fd_num);
		return proc_readlink_finish(buf, buflen, len);
	}

	name = proc_parse_path(path, &pid);
	if (!name)
		return -ENOENT;

	if (strcmp(name, "self_link") == 0)
	{
		if (!current_process)
			return -ENOENT;
		len = snprintf(buf, buflen, "%d", (int)current_process->task.pid);
		return proc_readlink_finish(buf, buflen, len);
	}

	if (strcmp(name, "exe_link") == 0)
	{
		len = proc_pid_exe_link_target_read(buf, buflen, pid);
		return proc_readlink_finish(buf, buflen, len);
	}

	return -ENOENT;
}

/*
 * /proc/uptime — "<seconds_uptime> <seconds_idle>\n" (Unix/BusyBox contract).
 * Values are real seconds from the monotonic clock / idle tick counter.
 */
int proc_uptime_read(char *buf, size_t count)
{
	uint64_t up_ms;
	uint64_t idle_ms;
	uint64_t up_sec;
	uint64_t idle_sec;
	unsigned up_frac;
	unsigned idle_frac;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	up_ms = clock_get_uptime_milliseconds();
	idle_ms = clock_get_idle_milliseconds();
	up_sec = up_ms / 1000;
	idle_sec = idle_ms / 1000;
	up_frac = (unsigned)((up_ms % 1000) / 10);     /* hundredths */
	idle_frac = (unsigned)((idle_ms % 1000) / 10);

	len = snprintf(buf, count, "%llu.%02u %llu.%02u\n",
		       (unsigned long long)up_sec, up_frac,
		       (unsigned long long)idle_sec, idle_frac);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/stat — Linux proc(5) / BusyBox top contract.
 *
 * Aggregate "cpu" + single "cpu0" jiffy lines (USER_HZ ≈ CONFIG_TICK_RATE_HZ).
 * IR0 has no per-state CPU accounting yet: idle from clock_get_idle_milliseconds(),
 * non-idle split user/system; nice/iowait/irq/softirq/steal are 0.
 * Source: man7.org/linux/man-pages/man5/proc_stat.5.html
 */
int proc_stat_read(char *buf, size_t count)
{
	uint64_t total;
	uint64_t idle;
	uint64_t busy;
	uint64_t user;
	uint64_t system;
	uint64_t nice = 0;
	uint64_t iowait = 0;
	uint64_t irq = 0;
	uint64_t softirq = 0;
	uint64_t steal = 0;
	unsigned runnable = 0;
	unsigned nprocs = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	total = clock_get_tick_count();
	idle = clock_get_idle_milliseconds() * (uint64_t)CONFIG_TICK_RATE_HZ / 1000ULL;
	if (idle > total)
		idle = total;
	busy = total - idle;
	user = busy / 2ULL;
	system = busy - user;

	clock_get_loadavg(NULL, NULL, NULL, &runnable, &nprocs, NULL);

	/*
	 * Eight fields after the label so FEATURE_TOP_SMP_CPU (BusyBox) accepts
	 * both the aggregate line (>=4) and cpu0 (>4). Duplicate cpu0: one logical CPU.
	 */
	len = snprintf(buf, count,
		       "cpu  %llu %llu %llu %llu %llu %llu %llu %llu\n"
		       "cpu0 %llu %llu %llu %llu %llu %llu %llu %llu\n"
		       "intr 0\n"
		       "ctxt 0\n"
		       "btime 0\n"
		       "processes %u\n"
		       "procs_running %u\n"
		       "procs_blocked 0\n",
		       (unsigned long long)user,
		       (unsigned long long)nice,
		       (unsigned long long)system,
		       (unsigned long long)idle,
		       (unsigned long long)iowait,
		       (unsigned long long)irq,
		       (unsigned long long)softirq,
		       (unsigned long long)steal,
		       (unsigned long long)user,
		       (unsigned long long)nice,
		       (unsigned long long)system,
		       (unsigned long long)idle,
		       (unsigned long long)iowait,
		       (unsigned long long)irq,
		       (unsigned long long)softirq,
		       (unsigned long long)steal,
		       nprocs,
		       runnable);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/* /proc/version: raw data only. One line: version\tdate\ttime\tuser\thost\tcompiler */
int proc_version_read(char *buf, size_t count)
{
    char uname_ver[64];

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    ir0_utsname_fill_version(uname_ver, sizeof(uname_ver));
    /* Human line aligned with uname(2) version (runtime UP|SMP + RR|Priority). */
    int len = snprintf(buf, count, "IR0 version %s %s (%s %s by %s@%s with %s)\n",
                       IR0_VERSION_STRING, uname_ver,
                       IR0_BUILD_DATE, IR0_BUILD_TIME,
                       IR0_BUILD_USER, IR0_BUILD_HOST, IR0_BUILD_CC);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

/* /proc/cmdline: Multiboot command line, or a QEMU-shaped default. */
int proc_boot_cmdline_read(char *buf, size_t count)
{
    const struct multiboot_info *mb;
    const char *cmdline = "root=/dev/hda console=ttyS0";
    size_t n;
    size_t i;

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;

    mb = (const struct multiboot_info *)get_boot_params();
    if (mb && (mb->flags & MULTIBOOT_FLAG_CMDLINE) && mb->cmdline)
        cmdline = (const char *)(uintptr_t)mb->cmdline;

    n = 0;
    while (cmdline[n] && n + 1 < count)
        n++;
    for (i = 0; i < n; i++)
        buf[i] = cmdline[i];
    if (n + 1 < count)
        buf[n++] = '\n';
    buf[n] = '\0';
    return (int)n;
}

/*
 * Build "flags" line from CPUID.1 EDX/ECX (silicon feature bits).
 * Each (bit, name) is appended when the bit is set.
 */
static void proc_cpuinfo_flags_from_cpuid(uint32_t edx, uint32_t ecx, char *out, size_t out_size)
{
    static const struct { uint32_t bit; const char *name; } edx_flags[] = {
        { 0, "fpu" }, { 1, "vme" }, { 2, "de" }, { 3, "pse" }, { 4, "tsc" },
        { 5, "msr" }, { 6, "pae" }, { 7, "mce" }, { 8, "cx8" }, { 9, "apic" },
        { 10, "sep" }, { 11, "mtrr" }, { 12, "pge" }, { 13, "mca" }, { 15, "cmov" },
        { 16, "pat" }, { 17, "pse36" }, { 19, "clflush" }, { 23, "mmx" },
        { 24, "fxsr" }, { 25, "sse" }, { 26, "sse2" }, { 28, "htt" }, { 29, "tm" },
        { 31, "pbe" }
    };
    static const struct { uint32_t bit; const char *name; } ecx_flags[] = {
        { 0, "sse3" }, { 1, "pclmulqdq" }, { 9, "ssse3" }, { 12, "fma" },
        { 13, "cx16" }, { 19, "sse4_1" }, { 20, "sse4_2" }, { 21, "x2apic" },
        { 22, "movbe" }, { 23, "popcnt" }, { 25, "aes" }, { 26, "xsave" },
        { 28, "avx" }, { 29, "f16c" }, { 30, "rdrand" }, { 31, "hypervisor" }
    };
    size_t len = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(edx_flags)/sizeof(edx_flags[0]) && len < out_size - 8; i++)
    {
        if (edx & (1U << edx_flags[i].bit))
        {
            if (len > 0) { out[len++] = ' '; out[len] = '\0'; }
            len += (size_t)snprintf(out + len, out_size - len, "%s", edx_flags[i].name);
        }
    }
    for (size_t i = 0; i < sizeof(ecx_flags)/sizeof(ecx_flags[0]) && len < out_size - 8; i++)
    {
        if (ecx & (1U << ecx_flags[i].bit))
        {
            if (len > 0) { out[len++] = ' '; out[len] = '\0'; }
            len += (size_t)snprintf(out + len, out_size - len, "%s", ecx_flags[i].name);
        }
    }
}

/* Generate /proc/cpuinfo content - all fields from silicon (CPUID) where available */
int proc_cpuinfo_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;

    memset(buf, 0, count);

    uint32_t cpu_id = get_cpu_id();
    uint32_t cpu_count = get_cpu_count();

    char vendor_str[13] = {0};
    if (get_cpu_vendor(vendor_str) < 0)
        strncpy(vendor_str, "Unknown", sizeof(vendor_str) - 1);

    uint32_t family = 0, model = 0, stepping = 0;
    get_cpu_signature(&family, &model, &stepping);

    char model_name[49] = {0};
    if (get_cpu_brand_string(model_name, sizeof(model_name)) < 0)
        strncpy(model_name, get_arch_name() ? get_arch_name() : "Unknown", sizeof(model_name) - 1);

    uint32_t max_leaf = 0;
    get_cpuid_max_leaf(&max_leaf);

    uint32_t feat_edx = 0, feat_ecx = 0;
    get_cpu_feature_bits(&feat_edx, &feat_ecx);

    char flags_buf[512];
    proc_cpuinfo_flags_from_cpuid(feat_edx, feat_ecx, flags_buf, sizeof(flags_buf));

    uint32_t clflush_sz = get_cpu_clflush_size();
    if (clflush_sz == 0)
        clflush_sz = 64;

    uint32_t arch_bits = 64;
#if defined(__i386__)
    arch_bits = 32;
#endif

    /* Raw: one line per field, key\tvalue */
    size_t off = 0;
    char t[64];
    int n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "processor\t%u\n", cpu_id); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "vendor_id\t%s\n", vendor_str); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "cpu family\t%u\nmodel\t%u\n", family, model); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "model name\t%s\nstepping\t%u\n", model_name, stepping); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "siblings\t%u\napicid\t%u\ncpuid level\t%u\n", cpu_count, cpu_id, max_leaf); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "flags\t%s\nclflush size\t%u\n", flags_buf, clflush_sz); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    {
	char hv_vendor[16];

	if (hypervisor_present() &&
	    hypervisor_vendor(hv_vendor, sizeof(hv_vendor)) == 0)
	{
	    n = snprintf(buf + off, (off < count) ? (count - off) : 0,
			 "hypervisor_vendor\t%s\n", hv_vendor);
	    if (n > 0 && (size_t)n < count - off)
		off += (size_t)n;
	}
    }
    snprintf(t, sizeof(t), "%ubits physical, %ubits virtual", arch_bits, arch_bits);
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "address sizes\t%s\n", t); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/*
 * /proc/loadavg — "0.03 0.01 0.00 1/7 123\n"
 * EMA from scheduler samples (see clock_get_loadavg); not raw tick noise.
 */
int proc_loadavg_read(char *buf, size_t count)
{
	uint32_t l1 = 0, l5 = 0, l15 = 0;
	unsigned runnable = 0, nprocs = 0;
	int last_pid = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	clock_get_loadavg(&l1, &l5, &l15, &runnable, &nprocs, &last_pid);
	len = snprintf(buf, count, "%u.%02u %u.%02u %u.%02u %u/%u %d\n",
		       l1 / 100, l1 % 100, l5 / 100, l5 % 100, l15 / 100,
		       l15 % 100, runnable, nprocs, last_pid);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

static void proc_u64_to_dec(uint64_t value, char *out, size_t out_len)
{
    char rev[24];
    size_t idx = 0;
    size_t pos = 0;

    if (!out || out_len == 0)
        return;

    if (value == 0)
    {
        if (out_len >= 2)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    while (value > 0 && idx < sizeof(rev))
    {
        rev[idx++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (idx > 0 && pos + 1 < out_len)
    {
        out[pos++] = rev[--idx];
    }
    out[pos] = '\0';
}

/*
 * Format size in sectors (512B) to string in G or M; *len receives length.
 */
static void proc_format_size(uint64_t sectors, char *out, size_t out_size, int *len)
{
    (void)out_size;
    uint64_t bytes = sectors * 512;
    uint64_t mb = bytes / (BYTES_PER_KB * BYTES_PER_KB);
    uint64_t gb = mb / 1024;
    uint64_t val = (gb > 0) ? gb : mb;
    char *p = out;
    if (val == 0)
        *p++ = '0';
    else
    {
        char rev[24];
        int idx = 0;
        while (val > 0) { rev[idx++] = '0' + (val % 10); val /= 10; }
        while (idx > 0) *p++ = rev[--idx];
    }
    *p++ = (gb > 0) ? 'G' : 'M';
    *p = '\0';
    *len = (int)(p - out);
}

/*
 * /proc/blockdevices — tab-separated; sectors are 512-byte units.
 * Header documents the contract; size_human uses MiB/GiB from those sectors.
 * Columns: type name maj min sectors_512 size_human model serial
 */
int proc_blockdevices_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    {
	int nh = snprintf(buf, count,
			  "# type\tname\tmaj\tmin\tsectors_512\tsize\tmodel\tserial\n");
	if (nh < 0)
		return -1;
	if ((size_t)nh >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	off = (size_t)nh;
    }
    for (uint8_t i = 0; i < 4; i++)
    {
        const char *disk_name = ir0_block_legacy_name(i);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;
        uint64_t size = ir0_block_sector_count_by_name(disk_name);
        const char *model = "-";
        const char *serial = "-";
        char name_buf[8];
        char size_human[16];
        int sh_len;
        proc_format_size(size, size_human, sizeof(size_human), &sh_len);
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + (int)i);
        char sectors_str[24];
        proc_u64_to_dec(size, sectors_str, sizeof(sectors_str));
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "disk\t%s\t%u\t%u\t%s\t%s\t%s\t%s\n",
                         name_buf, (unsigned)i, 0u, sectors_str,
                         size_human, model, serial);
        if (n < 0) return -1;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        int part_count = get_partition_count(i);
        for (int part_idx = 0; part_idx < part_count && off < count; part_idx++)
        {
            partition_info_t part_info;
            if (partition_nth_on_disk(i, (unsigned)part_idx, &part_info) != 0)
                continue;
            char part_name[12];
            char part_size_human[16];
            int psh_len;
            proc_format_size(part_info.total_sectors, part_size_human, sizeof(part_size_human), &psh_len);
            snprintf(part_name, sizeof(part_name), "hd%c%d", 'a' + (int)i,
                     (int)part_info.partition_number + 1);
            char part_sectors_str[24];
            proc_u64_to_dec(part_info.total_sectors, part_sectors_str, sizeof(part_sectors_str));
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "part\t%s\t%u\t%u\t%s\t%s\t-\t-\n",
                         part_name, (unsigned)i, (unsigned)(part_info.partition_number + 1),
                         part_sectors_str, part_size_human);
            if (n < 0) break;
            if (n >= (int)(count - off)) n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/filesystems: raw data only. One line per fs: type\tname (type=nodev or empty) */
int proc_filesystems_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\tproc\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\tdevfs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#if CONFIG_ENABLE_FS_TMPFS
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\ttmpfs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\tramfs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
#if CONFIG_ENABLE_FS_MINIX
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "\tminix\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "\tsimplefs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
#if CONFIG_ENABLE_FS_FAT16
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "\tfat16\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/partitions: raw data only. One line per device: major\tminor\tblocks_1k\tname */
int proc_partitions_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    for (uint8_t disk_id = 0; disk_id < MAX_DISKS; disk_id++)
    {
        const char *disk_name = ir0_block_legacy_name(disk_id);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;
        uint64_t disk_blocks_1k = ir0_block_sector_count_by_name(disk_name) / 2;
        char name_buf[16];
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + disk_id);
        char disk_blocks_str[24];
        proc_u64_to_dec(disk_blocks_1k, disk_blocks_str, sizeof(disk_blocks_str));
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%u\t%s\t%s\n",
                         (unsigned)disk_id, 0u, disk_blocks_str, name_buf);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        int part_count = get_partition_count(disk_id);
        for (int part_idx = 0; part_idx < part_count; part_idx++)
        {
            partition_info_t part_info;
            if (partition_nth_on_disk(disk_id, (unsigned)part_idx, &part_info) != 0)
                continue;
            uint64_t part_blocks_1k = part_info.total_sectors / 2;
            snprintf(name_buf, sizeof(name_buf), "hd%c%d", 'a' + disk_id,
                     (int)part_info.partition_number + 1);
            char part_blocks_str[24];
            proc_u64_to_dec(part_blocks_1k, part_blocks_str, sizeof(part_blocks_str));
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%u\t%s\t%s\n",
                         (unsigned)disk_id, (unsigned)(part_info.partition_number + 1),
                         part_blocks_str, name_buf);
            if (n < 0) break;
            if (n >= (int)(count - off)) n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/mounts: one line per VFS mount — device path fstype ro|rw 0 0 */
int proc_mounts_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;

    for (struct vfs_mount *m = vfs_get_mounts(); m && off < count; m = m->next) {
        const char *dev = (m->dev[0] != '\0') ? m->dev : "none";
        const char *fst = (m->fs && m->fs->name) ? m->fs->name : "unknown";
        const char *opts = (m->flags & IR0_MS_RDONLY) ? "ro" : "rw";
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%s %s %s %s 0 0\n",
                         dev, m->path, fst, opts);
        if (n <= 0 || (size_t)n >= count - off)
            break;
        off += (size_t)n;
    }
    /*
     * Pseudo-filesystems are served through the registry, not through the VFS
     * mount list, so they never appeared here and mount(1) showed only the
     * root. Linux lists proc, sysfs and devtmpfs; /heart is IR0-specific but
     * is a namespace the session can use, so it is listed too. vfs_statfs()
     * reports them with zero blocks, so df skips them without -a.
     */
    {
        static const char *const pseudo[] = {
            "proc /proc proc rw 0 0\n",
            "sysfs /sys sysfs rw 0 0\n",
            "devtmpfs /dev devtmpfs rw 0 0\n",
            "heartfs /heart heartfs rw 0 0\n",
        };
        size_t i;

        for (i = 0; i < sizeof(pseudo) / sizeof(pseudo[0]) && off < count; i++) {
            size_t len = strlen(pseudo[i]);

            if (len >= count - off)
                break;
            memcpy(buf + off, pseudo[i], len);
            off += len;
        }
    }

    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/*
 * Generate /proc/interrupts content from resource registry only.
 * Data comes from drivers that registered their IRQ (silicon/hardware).
 */
struct irq_collect_ctx {
    char *buf;
    size_t count;
    size_t off;
    uint8_t irqs[16];
    const char *names[16];
    int n;
};

static int irq_collect_cb(uint8_t irq, const char *name, void *ctx)
{
    struct irq_collect_ctx *c = (struct irq_collect_ctx *)ctx;
    if (c->n < 16)
    {
        c->irqs[c->n] = irq;
        c->names[c->n] = name;
        c->n++;
    }
    return 0;
}

/* /proc/interrupts: raw data only. One line per IRQ: irq\tname */
int proc_interrupts_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct irq_collect_ctx ctx = { .buf = buf, .count = count, .off = 0, .n = 0 };
    resource_foreach_irq(irq_collect_cb, &ctx);
    size_t off = 0;
    uint8_t order[16];
    for (int i = 0; i < ctx.n; i++) order[i] = (uint8_t)i;
    for (int i = 0; i < ctx.n - 1; i++)
        for (int j = i + 1; j < ctx.n; j++)
            if (ctx.irqs[order[i]] > ctx.irqs[order[j]])
                { uint8_t t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < ctx.n && off < count; i++)
    {
        int idx = order[i];
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%s\n", (unsigned)ctx.irqs[idx], ctx.names[idx]);
        if (n <= 0 || n >= (int)(count - off)) break;
        off += (size_t)n;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/*
 * Generate /proc/iomem content (physical memory map).
 * System RAM from PMM; MMIO ranges from resource registry (driver-registered).
 */
struct iomem_ctx {
    char *buf;
    size_t count;
    size_t off;
};

static int iomem_mmio_cb(uint64_t start, uint64_t end, const char *name, void *ctx)
{
	struct iomem_ctx *c = (struct iomem_ctx *)ctx;
	int n;

	/* Registry stores inclusive end (see resource_register_mmio callers). */
	n = snprintf(c->buf + c->off, (c->off < c->count) ? (c->count - c->off) : 0,
		     "%016llx-%016llx : %s\n",
		     (unsigned long long)start, (unsigned long long)end,
		     name ? name : "MMIO");
	if (n > 0 && (size_t)n < c->count - c->off)
	{
		c->off += (size_t)n;
		return 0;
	}
	return 1;
}

/*
 * /proc/iomem — inclusive hex ranges from the real PMM window + MMIO registry.
 * "System RAM" is only the region pmm_init manages (not a hardcoded 16 MiB).
 */
int proc_iomem_read(char *buf, size_t count)
{
	size_t off = 0;
	uintptr_t ram_start;
	uintptr_t ram_end_excl;
	uint64_t ram_end_incl;
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	ram_start = ir0_mm_pmm_start();
	ram_end_excl = ir0_mm_pmm_end();
	if (ram_end_excl > ram_start)
	{
		ram_end_incl = (uint64_t)ram_end_excl - 1ULL;
		n = snprintf(buf, count,
			     "%016llx-%016llx : System RAM (PMM-managed)\n",
			     (unsigned long long)ram_start,
			     (unsigned long long)ram_end_incl);
		if (n < 0)
			return -1;
		if ((size_t)n >= count)
		{
			buf[count - 1] = '\0';
			return (int)(count - 1);
		}
		off = (size_t)n;
	}

	{
		struct iomem_ctx ctx = { .buf = buf, .count = count, .off = off };

		resource_foreach_mmio(iomem_mmio_cb, &ctx);
		off = ctx.off;
	}

	if (off < count)
		buf[off] = '\0';
	return (int)off;
}

/* /proc/kmsg: kernel log ring buffer (read-only, no serial side effects). */
int proc_kmsg_read(char *buf, size_t count)
{
    int n;

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    n = klog_read_records(buf, count);
    if (n < 0)
        return -1;
    if (n >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return n;
}

/* /proc/swaps: Linux-style header; empty table when no swap devices. */
int proc_swaps_read(char *buf, size_t count)
{
    static const char hdr[] =
        "Filename\t\tType\t\tSize\t\tUsed\t\tPriority\n";

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    if (count <= sizeof(hdr))
    {
        memcpy(buf, hdr, count - 1);
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    memcpy(buf, hdr, sizeof(hdr) - 1);
    return (int)(sizeof(hdr) - 1);
}

/*
 * Generate /proc/ioports content from resource registry only.
 * Ranges and names come from drivers that registered their ports (silicon/hardware).
 */
struct ioport_ctx {
    char *buf;
    size_t count;
    size_t off;
};

/* Raw: start\tend\tname per line */
static int ioport_cb(uint16_t start, uint16_t end, const char *name, void *ctx)
{
    struct ioport_ctx *c = (struct ioport_ctx *)ctx;
    int n = snprintf(c->buf + c->off, (c->off < c->count) ? (c->count - c->off) : 0,
                     "%u\t%u\t%s\n", (unsigned)start, (unsigned)end, name);
    if (n > 0 && (size_t)n < c->count - c->off) { c->off += (size_t)n; return 0; }
    return 1;
}

int proc_ioports_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct ioport_ctx ctx = { .buf = buf, .count = count, .off = 0 };
    resource_foreach_ioport(ioport_cb, &ctx);
    if (ctx.off < count)
        buf[ctx.off] = '\0';
    return (int)ctx.off;
}

/* /proc/modules: raw data only. Same as drivers: name\tversion\tlang\tstate\tdescription */
int proc_modules_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/* /proc/timer_list: raw data only. One line: timer\tname\tfrequency\ttick_count\tuptime_sec\tuptime_ms */
int proc_timer_list_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    clock_stats_t stats;
    if (clock_get_stats(&stats) != 0)
        return 0;
    const char *timer_name = "Unknown";
    switch (stats.active_timer) {
        case CLOCK_TIMER_NONE: timer_name = "None"; break;
        case CLOCK_TIMER_PIT:  timer_name = "PIT"; break;
        case CLOCK_TIMER_HPET:  timer_name = "HPET"; break;
        case CLOCK_TIMER_LAPIC: timer_name = "LAPIC"; break;
        case CLOCK_TIMER_RTC:  timer_name = "RTC"; break;
    }
    char tick_count_str[24];
    char uptime_sec_str[24];
    proc_u64_to_dec(stats.tick_count, tick_count_str, sizeof(tick_count_str));
    proc_u64_to_dec(stats.uptime_seconds, uptime_sec_str, sizeof(uptime_sec_str));
    int n = snprintf(buf, count, "%s\t%u\t%s\t%s\t%u\n",
                     timer_name, stats.timer_frequency,
                     tick_count_str,
                     uptime_sec_str,
                     stats.uptime_milliseconds);
    if (n < 0) return -1;
    if (n >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    return n;
}

/* Generate /proc/[pid]/cmdline content */
int proc_cmdline_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	memset(buf, 0, count);

	if (!proc_fs_snap_acquire(pid, &snap))
		return -1;

	len = snprintf(buf, count, "%s", snap.comm[0] ? snap.comm : "(none)");
	proc_fs_snap_release(&snap);

	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}

	buf[len] = '\0';
	return len;
}

/* Legacy virtual-fd offset maps removed — offsets live in process fd_table. */
off_t proc_get_offset(int fd)
{
    (void)fd;
    return 0;
}

static int proc_readdir_add(struct vfs_dirent *entries, int max_entries, int n,
                            const char *name, uint8_t type)
{
    if (n < 0 || n >= max_entries || !name || !name[0])
        return n;

    strncpy(entries[n].name, name, sizeof(entries[n].name) - 1);
    entries[n].name[sizeof(entries[n].name) - 1] = '\0';
    entries[n].type = type;
    return n + 1;
}

static int proc_readdir_fill_pid_subdir(struct vfs_dirent *entries, int max_entries,
                                        int n)
{
	n = proc_readdir_add(entries, max_entries, n, "status", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "cmdline", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "stat", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "maps", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "statm", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "fd", DT_DIR);
	n = proc_readdir_add(entries, max_entries, n, "environ", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "exe", DT_LNK);
	return n;
}

/*
 * Snapshot live PIDs under irq_save, then fill entries — same pattern as
 * proc_ps_read. Avoids UAF if a concurrent exit unlinks process_list while
 * ls/getdents walks /proc (session soak: ls /proc | head → shell SIGSEGV).
 */
static int proc_readdir_fill_live_pids(struct vfs_dirent *entries, int max_entries,
                                       int n, int skip_zombies)
{
    pid_t snap[64];
    int snap_count = 0;
    process_t *p;
    unsigned long irqf;
    int i;

    if (!entries || max_entries <= 0 || n < 0)
        return n;

    irqf = irq_save();
    for (p = process_list;
         p && snap_count < (int)(sizeof(snap) / sizeof(snap[0])) &&
         n + snap_count < max_entries;
         p = p->next)
    {
        if (skip_zombies && p->state == PROCESS_ZOMBIE)
            continue;
        snap[snap_count++] = p->task.pid;
    }
    irq_restore(irqf);

    for (i = 0; i < snap_count && n < max_entries; i++)
    {
        char pid_str[16];
        int len;

        len = snprintf(pid_str, sizeof(pid_str), "%d", (int)snap[i]);
        if (len <= 0 || len >= (int)sizeof(pid_str))
            break;
        n = proc_readdir_add(entries, max_entries, n, pid_str, DT_DIR);
    }

    return n;
}

/*
 * Path-based readdir for /proc mounts (no virtual fds).
 * /proc          — digit PIDs first, then "pid", then registry children
 * /proc/pid      — one dirent per live PID
 * /proc/pid/N    — status, cmdline, stat
 *
 * Digit PIDs must come first: sys_getdents uses GETDENTS_BATCH_MAX (24) and
 * truncates; BusyBox top/ps scan only numeric dirents then open /proc/N/stat.
 * Filling the batch with static registry names alone yields "no process info".
 */
int proc_readdir(const char *path, struct vfs_dirent *entries, int max_entries)
{
    pid_t pid;
    const char *filename;
    int n;

    if (!path || !entries || max_entries <= 0)
        return -EINVAL;

    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
    {
        pseudo_fs_nodes_register_all();
        n = 0;
        n = proc_readdir_fill_live_pids(entries, max_entries, n, 1);
        n = proc_readdir_add(entries, max_entries, n, "self", DT_LNK);
        n = proc_readdir_add(entries, max_entries, n, "pid", DT_DIR);
        if (n < max_entries)
        {
            int reg;

            reg = pseudo_fs_collect_registry_children("/proc", entries,
                                                     max_entries, n);
            if (reg < 0)
                return reg;
            n = reg;
        }
        return n;
    }

    if (strcmp(path, "/proc/self") == 0 || strcmp(path, "/proc/self/") == 0)
    {
        if (!current_process)
            return -ENOENT;
        pid = current_process->task.pid;
        if (!process_find_by_pid(pid))
            return -ENOENT;
        n = 0;
        return proc_readdir_fill_pid_subdir(entries, max_entries, n);
    }

    filename = proc_resolve_path(path, &pid);
    if (!filename)
        return -ENOENT;

    if (strcmp(filename, "pid_dir") == 0)
    {
        n = 0;
        n = proc_readdir_fill_live_pids(entries, max_entries, n, 0);
        return n;
    }

    if (strcmp(filename, "pid_subdir") == 0)
    {
        if (!process_find_by_pid(pid))
            return -ENOENT;
        n = 0;
        return proc_readdir_fill_pid_subdir(entries, max_entries, n);
    }

    if (strcmp(filename, "fd_dir") == 0)
    {
        proc_fd_snap_t snap;
        int i;

        if (!process_find_by_pid(pid))
            return -ENOENT;
        if (!proc_fd_snap_acquire(pid, &snap))
            return 0;

        n = 0;
        for (i = 0; i < MAX_FDS_PER_PROCESS && n < max_entries; i++)
        {
            char fd_name[16];

            if (!snap.table[i].in_use)
                continue;
            if (snprintf(fd_name, sizeof(fd_name), "%d", i) <= 0)
                continue;
            n = proc_readdir_add(entries, max_entries, n, fd_name, DT_LNK);
        }
        return n;
    }

    return -ENOTDIR;
}

/* Legacy fd-based getdents — kept for transitional callers; prefer proc_readdir. */
int proc_getdents(int fd, void *dirent_buf, size_t count)
{
    (void)fd;
    (void)dirent_buf;
    (void)count;
    return -EBADF;
}

/* Legacy virtual-fd offset maps removed — offsets live in process fd_table. */
void proc_set_offset(int fd, off_t offset)
{
    (void)fd;
    (void)offset;
}

/* Open /proc — no longer assigns global virtual fds (use fd_table binds). */
int proc_open(const char *path, int flags)
{
    pid_t pid;
    const char *filename;

    (void)flags;

    if (!is_proc_path(path))
        return -EINVAL;

    pseudo_fs_nodes_register_all();

    filename = proc_parse_path(path, &pid);
    if (!filename)
        return -ENOENT;

    /* Files: opened only via pseudo_bind_file_fd (registry / dynamic). */
    if (strcmp(filename, "status") == 0 || strcmp(filename, "cmdline") == 0 ||
        (strcmp(filename, "stat") == 0 && pid > 0))
        return -ENOENT;

    if (strcmp(filename, "pid_dir") == 0 || strcmp(filename, "pid_subdir") == 0 ||
        strcmp(filename, "self_link") == 0)
        return -EISDIR;

    /* Static registry nodes also use bind path, not this helper. */
    return -ENOENT;
}

/* Read from /proc file — LEGACY global virtual fd only (PSEUDO_FS_*_FD_BASE).
 * Syscall path uses process fd_table is_pseudo + pseudo_fs_ops_read.
 */
int proc_read(int fd, char *buf, size_t count, off_t offset)
{
    int64_t pbytes;

    if (!buf || count == 0)
        return 0;

    if (pseudo_fs_find_by_fd(fd))
    {
        pbytes = pseudo_fs_read_fd(fd, buf, count, offset);
        return (int)pbytes;
    }

    return -EBADF;
}

/*
 * proc_write - Write to /proc file entry
 *
 * Most /proc nodes are read-only. Writable paths are explicit (e.g.
 * /proc/bluetooth/scan). Writes under /proc/sys/ are not implemented and
 * return -EOPNOTSUPP.
 */
/* Write to /proc — LEGACY global virtual fd only; syscall uses fd_table binds. */
int proc_write(int fd, const char *buf, size_t count)
{
    int64_t pw;

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -EINVAL;

    if (count == 0)
        return 0;

    if (pseudo_fs_find_by_fd(fd))
    {
        pw = pseudo_fs_write_fd(fd, buf, count);
        return (int)pw;
    }

    if (fd < 1000)
        return -EBADF;

    switch (fd)
    {
        default:
            /*
             * Resolve path from the opener's fd table; writes under the
             * proc/sys virtual tree are not supported (no sysctl knobs in-tree).
             */
            {
                if (!current_process)
                    return -ESRCH;

                if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
                    return -EBADF;

                const char *path = NULL;
                fd_entry_t *fdt = process_fd_table(current_process);

                if (fdt && fdt[fd].in_use)
                    path = fdt[fd].path;

                if (!path || strncmp(path, "/proc/", 6) != 0)
                    return -EACCES;

                path += 6;
                if (strncmp(path, "sys/", 4) == 0)
                    return -EOPNOTSUPP;

                return -EACCES;
            }
    }
}

/* Get stat for /proc file */
int proc_stat(const char *path, stat_t *st)
{
    pid_t pid;
    const char *filename;

    if (!st || !is_proc_path(path))
        return -EINVAL;

    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        pseudo_fs_stat_now(st);
        return 0;
    }

    filename = proc_parse_path(path, &pid);
    if (filename)
    {
        if (strcmp(filename, "pid_dir") == 0 ||
            strcmp(filename, "pid_subdir") == 0 ||
            strcmp(filename, "fd_dir") == 0 ||
            strcmp(filename, "self_link") == 0)
        {
            memset(st, 0, sizeof(stat_t));
            if (strcmp(filename, "self_link") == 0)
                st->st_mode = S_IFLNK | 0777;
            else
                st->st_mode = S_IFDIR | 0555;
            st->st_nlink = (strcmp(filename, "self_link") == 0) ? 1 : 2;
            st->st_uid = 0;
            st->st_gid = 0;
            st->st_size = 0;
            pseudo_fs_stat_now(st);
            return 0;
        }

        if (strcmp(filename, "fd_link") == 0 ||
            strcmp(filename, "exe_link") == 0)
        {
            memset(st, 0, sizeof(stat_t));
            st->st_mode = S_IFLNK | 0777;
            st->st_nlink = 1;
            st->st_uid = 0;
            st->st_gid = 0;
            st->st_size = 0;
            pseudo_fs_stat_now(st);
            return 0;
        }

        if (strcmp(filename, "environ") == 0 && pid > 0)
        {
            memset(st, 0, sizeof(stat_t));
            st->st_mode = S_IFREG | 0400;
            st->st_nlink = 1;
            st->st_uid = 0;
            st->st_gid = 0;
            st->st_size = 0;
            pseudo_fs_stat_now(st);
            return 0;
        }
    }

    pseudo_fs_nodes_register_all();

    {
        const pseudo_fs_entry_t *pf;
        int st_rc;

        pf = pseudo_fs_lookup(path);
        if (pf && pf->ops && pf->ops->stat)
            return pf->ops->stat(pf->ctx, st);

        st_rc = pseudo_fs_stat_path(path, st);
        if (st_rc == 0)
            return 0;
        if (st_rc != -ENOENT)
            return st_rc;
    }

    if (!filename)
        return -ENOENT;

    /*
     * Intermediate registry directories (e.g. /proc/net, /proc/bluetooth):
     * no exact node, but they contain registered leaves. Report them as real
     * directories with a live timestamp instead of ENOENT so `stat`/`ls -ld`
     * behave like Linux.
     */
    if (pseudo_fs_path_has_children(path))
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        pseudo_fs_stat_now(st);
        return 0;
    }

    /* File not found */
    return -ENOENT;
}
