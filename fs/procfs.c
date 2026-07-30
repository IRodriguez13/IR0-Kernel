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
#include <ir0/process.h>
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
#include <ir0/logging.h>

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
 * /proc/ps: one line header then one line per process, tab-separated.
 * Header: PID\tPPID\tS\tUID\tCMD
 * State: R=runnable/running, S=sleeping(blocked), Z=zombie (§8).
 */
int proc_ps_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    int n;

    n = snprintf(buf + off, count - off, "PID\tPPID\tS\tUID\tCMD\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;

    process_t *p = process_list;
    while (p && off < count - 1)
    {
        const char *state_str;
        if (process_is_zombie(p))
            state_str = "Z";
        else
        {
            switch (p->state)
            {
            case PROCESS_READY:   state_str = "R"; break;
            case PROCESS_RUNNING: state_str = "R"; break;
            case PROCESS_BLOCKED: state_str = "S"; break;
            case PROCESS_ZOMBIE:  state_str = "Z"; break;
            default:              state_str = "?"; break;
            }
        }
        const char *name = p->comm[0] ? p->comm : "(none)";
        n = snprintf(buf + off, count - off,
                         "%d\t%d\t%s\t%u\t%s\n",
                         (int)p->task.pid, (int)p->ppid, state_str,
                         (unsigned)p->uid, name);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        p = p->next;
    }
    if (off < count) buf[off] = '\0';
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
    int n = snprintf(buf, count,
                     "Inter-|   Receive                                                |  Transmit\n"
                     " face |   packets    errs                                        |  packets    errs\n");
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
        char rxp_str[24];
        char rxe_str[24];
        char txp_str[24];
        char txe_str[24];

        if (dev->get_stats)
            dev->get_stats(dev, &rxp, &txp, &rxe, &txe);

        proc_u64_to_dec(rxp, rxp_str, sizeof(rxp_str));
        proc_u64_to_dec(rxe, rxe_str, sizeof(rxe_str));
        proc_u64_to_dec(txp, txp_str, sizeof(txp_str));
        proc_u64_to_dec(txe, txe_str, sizeof(txe_str));
        n = snprintf(buf + off, count - off, "  %s: %s %s                                          %s %s\n",
                     (dev->name && dev->name[0] != '\0') ? dev->name : "eth0",
                     rxp_str, rxe_str, txp_str, txe_str);
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
 * Empty Linux-style socket tables so BusyBox netstat does not fail open().
 * Rows can be filled later from sock_stream / sock_udp inventories.
 */
static int proc_net_socktable_empty(char *buf, size_t count, const char *header)
{
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);
	if (!header)
		return 0;
	n = snprintf(buf, count, "%s", header);
	if (n < 0)
		return -1;
	if ((size_t)n >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return n;
}

int proc_net_tcp_read(char *buf, size_t count)
{
	return proc_net_socktable_empty(
		buf, count,
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
}

int proc_net_udp_read(char *buf, size_t count)
{
	return proc_net_socktable_empty(
		buf, count,
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
}

int proc_net_raw_read(char *buf, size_t count)
{
	return proc_net_socktable_empty(
		buf, count,
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
}

int proc_net_unix_read(char *buf, size_t count)
{
	return proc_net_socktable_empty(
		buf, count,
		"Num       RefCount Protocol Flags    Type St Inode Path\n");
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
           strcmp(filename, "pid_subdir") == 0;
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

	len = snprintf(buf, count,
		       "MemTotal:       %llu kB\n"
		       "MemFree:        %llu kB\n"
		       "MemUsed:        %llu kB\n"
		       "PageSize:       %u kB\n",
		       (unsigned long long)total_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)used_kb,
		       (unsigned)(IR0_MM_PAGE_SIZE / BYTES_PER_KB));
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
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    process_t *proc = (pid == -1) ? current_process : process_find_by_pid(pid);
    if (!proc)
        return 0;
    const char *state_str = "?";
    switch (proc->state) {
        case PROCESS_READY:   state_str = "R"; break;
        case PROCESS_RUNNING: state_str = "R"; break;
        case PROCESS_BLOCKED: state_str = "S"; break;
        case PROCESS_ZOMBIE:  state_str = "Z"; break;
    }
    int len = snprintf(buf, count, "%s\t%s\t%d\t%d\t%d\t%d\n",
                       proc->comm[0] ? proc->comm : "(none)",
                       state_str, (int)proc->task.pid, (int)proc->ppid,
                       (int)proc->uid, (int)proc->gid);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
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
    process_t *proc;
    const char *state_str = "?";
    int tty_nr = IR0_PROC_CONSOLE_TTY_NR;
    int len;

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);

    proc = (pid == -1) ? current_process : process_find_by_pid(pid);
    if (!proc)
        return 0;

    switch (proc->state)
    {
        case PROCESS_READY:   state_str = "R"; break;
        case PROCESS_RUNNING: state_str = "R"; break;
        case PROCESS_BLOCKED: state_str = "S"; break;
        case PROCESS_ZOMBIE:  state_str = "Z"; break;
    }

    /*
     * Linux proc(5) after ") ": state … tpgid, then flags..priority (10
     * fields), nice, num_threads, itrealvalue, starttime, vsize, rss, …
     * Misplacing starttime as field 21 made BusyBox FAST_TOP see nice≠0
     * and vsz=0 → STAT "RWN" instead of "R".
     */
    len = snprintf(buf, count,
                   "%d (%s) %s %d %d %d %d %d "     /*  1-8  */
                   "0 0 0 0 0 0 0 0 0 %d "          /*  9-18 (18=priority) */
                   "0 0 0 %llu 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n", /* 19-37 */
                   (int)proc->task.pid,
                   proc->comm[0] ? proc->comm : "none",
                   state_str,
                   (int)proc->ppid,
                   (int)proc->pgid,
                   (int)proc->sid,
                   tty_nr,
                   (int)proc->pgid,               /* tpgid: fg group on tty */
                   (int)proc->sched_prio,         /* 18: priority */
                   (unsigned long long)proc->start_ticks); /* 22: starttime */
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

	if (arch_hypervisor_present() &&
	    arch_hypervisor_vendor(hv_vendor, sizeof(hv_vendor)) == 0)
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
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    process_t *proc = NULL;
    
    if (pid == -1)
    {
        /* Current process */
        proc = current_process;
    } else
    {
        /* Specific process */
        proc = process_find_by_pid(pid);
    }
    
    if (!proc)
    {
        /* No such process */
        return -1;
    }
    
    /* Get command name from process */
    int len = snprintf(buf, count, "%s", proc->comm[0] ? proc->comm : "(none)");
    
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
        process_t *p;
        unsigned long irqf;

        pseudo_fs_nodes_register_all();
        n = 0;
        irqf = irq_save();
        for (p = process_list; p && n < max_entries; p = p->next)
        {
            char pid_str[16];
            int len;

            if (p->state == PROCESS_ZOMBIE)
                continue;
            len = snprintf(pid_str, sizeof(pid_str), "%d", (int)p->task.pid);
            if (len <= 0 || len >= (int)sizeof(pid_str))
                break;
            n = proc_readdir_add(entries, max_entries, n, pid_str, DT_DIR);
        }
        irq_restore(irqf);
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

    filename = proc_resolve_path(path, &pid);
    if (!filename)
        return -ENOENT;

    if (strcmp(filename, "pid_dir") == 0)
    {
        process_t *p = process_list;

        n = 0;
        while (p && n < max_entries)
        {
            char pid_str[16];
            int len;

            len = snprintf(pid_str, sizeof(pid_str), "%d", (int)p->task.pid);
            if (len <= 0 || len >= (int)sizeof(pid_str))
                break;
            n = proc_readdir_add(entries, max_entries, n, pid_str, DT_DIR);
            p = p->next;
        }
        return n;
    }

    if (strcmp(filename, "pid_subdir") == 0)
    {
        if (!process_find_by_pid(pid))
            return -ENOENT;
        n = 0;
        n = proc_readdir_add(entries, max_entries, n, "status", DT_REG);
        n = proc_readdir_add(entries, max_entries, n, "cmdline", DT_REG);
        n = proc_readdir_add(entries, max_entries, n, "stat", DT_REG);
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

    if (strcmp(filename, "pid_dir") == 0 || strcmp(filename, "pid_subdir") == 0)
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
        return 0;
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

    filename = proc_parse_path(path, &pid);
    if (!filename)
        return -ENOENT;
    
    /* Check if directory (OSDev-style /proc/pid) */
    if (strcmp(filename, "pid_dir") == 0 || strcmp(filename, "pid_subdir") == 0)
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        st->st_uid = 0;
        st->st_gid = 0;
        st->st_size = 0;
        return 0;
    }

    /* File not found */
    return -ENOENT;
}
