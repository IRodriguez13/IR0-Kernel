/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_procfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de procfs
 */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include "process.h"
#include <kernel/syscalls/fs_syscalls.h>
#include <ir0/procfs.h>
#include <ir0/stat.h>
#include <string.h>

static int ktest_pid_to_str(int pid, char *buf, size_t buflen)
{
	char rev[16];
	int n = 0;
	int pos = 0;

	if (!buf || buflen == 0)
		return -1;

	if (pid == 0)
	{
		if (buflen < 2)
			return -1;
		buf[0] = '0';
		buf[1] = '\0';
		return 1;
	}

	while (pid > 0 && n < (int)sizeof(rev))
	{
		rev[n++] = (char)('0' + (pid % 10));
		pid /= 10;
	}

	if ((size_t)n + 1 > buflen)
		return -1;

	while (n > 0)
		buf[pos++] = rev[--n];
	buf[pos] = '\0';
	return pos;
}

void ktest_procfs_uptime(void)
{
	KTEST_BEGIN("procfs_uptime");
	int64_t fd = sys_open("/proc/uptime", 0, 0);
	KASSERT_GT(fd, 0);
	char buf[128];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');
	KTEST_END();
}

void ktest_procfs_pid_status(void)
{
	pid_t pid = -1;
	const char *name;
	int64_t fd;
	char buf[256];
	int64_t n;

	KTEST_BEGIN("procfs_pid_status");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/status", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "status") == 0);

	fd = sys_open("/proc/status", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);

	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(strstr(buf, "\t") != NULL);

	KTEST_END();
}

void ktest_procfs_pid_maps(void)
{
	int64_t fd;
	char buf[512];
	int64_t n;

	KTEST_BEGIN("procfs_pid_maps");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	/* /proc/self/maps: readable; when the task has an mm, each line is a
	 * "<hex>-<hex> <perms>" range. A pure kernel thread may have no regions. */
	fd = sys_open("/proc/self/maps", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	if (n > 0)
		KASSERT(strstr(buf, "-") != NULL);

	/* /proc/self/statm: 7 space-separated page counts; first is a digit. */
	fd = sys_open("/proc/self/statm", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');
	KASSERT(strstr(buf, " ") != NULL);

	KTEST_END();
}

void ktest_procfs_pid_fd(void)
{
	const char *path = "/proc/self/fd/0";
	char buf[256];
	char linkbuf[256];
	int64_t fd;
	int64_t n;
	int64_t link_len;

	KTEST_BEGIN("procfs_pid_fd");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	fd = sys_open(path, 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);

	memset(linkbuf, 0, sizeof(linkbuf));
	link_len = sys_readlink(path, linkbuf, sizeof(linkbuf) - 1);
	KASSERT_GT(link_len, 0);
	KASSERT(strcmp(buf, linkbuf) == 0);

	KTEST_END();
}

void ktest_procfs_pid_environ(void)
{
	int64_t fd;
	char buf[512];
	int64_t n;

	KTEST_BEGIN("procfs_pid_environ");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	fd = sys_open("/proc/self/environ", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	if (current_process->saved_environ_len > 0)
		KASSERT_GT(n, 0);

	KTEST_END();
}

void ktest_procfs_self_symlink(void)
{
	pid_t pid = -1;
	const char *name;
	char linkbuf[64];
	char expect[32];
	stat_t st;
	int plen;
	int expect_len;

	KTEST_BEGIN("procfs_self_symlink");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/self", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "self_link") == 0);
	KASSERT_EQ(pid, current_process->task.pid);

	expect_len = ktest_pid_to_str((int)current_process->task.pid,
				      expect, sizeof(expect));
	KASSERT_GT(expect_len, 0);

	memset(linkbuf, 0, sizeof(linkbuf));
	plen = proc_readlink("/proc/self", linkbuf, sizeof(linkbuf));
	KASSERT_EQ(plen, expect_len);
	KASSERT(strcmp(linkbuf, expect) == 0);

	{
		int64_t link_len;

		memset(linkbuf, 0, sizeof(linkbuf));
		link_len = sys_readlink("/proc/self", linkbuf, sizeof(linkbuf));
		KASSERT_EQ(link_len, expect_len);
		KASSERT(strcmp(linkbuf, expect) == 0);
	}

	memset(&st, 0, sizeof(st));
	KASSERT_EQ(proc_stat("/proc/self", &st), 0);
	KASSERT(S_ISLNK(st.st_mode));

	KTEST_END();
}

void ktest_procfs_pid_exe(void)
{
	pid_t pid = -1;
	const char *name;
	char linkbuf[512];
	stat_t st;
	int plen;

	KTEST_BEGIN("procfs_pid_exe");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/self/exe", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "exe_link") == 0);
	KASSERT_EQ(pid, current_process->task.pid);

	memset(&st, 0, sizeof(st));
	KASSERT_EQ(proc_stat("/proc/self/exe", &st), 0);
	KASSERT(S_ISLNK(st.st_mode));

	memset(linkbuf, 0, sizeof(linkbuf));
	plen = proc_readlink("/proc/self/exe", linkbuf, sizeof(linkbuf));
	if (current_process->exe_path[0])
	{
		KASSERT_GT(plen, 0);
		KASSERT(strcmp(linkbuf, current_process->exe_path) == 0);
	}
	else
		KASSERT(plen < 0);

	{
		int64_t link_len;

		memset(linkbuf, 0, sizeof(linkbuf));
		link_len = sys_readlink("/proc/self/exe", linkbuf, sizeof(linkbuf));
		if (current_process->exe_path[0])
		{
			KASSERT_GT(link_len, 0);
			KASSERT(strcmp(linkbuf, current_process->exe_path) == 0);
		}
		else
			KASSERT(link_len < 0);
	}

	KTEST_END();
}
