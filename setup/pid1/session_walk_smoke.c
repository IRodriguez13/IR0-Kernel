/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: session_walk_smoke.c
 * Description: PID1 — recursive walk of a real 9p host tree, the workload
 *              that double-faulted an interactive session.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * An interactive session ran `find -name home` from / and the kernel took a
 * double fault with RSP inside a kernel stack guard page. The interactive
 * targets export the whole IR0 tree over virtio-9p at /heart/dennis/src, but
 * every 9p smoke so far shared a freshly created empty directory, so no test
 * ever walked a deep tree over that transport.
 *
 * This walks a real host tree the way find does: opendir, stat every entry,
 * descend without a depth limit. It then reads back the kernel's own stack
 * headroom watermark, because "it did not crash this run" is not a result.
 */

#define WALK_MAX_ENTRIES 40000
#define WALK_PATH_MAX 1024

/* Fail if the kernel ever came within this much of a guard page. */
#define KSTACK_FLOOR_BYTES 4096

/*
 * Ceiling on true peak usage of this task's 32 KiB kernel stack. The chain
 * that faulted spent 20 KiB in one getdents; anything approaching that is a
 * regression even if it happens not to crash.
 */
#define KSTACK_PEAK_CEILING 12288

static unsigned long g_dirs;
static unsigned long g_files;
static unsigned long g_visited;
static unsigned g_max_depth;

static int fail(const char *t, int err)
{
	printf("%s errno=%d\n", t, err);
	fflush(stdout);
	return 1;
}

static long meminfo_field(const char *key)
{
	char buf[2048];
	int fd;
	ssize_t n;
	char *p;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	p = strstr(buf, key);
	if (!p)
		return -1;
	p += strlen(key);
	while (*p == ' ' || *p == '\t')
		p++;
	return strtol(p, NULL, 10);
}

static void walk(char *path, size_t len, unsigned depth)
{
	DIR *d;
	struct dirent *de;

	if (g_visited >= WALK_MAX_ENTRIES)
		return;
	if (depth > g_max_depth)
		g_max_depth = depth;

	d = opendir(path);
	if (!d)
		return;

	while ((de = readdir(d)) != NULL)
	{
		size_t nlen;
		struct stat st;

		if (de->d_name[0] == '.' &&
		    (de->d_name[1] == '\0' ||
		     (de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;
		if (g_visited >= WALK_MAX_ENTRIES)
			break;

		nlen = strlen(de->d_name);
		if (len + nlen + 2 >= WALK_PATH_MAX)
			continue;

		path[len] = '/';
		memcpy(path + len + 1, de->d_name, nlen + 1);
		g_visited++;

		/* find stats every entry; so do we, including symlinks. */
		if (lstat(path, &st) != 0)
		{
			path[len] = '\0';
			continue;
		}

		if (S_ISDIR(st.st_mode))
		{
			g_dirs++;
			walk(path, len + 1 + nlen, depth + 1);
		}
		else
		{
			g_files++;
		}

		path[len] = '\0';
	}

	closedir(d);
}

/*
 * Round-trip directory names through create, readdir and stat.
 *
 * MINIX stores names in a 14-byte field with no terminator when the name
 * fills it, so `du` reported "/usr/share/ash-completion<garbage>: Invalid
 * argument": readdir handed back bytes from the following entry, and a
 * lookup of any 14-character name compared past the field. Bracket the
 * boundary at 13, 14 and 15 characters.
 */
static int test_dirent_names(void)
{
	static const char *const names[] = {
		"abcdefghijklm",  /* 13 */
		"ash-completion", /* 14: exactly the on-disk field */
		"abcdefghijklmno" /* 15: longer than the field */
	};
	char path[128];
	unsigned k;

	mkdir("/tmp/nm", 0777);

	for (k = 0; k < sizeof(names) / sizeof(names[0]); k++)
	{
		int fd;
		int too_long = strlen(names[k]) > 14;

		snprintf(path, sizeof(path), "/tmp/nm/%s", names[k]);
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (too_long)
		{
			/*
			 * The name does not fit the on-disk field. It must be
			 * refused with ENAMETOOLONG and nothing must be left
			 * behind under a truncated name.
			 */
			if (fd >= 0)
			{
				close(fd);
				printf("SESSION_WALK_NAME_LONG_ACCEPTED name=%s\n",
				       names[k]);
				return -1;
			}
			if (errno != ENAMETOOLONG)
			{
				printf("SESSION_WALK_NAME_LONG_ERRNO name=%s errno=%d\n",
				       names[k], errno);
				return -1;
			}
			continue;
		}

		if (fd < 0)
		{
			printf("SESSION_WALK_NAME_CREATE_FAIL name=%s errno=%d\n",
			       names[k], errno);
			return -1;
		}
		close(fd);
	}

	for (k = 0; k < sizeof(names) / sizeof(names[0]); k++)
	{
		DIR *d;
		struct dirent *de;
		int seen = 0;
		size_t want = strlen(names[k]);

		if (want > 14)
			continue; /* refused above; must not exist */

		d = opendir("/tmp/nm");
		if (!d)
			return -1;
		while ((de = readdir(d)) != NULL)
		{
			struct stat st;

			if (strncmp(de->d_name, names[k], want) != 0)
				continue;
			seen = 1;

			if (strlen(de->d_name) != want)
			{
				printf("SESSION_WALK_NAME_GARBAGE name=%s got=%s len=%u\n",
				       names[k], de->d_name,
				       (unsigned)strlen(de->d_name));
				closedir(d);
				return -1;
			}

			/* du stats the name readdir handed it. */
			snprintf(path, sizeof(path), "/tmp/nm/%s", de->d_name);
			if (stat(path, &st) != 0)
			{
				printf("SESSION_WALK_NAME_STAT_FAIL path=%s errno=%d\n",
				       path, errno);
				closedir(d);
				return -1;
			}
			break;
		}
		closedir(d);

		if (!seen)
		{
			printf("SESSION_WALK_NAME_MISSING name=%s\n", names[k]);
			return -1;
		}
	}

	printf("SESSION_WALK_NAMES_OK\n");
	fflush(stdout);
	return 0;
}

/*
 * The numbers a session reads from different tools must agree: free(1) goes
 * through sysinfo(2) while /proc/meminfo is rendered separately, and the two
 * used to be able to drift.
 */
static int test_mem_consistency(void)
{
	struct sysinfo si;
	long total_kb;
	long free_kb;
	long avail_kb;
	long si_total_kb;

	if (sysinfo(&si) != 0)
		return -1;

	total_kb = meminfo_field("MemTotal:");
	free_kb = meminfo_field("MemFree:");
	avail_kb = meminfo_field("MemAvailable:");
	si_total_kb = (long)((si.totalram * si.mem_unit) / 1024);

	if (total_kb <= 0 || si_total_kb != total_kb)
	{
		printf("SESSION_WALK_MEM_MISMATCH proc=%ld sysinfo=%ld\n",
		       total_kb, si_total_kb);
		return -1;
	}
	if (free_kb <= 0 || free_kb > total_kb)
	{
		printf("SESSION_WALK_MEM_FREE_BAD free=%ld total=%ld\n",
		       free_kb, total_kb);
		return -1;
	}
	/* free(1) prints this as its "available" column. */
	if (avail_kb <= 0)
	{
		printf("SESSION_WALK_MEM_AVAIL_BAD avail=%ld\n", avail_kb);
		return -1;
	}

	printf("SESSION_WALK_MEM_OK total_kb=%ld free_kb=%ld avail_kb=%ld\n",
	       total_kb, free_kb, avail_kb);
	fflush(stdout);
	return 0;
}


/*
 * BusyBox ships no lsblk applet, so the product provides /bin/lsblk on top of
 * /proc/blockdevices. Exercise it here rather than through the console: the
 * interactive path is the flaky one, and this only needs fork/exec.
 */
static int test_lsblk(void)
{
	int fds[2];
	pid_t pid;
	char out[1024];
	ssize_t total = 0;
	int status = 0;

	{
		struct stat lst;

		/* Clearer than the bare exit 127 execl would give us. */
		if (stat("/bin/lsblk", &lst) != 0)
		{
			printf("SESSION_WALK_LSBLK_MISSING errno=%d\n", errno);
			return -1;
		}
	}

	if (pipe(fds) != 0)
	{
		printf("SESSION_WALK_LSBLK_PIPE_FAIL errno=%d\n", errno);
		return -1;
	}

	pid = fork();
	if (pid < 0)
	{
		printf("SESSION_WALK_LSBLK_FORK_FAIL errno=%d\n", errno);
		return -1;
	}
	if (pid == 0)
	{
		close(fds[0]);
		dup2(fds[1], 1);
		close(fds[1]);
		execl("/bin/lsblk", "lsblk", (char *)NULL);
		_exit(127);
	}

	close(fds[1]);
	for (;;)
	{
		ssize_t n = read(fds[0], out + total,
				 sizeof(out) - 1 - (size_t)total);

		if (n <= 0)
			break;
		total += n;
		if ((size_t)total >= sizeof(out) - 1)
			break;
	}
	out[total] = '\0';
	close(fds[0]);
	waitpid(pid, &status, 0);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		printf("SESSION_WALK_LSBLK_EXIT status=%d\n", status);
		return -1;
	}
	if (!strstr(out, "NAME") || !strstr(out, "hda") ||
	    !strstr(out, "disk"))
	{
		printf("SESSION_WALK_LSBLK_OUTPUT_BAD\n%s\n", out);
		return -1;
	}

	printf("SESSION_WALK_LSBLK_OK\n%s", out);
	fflush(stdout);
	return 0;
}

int main(void)
{
	static char path[WALK_PATH_MAX];
	long kstack_free;
	long irq_nest;
	long kstack_peak;

	mkdir("/mnt", 0755);
	mkdir("/mnt/host", 0755);

	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
		return fail("SESSION_WALK_MOUNT_FAIL", errno);
	printf("SESSION_WALK_MOUNT_OK\n");

	/*
	 * df(1) and mount(1) read /proc/mounts (via the /etc/mtab symlink), so
	 * a mount that works but never shows up there is invisible to the
	 * session even though the files are reachable.
	 */
	{
		char mbuf[1024];
		int mfd = open("/proc/mounts", O_RDONLY);
		ssize_t mn = -1;

		if (mfd >= 0)
		{
			mn = read(mfd, mbuf, sizeof(mbuf) - 1);
			close(mfd);
		}
		if (mn <= 0)
		{
			printf("SESSION_WALK_MOUNTS_UNREADABLE\n");
			return fail("SESSION_WALK_MOUNTS_FAIL", errno);
		}
		mbuf[mn] = '\0';
		printf("SESSION_WALK_MOUNTS\n%s", mbuf);
		if (!strstr(mbuf, "/mnt/host"))
		{
			printf("SESSION_WALK_MOUNTS_MISSING_9P\n");
			return fail("SESSION_WALK_MOUNTS_FAIL", 0);
		}
		/* mount(1) should see the pseudo namespaces too, as on Linux. */
		if (!strstr(mbuf, " /proc ") || !strstr(mbuf, " /sys ") ||
		    !strstr(mbuf, " /dev ") || !strstr(mbuf, " /heart "))
		{
			printf("SESSION_WALK_MOUNTS_MISSING_PSEUDO\n");
			return fail("SESSION_WALK_MOUNTS_FAIL", 0);
		}
		{
			struct statfs psfs;

			/* Must not inherit the root mount's block counts. */
			if (statfs("/proc", &psfs) != 0)
			{
				printf("SESSION_WALK_STATFS_PROC_FAIL errno=%d\n", errno);
				return fail("SESSION_WALK_MOUNTS_FAIL", errno);
			}
			if (psfs.f_blocks != 0)
			{
				printf("SESSION_WALK_STATFS_PROC_BLOCKS blocks=%lu\n",
				       (unsigned long)psfs.f_blocks);
				return fail("SESSION_WALK_MOUNTS_FAIL", 0);
			}
		}
		/* df also needs statfs(2) per mount point to print a row. */
		{
			struct statfs sfs;

			if (statfs("/mnt/host", &sfs) != 0)
			{
				printf("SESSION_WALK_STATFS_FAIL errno=%d\n", errno);
				return fail("SESSION_WALK_MOUNTS_FAIL", errno);
			}
			printf("SESSION_WALK_STATFS bsize=%ld blocks=%lu\n",
			       (long)sfs.f_bsize, (unsigned long)sfs.f_blocks);
			/* Tstatfs must reach the host; zero means df shows no size. */
			if (sfs.f_blocks == 0)
			{
				printf("SESSION_WALK_STATFS_9P_EMPTY\n");
				return fail("SESSION_WALK_MOUNTS_FAIL", 0);
			}
		}

		printf("SESSION_WALK_MOUNTS_OK\n");
		fflush(stdout);
	}
	fflush(stdout);

	strcpy(path, "/mnt/host");
	walk(path, strlen(path), 1);

	printf("SESSION_WALK_STATS dirs=%lu files=%lu visited=%lu maxdepth=%u\n",
	       g_dirs, g_files, g_visited, g_max_depth);
	fflush(stdout);

	if (g_visited < 100)
		return fail("SESSION_WALK_TOO_SHALLOW", (int)g_visited);

	/* Walk the kernel's own trees too: /proc and /heart are what the
	 * session listed before it faulted. */
	strcpy(path, "/proc");
	walk(path, strlen(path), 1);
	strcpy(path, "/heart");
	walk(path, strlen(path), 1);

	/*
	 * Write back to the share and read the pseudo-FS nodes the faulting
	 * session touched. sys_write plus v9p_write, and the bluetooth /proc
	 * readers, are the deepest remaining frames; measure them instead of
	 * assuming they fit.
	 */
	{
		static const char *const nodes[] = {
			"/proc/meminfo", "/proc/bluetooth/devices",
			"/proc/interrupts", "/proc/stat", "/proc/self/maps"
		};
		static char blob[8192];
		unsigned k;
		int fd;

		memset(blob, 'x', sizeof(blob));
		fd = open("/mnt/host/.ir0_session_walk.tmp",
			  O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0)
		{
			(void)write(fd, blob, sizeof(blob));
			close(fd);
			unlink("/mnt/host/.ir0_session_walk.tmp");
		}

		for (k = 0; k < sizeof(nodes) / sizeof(nodes[0]); k++)
		{
			fd = open(nodes[k], O_RDONLY);
			if (fd < 0)
				continue;
			while (read(fd, blob, sizeof(blob)) > 0)
				;
			close(fd);
		}
		printf("SESSION_WALK_IO_OK\n");
		fflush(stdout);
	}

	if (test_dirent_names() != 0)
		return fail("SESSION_WALK_NAMES_FAIL", errno);
	if (test_mem_consistency() != 0)
		return fail("SESSION_WALK_MEM_FAIL", errno);

	kstack_free = meminfo_field("KStackMinFree:");
	irq_nest = meminfo_field("IrqNestMax:");
	kstack_peak = meminfo_field("KStackPeak:");
	printf("SESSION_WALK_KSTACK free=%ld irq_nest_max=%ld peak=%ld\n",
	       kstack_free, irq_nest, kstack_peak);
	fflush(stdout);

	if (kstack_free >= 0 && kstack_free < KSTACK_FLOOR_BYTES)
		return fail("SESSION_WALK_KSTACK_LOW", (int)kstack_free);
	if (kstack_peak > KSTACK_PEAK_CEILING)
		return fail("SESSION_WALK_KSTACK_PEAK_HIGH", (int)kstack_peak);

	if (test_lsblk() != 0)
		return fail("SESSION_WALK_LSBLK_FAIL", 0);

	printf("SESSION_WALK_OK\n");
	fflush(stdout);
	for (;;)
		pause();
	return 0;
}
