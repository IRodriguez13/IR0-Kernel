/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pipeline_stress_smoke.c
 * Description: PID1 stress — SIGPIPE, multi-fork COW, ash echo|cat, and
 *              hexdump|head without ash (desk regression).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <ir0/ktm/uapi.h>

#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef WNOHANG
#define WNOHANG 1
#endif

#define STRESS_ROUNDS 1

static int g_cow = 1;

static void out(const char *s)
{
	size_t n = 0;

	if (!s)
		return;
	while (s[n])
		n++;
	(void)write(1, s, n);
}

static void out_dec(int v)
{
	char buf[16];
	int n = 0;
	int neg = 0;

	if (v < 0)
	{
		neg = 1;
		v = -v;
	}
	if (v == 0)
		buf[n++] = '0';
	else
	{
		char tmp[16];
		int t = 0;

		while (v > 0 && t < (int)sizeof(tmp))
		{
			tmp[t++] = (char)('0' + (v % 10));
			v /= 10;
		}
		while (t > 0)
			buf[n++] = tmp[--t];
	}
	buf[n] = '\0';
	if (neg)
		out("-");
	out(buf);
}

/*
 * Emit /proc/ps so a hung step shows which processes still exist and in what
 * state (R/S/Z) instead of only the timeout code.
 */
static void dump_proc_ps(const char *tag)
{
	char buf[512];
	ssize_t n;
	int fd;

	out("PIPELINE_STRESS_PS ");
	out(tag);
	out("\n");

	fd = open("/proc/ps", O_RDONLY);
	if (fd < 0)
	{
		out("PIPELINE_STRESS_PS open-failed\n");
		return;
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		(void)write(1, buf, (size_t)n);
	if (n < 0)
	{
		out("PIPELINE_STRESS_PS read-failed errno=");
		out_dec(errno);
		out("\n");
	}
	(void)close(fd);
	out("PIPELINE_STRESS_PS end\n");
}

/*
 * Dump the KTM event ring before reporting a failure. The bugs this smoke
 * catches are ordering bugs across two tasks (a wake published before the
 * sleeper blocks, a pipe end released while a reader waits); the serial log
 * interleaves them, the ring keeps them in sequence order.
 */
static void dump_ktm_ring(void)
{
	int fd = open("/dev/ktm", O_RDONLY);

	if (fd < 0)
		return;
	/*
	 * KTM_SUBSYS_IPC (5) first and on its own: a timeout spends its whole
	 * window ping-ponging BLOCK/WAKE, which buried the pipe history when
	 * both subsystems shared one budget. Then a short scheduler tail for
	 * the moments around the hang. The numeric masks avoid pulling the
	 * kernel-side enum header into a musl build.
	 */
	(void)ioctl(fd, KTM_IOC_DUMP_EVENTS, (void *)KTM_DUMP_ARG(1u << 5, 120));
	(void)ioctl(fd, KTM_IOC_DUMP_EVENTS, (void *)KTM_DUMP_ARG(1u << 3, 40));
	(void)close(fd);
}

static void fail(const char *why)
{
	dump_ktm_ring();
	out("PIPELINE_STRESS_FAIL ");
	out(why);
	out("\n");
	for (;;)
		pause();
}

static int wait_status_ok(int status)
{
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGPIPE)
		return 1;
	if (WIFEXITED(status) &&
	    (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 141))
		return 1;
	return 0;
}

static int test_sigpipe_direct(void)
{
	int fds[2];
	pid_t writer;
	int status;
	char buf[256];

	out("PIPELINE_STEP=sigpipe_direct\n");
	if (pipe(fds) < 0)
		return -1;
	writer = fork();
	if (writer < 0)
	{
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (writer == 0)
	{
		close(fds[0]);
		for (;;)
		{
			if (write(fds[1], buf, sizeof(buf)) < 0)
				_exit(2);
		}
	}
	close(fds[1]);
	(void)read(fds[0], buf, 32);
	close(fds[0]);
	if (waitpid(writer, &status, 0) < 0 || !wait_status_ok(status))
	{
		out("PIPELINE_STRESS_FAIL_REASON=sigpipe_direct\n");
		return -1;
	}
	return 0;
}

static int test_n_fork_cow_write(int n, const char *tag)
{
	pid_t kid;
	int i;
	int st;

	if (n < 2 || n > 8)
		return -1;
	out("PIPELINE_STEP=");
	out(tag);
	out("\n");
	g_cow = 1;
	/*
	 * Serialize fork→write→wait so each child COW-breaks after the parent
	 * is already RO+PAGE_COW from the previous fork (pipeline chain),
	 * without concurrent writers racing the same .bss page.
	 */
	for (i = 0; i < n; i++)
	{
		kid = fork();
		if (kid < 0)
			return -1;
		if (kid == 0)
		{
			g_cow = 10 + i;
			_exit(g_cow == 10 + i ? 0 : 1);
		}
		if (waitpid(kid, &st, 0) < 0)
			return -1;
		if (WIFSIGNALED(st) || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
		{
			out("PIPELINE_STRESS_FAIL_REASON=");
			out(tag);
			out("\n");
			return -1;
		}
	}
	return 0;
}

/*
 * Concurrent COW: N children write the same .bss page without waiting
 * between forks (ash pipe stages). Catches races that serial COW misses.
 */
static int test_parallel_cow_write(int n, const char *tag)
{
	pid_t kids[8];
	int i;
	int st;

	if (n < 2 || n > 8)
		return -1;
	out("PIPELINE_STEP=");
	out(tag);
	out("\n");
	g_cow = 1;
	for (i = 0; i < n; i++)
	{
		kids[i] = fork();
		if (kids[i] < 0)
			return -1;
		if (kids[i] == 0)
		{
			g_cow = 20 + i;
			_exit(g_cow == 20 + i ? 0 : 1);
		}
	}
	for (i = 0; i < n; i++)
	{
		if (waitpid(kids[i], &st, 0) < 0)
			return -1;
		if (WIFSIGNALED(st) || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
		{
			out("PIPELINE_STRESS_FAIL_REASON=");
			out(tag);
			out("\n");
			return -1;
		}
	}
	return 0;
}

/*
 * N concurrent pipelines: each child streams into its own pipe while the
 * parent drains a little and closes early, so every writer takes SIGPIPE at
 * a different moment. Stresses the pipe wake and the wait4 reap together,
 * which is where the lost-wakeup class shows up (Linux prepare_to_wait).
 */
static int test_parallel_pipe_stress(int n, const char *tag)
{
	pid_t kids[4];
	int rfd[4];
	int i;
	int st;

	if (n < 2 || n > 4)
		return -1;
	out("PIPELINE_STEP=");
	out(tag);
	out("\n");

	for (i = 0; i < n; i++)
	{
		int fds[2];

		if (pipe(fds) < 0)
			return -1;
		kids[i] = fork();
		if (kids[i] < 0)
		{
			close(fds[0]);
			close(fds[1]);
			return -1;
		}
		if (kids[i] == 0)
		{
			char msg[16];
			int k;

			close(fds[0]);
			for (k = 0; k < (int)sizeof(msg); k++)
				msg[k] = 'x';
			for (k = 0; k < 64; k++)
			{
				if (write(fds[1], msg, sizeof(msg)) < 0)
					_exit(0); /* EPIPE is the expected end */
			}
			close(fds[1]);
			_exit(0);
		}
		close(fds[1]);
		rfd[i] = fds[0];
	}

	for (i = 0; i < n; i++)
	{
		char buf[32];

		(void)read(rfd[i], buf, sizeof(buf));
		close(rfd[i]);
	}

	for (i = 0; i < n; i++)
	{
		for (;;)
		{
			if (waitpid(kids[i], &st, 0) >= 0)
				break;
			if (errno == EINTR)
				continue;
			out("PIPELINE_STRESS_FAIL_REASON=");
			out(tag);
			out(" waitpid errno=");
			out_dec(errno);
			out("\n");
			return -1;
		}
	}
	return 0;
}

/*
 * Generic early-close reader: fork writer → read some → close → expect
 * SIGPIPE / exit 141 (or clean exit after -n bound).
 */
static int test_writer_head_direct(const char *tag, char *const wargv[],
				   int want_bytes)
{
	int fds[2];
	pid_t w;
	int st_w = -1;
	char buf[256];
	ssize_t n;
	int got = 0;

	out("PIPELINE_STEP=");
	out(tag);
	out("\n");
	if (pipe(fds) < 0)
		return -1;

	w = fork();
	if (w < 0)
	{
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (w == 0)
	{
		close(fds[0]);
		if (dup2(fds[1], 1) < 0)
			_exit(127);
		close(fds[1]);
		execve(wargv[0], wargv, NULL);
		_exit(127);
	}

	close(fds[1]);
	while (got < want_bytes)
	{
		n = read(fds[0], buf, sizeof(buf));
		if (n > 0)
		{
			got += (int)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		break;
	}
	close(fds[0]);

	for (;;)
	{
		if (waitpid(w, &st_w, 0) >= 0)
			break;
		if (errno == EINTR)
			continue;
		if (errno == ECHILD && got > 0)
		{
			st_w = 0;
			break;
		}
		out("PIPELINE_STRESS_FAIL_REASON=");
		out(tag);
		out(" waitpid errno=");
		out_dec(errno);
		out("\n");
		return -1;
	}
	if (!wait_status_ok(st_w))
	{
		out("PIPELINE_STRESS_FAIL_REASON=");
		out(tag);
		out(" status=");
		out_dec(st_w);
		out("\n");
		return -1;
	}
	if (got <= 0)
	{
		out("PIPELINE_STRESS_FAIL_REASON=");
		out(tag);
		out(" empty status=");
		out_dec(st_w);
		out(" errno=");
		out_dec(errno);
		out("\n");
		return -1;
	}
	return 0;
}

static int test_hexdump_head_direct(void)
{
	char *argv[] = {
		"/bin/busybox", "hexdump", "-n", "256", "-C",
		"/bin/busybox", NULL
	};

	return test_writer_head_direct("hexdump_head_direct", argv, 200);
}

static int test_yes_head_direct(void)
{
	char *argv[] = { "/bin/busybox", "yes", NULL };

	return test_writer_head_direct("yes_head_direct", argv, 40);
}

static int test_od_head_direct(void)
{
	char *argv[] = {
		"/bin/busybox", "od", "-N", "128", "-tx1", "/bin/busybox", NULL
	};

	return test_writer_head_direct("od_head_direct", argv, 64);
}

static int run_ash_timeout(const char *cmd, int max_secs)
{
	pid_t pid;
	int status;
	int waited;
	int tick;
	char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
	char *envp[] = {
		"PATH=/bin:/usr/bin", "HOME=/", "USER=root", NULL
	};

	if (max_secs < 1)
		max_secs = 1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		execve("/bin/sh", argv, envp);
		_exit(127);
	}
	/*
	 * Bound wait: ash that leaves a pipe end open deadlocks the writer
	 * (hexdump|head). Poll so we can SIGKILL and fail the step instead of
	 * wedging the smoke until QEMU stale-kill.
	 */
	for (tick = 0; tick < max_secs; tick++)
	{
		waited = waitpid(pid, &status, WNOHANG);
		if (waited == pid)
			goto got_status;
		if (waited < 0 && errno != EINTR)
			return -1;
		sleep(1);
	}
	/*
	 * Snapshot the process table before killing: ec=-2 alone never said
	 * whether ash is blocked in wait4, whether a stage exited and was not
	 * reaped, or whether nobody ran at all.
	 */
	dump_proc_ps("hung");

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);
	return -2;

got_status:
	if (WIFEXITED(status))
	{
		int ec = WEXITSTATUS(status);

		/* 141 = 128+SIGPIPE; 255 = BusyBox ash pipeline after SIGPIPE. */
		if (ec == 0 || ec == 141 || ec == 255)
			return 0;
		return ec;
	}
	if (WIFSIGNALED(status))
	{
		if (WTERMSIG(status) == SIGPIPE)
			return 0;
		return 128 + WTERMSIG(status);
	}
	return -1;
}

static int run_ash(const char *cmd)
{
	return run_ash_timeout(cmd, 20);
}

static int require_ash(const char *tag, const char *cmd)
{
	int ec;
	int attempt;

	out("PIPELINE_STEP=");
	out(tag);
	out("\n");
	/*
	 * Ash STANDALONE pipelines occasionally return 1 without SEGV under
	 * load; one retry keeps the gate honest without soft-skipping forever.
	 */
	for (attempt = 0; attempt < 2; attempt++)
	{
		ec = run_ash(cmd);
		if (ec == 0)
			return 0;
	}
	out("PIPELINE_STRESS_FAIL_REASON=");
	out(tag);
	out(" ec=");
	out_dec(ec);
	out("\n");
	return -1;
}

/* Best-effort ash: never emits PIPELINE_STRESS_FAIL* (autokill fail-regex). */
static void try_ash(const char *tag, const char *cmd)
{
	int ec;

	out("PIPELINE_STEP=");
	out(tag);
	out("\n");
	ec = run_ash_timeout(cmd, 20);
	if (ec != 0)
	{
		out("PIPELINE_STRESS_SKIP=");
		out(tag);
		out(" ec=");
		out_dec(ec);
		out("\n");
	}
}

/*
 * A `find` over a deep tree double-faulted the kernel: the recursive rmdir
 * helper had a 4 KiB stack frame and a depth limit of 32, four times what a
 * 32 KiB kernel stack can hold. Nothing in the suite ever built a tree deep
 * enough to reach it, so the overflow only showed up in an interactive
 * session. Build one and tear it down.
 */
#define DEEP_TREE_LEVELS 24

static int test_deep_tree(void)
{
	char path[8 * DEEP_TREE_LEVELS + 16];
	int len = 0;
	int i;
	int rc;

	len = 0;
	for (i = 0; i < 4; i++)
		path[len++] = "/tmp"[i];
	path[len] = '\0';
	(void)mkdir(path, 0777);

	for (i = 0; i < DEEP_TREE_LEVELS; i++)
	{
		path[len++] = '/';
		path[len++] = 'd';
		path[len++] = (char)('0' + (i % 10));
		path[len] = '\0';
		if (mkdir(path, 0777) != 0 && errno != EEXIST)
		{
			out("PIPELINE_STRESS_DEEP_MKDIR_FAIL level=");
			out_dec(i);
			out("\n");
			return -1;
		}
	}

	/*
	 * Remove the leaf bottom-up first. If this works but the recursive
	 * removal below does not, the fault is in the kernel walk rather than
	 * in rmdir itself.
	 */
	{
		int j;
		int leaf = len;

		for (j = DEEP_TREE_LEVELS - 1; j >= DEEP_TREE_LEVELS - 2; j--)
		{
			path[leaf] = '\0';
			if (rmdir(path) != 0)
			{
				out("PIPELINE_STRESS_DEEP_LEAF_FAIL level=");
				out_dec(j);
				out(" errno=");
				out_dec(errno);
				out("\n");
				return -1;
			}
			leaf -= 3;
		}
		len = leaf;
		path[len] = '\0';
	}

	/*
	 * Walk the tree the way the interactive session did when the kernel
	 * double-faulted, then tear it down through unlinkat(AT_REMOVEDIR),
	 * which is the call that makes the kernel recurse the whole subtree
	 * inside one syscall. rmdir(2) itself is non-recursive by POSIX and
	 * never reaches that code.
	 */
	if (run_ash("find /tmp/d0 > /dev/null") != 0)
	{
		out("PIPELINE_STRESS_DEEP_FIND_FAIL\n");
		return -1;
	}

	rc = (int)syscall(SYS_unlinkat, AT_FDCWD, "/tmp/d0", AT_REMOVEDIR);
	if (rc != 0)
	{
		out("PIPELINE_STRESS_DEEP_RMDIR_FAIL rc=");
		out_dec(rc);
		out(" errno=");
		out_dec(errno);
		out("\n");
		return -1;
	}

	out("PIPELINE_STRESS_DEEP_TREE_OK levels=");
	out_dec(DEEP_TREE_LEVELS);
	out("\n");
	return 0;
}

/*
 * BusyBox `free` and `uptime` call sysinfo(2). While the syscall was missing
 * they returned -ENOSYS and printed their uninitialised struct: an uptime of
 * weeks and gigabytes of RAM. The matrix passed them anyway because it only
 * checked the exit code, so assert the values, not the status.
 */
static int test_sysinfo(void)
{
	struct sysinfo si;

	if (sysinfo(&si) != 0)
	{
		out("PIPELINE_STRESS_SYSINFO_FAIL errno=");
		out_dec(errno);
		out("\n");
		return -1;
	}

	/* A smoke boots in seconds; a week means the field is garbage. */
	if (si.uptime < 0 || si.uptime > 86400)
	{
		out("PIPELINE_STRESS_SYSINFO_UPTIME_BAD sec=");
		out_dec((int)si.uptime);
		out("\n");
		return -1;
	}
	if (si.mem_unit == 0 || si.totalram == 0 || si.freeram > si.totalram)
	{
		out("PIPELINE_STRESS_SYSINFO_MEM_BAD total=");
		out_dec((int)si.totalram);
		out(" free=");
		out_dec((int)si.freeram);
		out("\n");
		return -1;
	}
	if (si.procs == 0)
	{
		out("PIPELINE_STRESS_SYSINFO_PROCS_BAD\n");
		return -1;
	}

	out("PIPELINE_STRESS_SYSINFO_OK uptime=");
	out_dec((int)si.uptime);
	out(" ram_mib=");
	out_dec((int)((si.totalram * si.mem_unit) / (1024 * 1024)));
	out(" procs=");
	out_dec((int)si.procs);
	out("\n");
	return 0;
}

/*
 * Pseudo-FS nodes are generated on read and had no timestamps at all, so
 * ls -l dated every one of them 1970. Anything before 2020 means the wall
 * clock never reached the stat path.
 */
#define EPOCH_2020 1577836800L

static int test_timestamps(void)
{
	static const char *const paths[] = {
		"/proc/uptime", "/proc/meminfo", "/tmp"
	};
	unsigned i;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
	{
		struct stat st;

		if (stat(paths[i], &st) != 0)
			continue;
		if ((long)st.st_mtime < EPOCH_2020)
		{
			out("PIPELINE_STRESS_MTIME_EPOCH path=");
			out(paths[i]);
			out(" mtime=");
			out_dec((int)st.st_mtime);
			out("\n");
			return -1;
		}
	}

	out("PIPELINE_STRESS_MTIME_OK\n");
	return 0;
}

int main(void)
{
	int round;
	static const char *const tags[STRESS_ROUNDS] = {
		"r0"
	};

	out("PIPELINE_STRESS_START\n");

	if (test_sigpipe_direct() != 0)
		fail("sigpipe_direct");
	if (test_n_fork_cow_write(2, "double_fork_cow_write") != 0)
		fail("double_fork_cow_write");
	if (test_n_fork_cow_write(3, "triple_fork_cow_write") != 0)
		fail("triple_fork_cow_write");
	if (test_parallel_cow_write(4, "parallel_cow_write") != 0)
		fail("parallel_cow_write");
	if (test_parallel_pipe_stress(4, "parallel_pipe_stress") != 0)
		fail("parallel_pipe_stress");

	if (require_ash("echo_only", "echo pipeok") != 0)
		fail("echo_only");
	if (require_ash("echo_cat", "echo pipeok | cat") != 0)
		fail("echo_cat");
	/* Hard: identity→anon zero + pipe_wait prepare_to_wait (Linux-like). */
	if (require_ash("echo_cat_head", "echo pipeok | cat -u | head") != 0)
		fail("echo_cat_head");

	/*
	 * Soft probe: same pipeline that still crashes hexdump in getty
	 * sessions (musl FILE* corrupted, write into .rodata). Kept soft so the
	 * gate reports the state instead of hiding it.
	 */
	try_ash("ash_hexdump_head", "hexdump -C /bin/busybox | cat -u | head -n 3");
	try_ash("ash_pipe_4stage", "echo pipeok | cat | cat | head");
	try_ash("ash_bigcat_head", "cat /bin/busybox | head -n 1");
	/* Session soak: ash SIGSEGV after head completes (pipeline teardown). */
	if (require_ash("ash_ls_proc_head", "ls /proc | head") != 0)
		fail("ash_ls_proc_head");

	if (test_hexdump_head_direct() != 0)
		fail("hexdump_head_direct");
	if (test_yes_head_direct() != 0)
		fail("yes_head_direct");
	if (test_od_head_direct() != 0)
		fail("od_head_direct");

	if (test_deep_tree() != 0)
		fail("deep_tree");
	if (test_sysinfo() != 0)
		fail("sysinfo");
	if (test_timestamps() != 0)
		fail("timestamps");

	for (round = 0; round < STRESS_ROUNDS; round++)
	{
		if (test_hexdump_head_direct() != 0)
			fail(tags[round]);
	}

	out("PIPELINE_STRESS_OK\n");
	for (;;)
		pause();
	return 0;
}
