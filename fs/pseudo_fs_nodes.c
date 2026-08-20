/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pseudo_fs_nodes.c
 * Description: Registered /proc and /sys endpoints using pseudo_fs_ops_t.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/pseudo_fs.h>
#include <ir0/heartfs.h>
#include <ir0/sysfs.h>
#include <ir0/procfs.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <string.h>

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

#define PSEUDO_SYS_MAX_CPUS 16

extern int proc_mounts_read(char *buf, size_t count);

static int g_pseudo_fs_nodes_ready;
static int g_proc_dynamic_registered;
static uintptr_t g_sys_cpu_ctx[PSEUDO_SYS_MAX_CPUS];
static uintptr_t g_sys_cpu_online_ctx[PSEUDO_SYS_MAX_CPUS];

static int64_t pseudo_read_wrap_int(void *ctx, char *buf, size_t count, off_t *offset)
{
    int (*fn)(char *, size_t);

    (void)offset;
    fn = (int (*)(char *, size_t))ctx;
    if (!fn)
        return -EINVAL;
    return fn(buf, count);
}

static int64_t pseudo_max_processes_write(void *ctx, const char *buf, size_t count)
{
    int r;

    (void)ctx;
    r = sys_kernel_max_processes_write_reg(buf, count);
    if (r < 0)
        return r;
    return (int64_t)count;
}

static int64_t pseudo_sys_cpu_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    unsigned cpu = (unsigned)(uintptr_t)ctx;

    (void)offset;
    return sys_devices_cpu_read_reg(buf, count, cpu);
}

static int64_t pseudo_sys_cpu_online_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    unsigned cpu = (unsigned)(uintptr_t)ctx;

    (void)offset;
    return sys_devices_cpu_online_read_reg(buf, count, cpu);
}

static int64_t pseudo_sys_cpu_online_write(void *ctx, const char *buf, size_t count)
{
    unsigned cpu = (unsigned)(uintptr_t)ctx;
    int r;

    r = sys_devices_cpu_online_write_reg(cpu, buf, count);
    if (r < 0)
        return r;
    return (int64_t)count;
}

#if CONFIG_ENABLE_BLUETOOTH
static int64_t pseudo_write_cstr(void *ctx, const char *buf, size_t count)
{
    int (*fn)(const char *);

    fn = (int (*)(const char *))ctx;
    if (!fn || !buf)
        return -EINVAL;

    {
        char cmd_buf[64];
        size_t copy_len = (count < sizeof(cmd_buf) - 1) ? count : (sizeof(cmd_buf) - 1);

        memcpy(cmd_buf, buf, copy_len);
        cmd_buf[copy_len] = '\0';
        while (copy_len > 0 && (cmd_buf[copy_len - 1] == '\n' ||
                                cmd_buf[copy_len - 1] == '\r' ||
                                cmd_buf[copy_len - 1] == ' '))
        {
            copy_len--;
            cmd_buf[copy_len] = '\0';
        }

        {
            int result = fn(cmd_buf);
            if (result < 0)
                return result;
        }
    }

    return (int64_t)count;
}

static int64_t pseudo_bt_scan_open(void *ctx, int flags)
{
    (void)ctx;
    (void)flags;
    return ir0_bt_hci_open();
}
#endif

static int pseudo_default_stat(void *ctx, stat_t *st)
{
    (void)ctx;
    if (!st)
        return -EINVAL;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = 0;
    return 0;
}

static int pseudo_writable_stat(void *ctx, stat_t *st)
{
    (void)ctx;
    if (!st)
        return -EINVAL;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0664;
    st->st_nlink = 1;
    st->st_size = 4096;
    return 0;
}

static const pseudo_fs_ops_t proc_mounts_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t proc_static_read_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_static_read_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_max_processes_ops = {
    .read = pseudo_read_wrap_int,
    .write = pseudo_max_processes_write,
    .stat = pseudo_writable_stat,
};

static const pseudo_fs_ops_t sys_cpu_read_ops = {
    .read = pseudo_sys_cpu_read,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_cpu_online_ops = {
    .read = pseudo_sys_cpu_online_read,
    .write = pseudo_sys_cpu_online_write,
    .stat = pseudo_writable_stat,
};

#if CONFIG_ENABLE_NETWORKING
typedef struct sys_net_iface_ctx
{
	char ifname[16];
	unsigned attr;
	int in_use;
} sys_net_iface_ctx_t;

#define SYS_NET_IFACE_CTX_MAX 32

static sys_net_iface_ctx_t g_sys_net_iface_ctx[SYS_NET_IFACE_CTX_MAX];
static int g_sys_net_dynamic_registered;

static sys_net_iface_ctx_t *sys_net_iface_ctx_alloc(void)
{
	int i;

	for (i = 0; i < SYS_NET_IFACE_CTX_MAX; i++)
	{
		if (!g_sys_net_iface_ctx[i].in_use)
			return &g_sys_net_iface_ctx[i];
	}
	return NULL;
}

static int sys_net_iface_match(const char *path, void **out_ctx)
{
	const char *rest;
	const char *slash;
	char ifname[16];
	size_t nlen;
	int attr;
	sys_net_iface_ctx_t *ctx;

	if (!path || !out_ctx)
		return -EINVAL;
	if (strncmp(path, "/sys/class/net/", 15) != 0)
		return -ENOENT;
	rest = path + 15;
	if (!rest[0])
		return -ENOENT;
	slash = strchr(rest, '/');
	if (!slash)
	{
		/* /sys/class/net/<iface> — directory node (Linux sysfs). */
		nlen = strlen(rest);
		attr = SYS_NET_ATTR_DIR;
	}
	else if (!slash[1])
	{
		return -ENOENT;
	}
	else
	{
		nlen = (size_t)(slash - rest);
		attr = sys_class_net_parse_attr(slash + 1);
		if (attr < 0)
			return -ENOENT;
	}
	if (nlen == 0 || nlen >= sizeof(ifname))
		return -ENOENT;
	memcpy(ifname, rest, nlen);
	ifname[nlen] = '\0';
	if (sys_class_net_find_name(ifname) != 0)
		return -ENOENT;
	ctx = sys_net_iface_ctx_alloc();
	if (!ctx)
		return -ENFILE;
	memset(ctx, 0, sizeof(*ctx));
	memcpy(ctx->ifname, ifname, nlen + 1);
	ctx->attr = (unsigned)attr;
	ctx->in_use = 1;
	*out_ctx = ctx;
	return 0;
}

static int sys_net_iface_stat(void *ctx, stat_t *st)
{
	sys_net_iface_ctx_t *file = ctx;

	if (!file || !st)
		return -EINVAL;
	memset(st, 0, sizeof(*st));
	if (file->attr == (unsigned)SYS_NET_ATTR_DIR)
	{
		st->st_mode = S_IFDIR | 0555;
		st->st_nlink = 2;
	}
	else
	{
		st->st_mode = S_IFREG | 0444;
		st->st_nlink = 1;
		st->st_size = 4096;
	}
	return 0;
}

static int64_t sys_net_iface_read(void *ctx, char *buf, size_t count, off_t *offset)
{
	sys_net_iface_ctx_t *file = ctx;

	(void)offset;
	if (!file || !buf)
		return -EINVAL;
	if (file->attr == (unsigned)SYS_NET_ATTR_DIR)
		return -EISDIR;
	return sys_class_net_attr_read_named(buf, count, file->ifname, file->attr);
}

static int64_t sys_net_iface_close(void *ctx)
{
	sys_net_iface_ctx_t *file = ctx;

	if (file)
		file->in_use = 0;
	return 0;
}

static const pseudo_fs_ops_t sys_net_iface_ops = {
	.read = sys_net_iface_read,
	.close = sys_net_iface_close,
	.stat = sys_net_iface_stat,
};

static void pseudo_fs_register_sys_net_dynamic(void)
{
	if (g_sys_net_dynamic_registered)
		return;
	if (pseudo_fs_register_dynamic("/sys", sys_net_iface_match,
				       &sys_net_iface_ops) == 0)
		g_sys_net_dynamic_registered = 1;
}
#endif

#if CONFIG_ENABLE_BLUETOOTH
static const pseudo_fs_ops_t proc_bt_devices_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t proc_bt_scan_ops = {
    .read = pseudo_read_wrap_int,
    .write = pseudo_write_cstr,
    .open = pseudo_bt_scan_open,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_address_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_state_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_neighbors_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_sessions_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};
#endif

static int64_t pseudo_hostname_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    (void)ctx;
    (void)offset;
    return sys_kernel_hostname_read_reg(buf, count);
}

static int64_t pseudo_hostname_write(void *ctx, const char *buf, size_t count)
{
    (void)ctx;
    return sys_kernel_hostname_write_reg(buf, count);
}

static const pseudo_fs_ops_t sys_hostname_ops = {
    .read = pseudo_hostname_read,
    .write = pseudo_hostname_write,
    .stat = pseudo_writable_stat,
};

typedef struct proc_pid_file_ctx
{
    pid_t pid;
    int kind;
    int in_use;
} proc_pid_file_ctx_t;

#define PROC_PID_FILE_STATUS  0
#define PROC_PID_FILE_CMDLINE 1
#define PROC_PID_FILE_STAT    2
#define PROC_PID_CTX_MAX      32

static proc_pid_file_ctx_t g_proc_pid_ctx[PROC_PID_CTX_MAX];

static proc_pid_file_ctx_t *proc_pid_file_ctx_alloc(void)
{
    int i;

    for (i = 0; i < PROC_PID_CTX_MAX; i++)
    {
        if (!g_proc_pid_ctx[i].in_use)
            return &g_proc_pid_ctx[i];
    }

    return NULL;
}

static int proc_pid_file_match(const char *path, void **out_ctx)
{
    pid_t pid;
    const char *name;
    proc_pid_file_ctx_t *ctx;

    if (!out_ctx)
        return -EINVAL;

    name = proc_resolve_path(path, &pid);
    if (!name)
        return -ENOENT;

    if (strcmp(name, "status") != 0 && strcmp(name, "cmdline") != 0 &&
        !(strcmp(name, "stat") == 0 && pid > 0))
        return -ENOENT;

    ctx = proc_pid_file_ctx_alloc();
    if (!ctx)
        return -ENFILE;

    ctx->pid = pid;
    if (strcmp(name, "cmdline") == 0)
        ctx->kind = PROC_PID_FILE_CMDLINE;
    else if (strcmp(name, "stat") == 0)
        ctx->kind = PROC_PID_FILE_STAT;
    else
        ctx->kind = PROC_PID_FILE_STATUS;
    ctx->in_use = 1;
    *out_ctx = ctx;
    return 0;
}

static int64_t proc_pid_file_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    proc_pid_file_ctx_t *file = ctx;

    (void)offset;
    if (!file || !buf)
        return -EINVAL;

    if (file->kind == PROC_PID_FILE_CMDLINE)
        return proc_cmdline_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_STAT)
        return proc_pid_stat_read(buf, count, file->pid);

    return proc_status_read(buf, count, file->pid);
}

static int64_t proc_pid_file_close(void *ctx)
{
    proc_pid_file_ctx_t *file = ctx;

    if (file)
        file->in_use = 0;
    return 0;
}

static int proc_pid_file_stat(void *ctx, stat_t *st)
{
    (void)ctx;
    if (!st)
        return -EINVAL;

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    /* Linux reports 0 for /proc/pid files; readers must not trust st_size. */
    st->st_size = 0;
    return 0;
}

static const pseudo_fs_ops_t proc_pid_file_ops = {
    .read = proc_pid_file_read,
    .close = proc_pid_file_close,
    .stat = proc_pid_file_stat,
};

static void pseudo_fs_register_proc_dynamic(void)
{
    int rc;

    if (g_proc_dynamic_registered)
        return;

    rc = pseudo_fs_register_dynamic("/proc", proc_pid_file_match, &proc_pid_file_ops);
    if (rc == 0)
        g_proc_dynamic_registered = 1;
}

static void pseudo_fs_register_sys_cpus(void)
{
    uint32_t cpus = get_cpu_count();
    char rel[96];

    if (cpus == 0)
        cpus = 1;
    if (cpus > PSEUDO_SYS_MAX_CPUS)
        cpus = PSEUDO_SYS_MAX_CPUS;

    for (uint32_t i = 0; i < cpus; i++)
    {
        g_sys_cpu_ctx[i] = (uintptr_t)i;
        g_sys_cpu_online_ctx[i] = (uintptr_t)i;

        snprintf(rel, sizeof(rel), "devices/system/cpu%u", (unsigned)i);
        pseudo_fs_register("/sys", rel, &sys_cpu_read_ops, (void *)g_sys_cpu_ctx[i]);

        snprintf(rel, sizeof(rel), "devices/system/cpu%u/online", (unsigned)i);
        pseudo_fs_register("/sys", rel, &sys_cpu_online_ops, (void *)g_sys_cpu_online_ctx[i]);
    }
}

void pseudo_fs_nodes_register_all(void)
{
    if (g_pseudo_fs_nodes_ready)
        return;

    pseudo_fs_register_proc_dynamic();

    pseudo_fs_register("/proc", "mounts", &proc_mounts_ops,
                       (void *)(uintptr_t)proc_mounts_read);
    pseudo_fs_register("/proc", "cpuinfo", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_cpuinfo_read);
    pseudo_fs_register("/proc", "blockdevices", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_blockdevices_read);
    pseudo_fs_register("/proc", "netinfo", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_netinfo_read);
    pseudo_fs_register("/proc", "version", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_version_read);
    pseudo_fs_register("/proc", "cmdline", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_boot_cmdline_read);
    pseudo_fs_register("/proc", "uptime", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_uptime_read);
    pseudo_fs_register("/proc", "stat", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_stat_read);
    pseudo_fs_register("/proc", "meminfo", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_meminfo_read);
    pseudo_fs_register("/proc", "ps", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_ps_read);
    pseudo_fs_register("/proc", "drivers", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_drivers_read);
    pseudo_fs_register("/proc", "loadavg", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_loadavg_read);
    pseudo_fs_register("/proc", "filesystems", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_filesystems_read);
    pseudo_fs_register("/proc", "partitions", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_partitions_read);
    pseudo_fs_register("/proc", "interrupts", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_interrupts_read);
    pseudo_fs_register("/proc", "iomem", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_iomem_read);
    pseudo_fs_register("/proc", "ioports", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_ioports_read);
    pseudo_fs_register("/proc", "modules", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_modules_read);
    pseudo_fs_register("/proc", "timer_list", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_timer_list_read);
    pseudo_fs_register("/proc", "kmsg", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_kmsg_read);
    pseudo_fs_register("/proc", "swaps", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_swaps_read);
    pseudo_fs_register("/proc", "net/dev", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_dev_read);
    pseudo_fs_register("/proc", "net/route", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_route_read);
    pseudo_fs_register("/proc", "net/tcp", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_tcp_read);
    pseudo_fs_register("/proc", "net/udp", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_udp_read);
    pseudo_fs_register("/proc", "net/raw", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_raw_read);
    pseudo_fs_register("/proc", "net/unix", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_unix_read);

#if CONFIG_ENABLE_BLUETOOTH
    pseudo_fs_register("/proc", "bluetooth/devices", &proc_bt_devices_ops,
                       (void *)(uintptr_t)ir0_bt_proc_devices_read);
    pseudo_fs_register("/proc", "bluetooth/scan", &proc_bt_scan_ops,
                       (void *)(uintptr_t)ir0_bt_proc_scan_read);
    pseudo_fs_register("/sys", "class/bluetooth/hci0/address", &sys_bt_address_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_hci0_address_read);
    pseudo_fs_register("/sys", "class/bluetooth/hci0/state", &sys_bt_state_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_hci0_state_read);
    pseudo_fs_register("/sys", "class/bluetooth/topology/neighbors", &sys_bt_neighbors_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_topology_neighbors_read);
    pseudo_fs_register("/sys", "class/bluetooth/sessions", &sys_bt_sessions_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_sessions_read);
#endif

    pseudo_fs_register("/sys", "kernel/hostname", &sys_hostname_ops, NULL);
    pseudo_fs_register("/sys", "kernel/version", &sys_static_read_ops,
                       (void *)(uintptr_t)sys_kernel_version_read_reg);
    pseudo_fs_register("/sys", "kernel/osrelease", &sys_static_read_ops,
                       (void *)(uintptr_t)sys_kernel_osrelease_read_reg);
    pseudo_fs_register("/sys", "kernel/build", &sys_static_read_ops,
		       (void *)(uintptr_t)sys_kernel_build_read_reg);
    pseudo_fs_register("/sys", "kernel/features", &sys_static_read_ops,
		       (void *)(uintptr_t)sys_kernel_features_read_reg);
    pseudo_fs_register("/sys", "kernel/max_processes", &sys_max_processes_ops,
                       (void *)(uintptr_t)sys_kernel_max_processes_read_reg);
    pseudo_fs_register("/sys", "devices/system", &sys_static_read_ops,
                       (void *)(uintptr_t)sys_devices_system_read_reg);
    pseudo_fs_register("/sys", "devices/block", &sys_static_read_ops,
                       (void *)(uintptr_t)sys_devices_block_read_reg);
    pseudo_fs_register("/sys", "console/mode", &sys_static_read_ops,
                       (void *)(uintptr_t)sys_console_mode_read_reg);
    /* NIC list + /sys/class/net/<iface>/attrs via dynamic matcher. */
    pseudo_fs_register("/sys", "class/net", &sys_static_read_ops,
		       (void *)(uintptr_t)sys_class_net_list_read_reg);
#if CONFIG_ENABLE_NETWORKING
    pseudo_fs_register_sys_net_dynamic();
#endif

    pseudo_fs_register_sys_cpus();

    heart_nodes_register();

    g_pseudo_fs_nodes_ready = 1;
}
