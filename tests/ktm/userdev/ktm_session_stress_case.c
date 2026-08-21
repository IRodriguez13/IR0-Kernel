/**
 * IR0 userspace — KTM session-stress case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_session_stress_case.c
 * Description: PID1 session loop against packed ISD BusyBox applets (echo,
 *              echo|cat, true, busybox echo, uname, ls) plus brk/mmap/ctype
 *              and stat(2) of those inodes between commands. This is the
 *              ash-like surface, not a stub drain.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

#ifndef STORM_N
#define STORM_N 64
#endif

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int parent_tls_mm_ping(void)
{
	void *p;
	void *m;
	volatile int c;

	p = malloc(256);
	if (!p)
		return -1;
	memset(p, 0xa5, 256);
	c = isspace('\t') && isdigit('3');
	((char *)p)[0] = (char)c;
	free(p);

	m = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		 -1, 0);
	if (m == MAP_FAILED)
		return -1;
	((volatile char *)m)[0] = 'S';
	((volatile char *)m)[4096] = 'T';
	return munmap(m, 8192);
}

static int parent_stat_ping(void)
{
	struct stat st;

	if (stat("/bin/busybox", &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_size <= 0)
		return -1;
	if (stat("/bin/echo", &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_size <= 0)
		return -1;
	if (stat("/bin/ls", &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode))
		return -1;
	return 0;
}

/*
 * Live ash does stat(2) of the same inode while another task execs it.
 * MINIX used to fill struct stat from a static inode the walker overwrote.
 */
static int overlap_stat_exec(void)
{
	pid_t pid;
	int status = 0;
	struct stat st;
	int i;
	char *argv[] = { "/bin/true", NULL };

	pid = fork();
	if (pid == 0)
	{
		execve(argv[0], argv, NULL);
		_exit(127);
	}
	if (pid < 0)
		return -1;

	for (i = 0; i < 32; i++)
	{
		if (stat("/bin/true", &st) != 0 || !S_ISREG(st.st_mode) ||
		    st.st_size <= 0)
		{
			(void)waitpid(pid, &status, 0);
			return -1;
		}
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

static int run_argv_capture(char *const argv[], char *out, size_t out_sz,
			    int *status_out)
{
	int pfd[2];
	pid_t pid;
	size_t got = 0;
	int status = 0;

	if (pipe(pfd) != 0)
		return -1;

	pid = fork();
	if (pid == 0)
	{
		(void)close(pfd[0]);
		if (dup2(pfd[1], 1) < 0)
			_exit(127);
		(void)close(pfd[1]);
		execve(argv[0], argv, NULL);
		_exit(127);
	}
	if (pid < 0)
	{
		(void)close(pfd[0]);
		(void)close(pfd[1]);
		return -1;
	}

	(void)close(pfd[1]);
	/*
	 * Reap before draining stdout. Overlapping the parent's read with
	 * exec of product BusyBox (666K) #PF'd PID1 at 0x80b695d8. Small
	 * applet output fits in the pipe buffer after the child exits.
	 */
	if (waitpid(pid, &status, 0) < 0)
	{
		(void)close(pfd[0]);
		return -1;
	}
	if (out && out_sz)
	{
		ssize_t n;

		while (got + 1 < out_sz)
		{
			n = read(pfd[0], out + got, out_sz - 1 - got);
			if (n <= 0)
				break;
			got += (size_t)n;
		}
		out[got] = '\0';
	}
	else
	{
		char dump[256];

		while (read(pfd[0], dump, sizeof(dump)) > 0)
			;
	}
	(void)close(pfd[0]);

	if (status_out)
		*status_out = status;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

/*
 * echo hi | cat — parent splices /bin/echo stdout into /bin/cat (same FD
 * contract as init_fase48_pipe_exec_only). Two concurrent children sharing
 * a pipe is a separate kernel surface; this is the ash-like command pair.
 */
static int run_echo_pipe_cat(void)
{
	char echoed[16];
	char buf[16];
	int inpipe[2];
	int outpipe[2];
	int status = 0;
	pid_t pid;
	size_t got = 0;
	ssize_t n;
	char *echo_argv[] = { "/bin/echo", "hi", NULL };
	char *cat_argv[] = { "/bin/cat", NULL };

	if (run_argv_capture(echo_argv, echoed, sizeof(echoed), &status) != 0)
		return -1;
	if (strcmp(echoed, "hi\n") != 0)
		return -1;

	if (pipe(inpipe) != 0 || pipe(outpipe) != 0)
		return -1;

	pid = fork();
	if (pid == 0)
	{
		if (dup2(inpipe[0], 0) < 0 || dup2(outpipe[1], 1) < 0)
			_exit(127);
		(void)close(inpipe[0]);
		(void)close(inpipe[1]);
		(void)close(outpipe[0]);
		(void)close(outpipe[1]);
		execve(cat_argv[0], cat_argv, NULL);
		_exit(127);
	}
	if (pid < 0)
		return -1;

	(void)close(inpipe[0]);
	(void)close(outpipe[1]);
	(void)write(inpipe[1], echoed, strlen(echoed));
	(void)close(inpipe[1]);
	if (waitpid(pid, &status, 0) < 0)
	{
		(void)close(outpipe[0]);
		return -1;
	}
	while (got + 1 < sizeof(buf))
	{
		n = read(outpipe[0], buf + got, sizeof(buf) - 1 - got);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	buf[got] = '\0';
	(void)close(outpipe[0]);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return strcmp(buf, "hi\n") == 0 ? 0 : -1;
}

static int session_tick(int *fork_fail, int *exec_fail, int *wait_fail,
			int *mm_fail, int *cmd_fail, int *stat_fail)
{
	char out[64];
	int status = 0;
	char *echo_argv[] = { "/bin/echo", "hi", NULL };
	char *true_argv[] = { "/bin/true", NULL };
	char *bb_argv[] = { "/bin/busybox", "echo", "ok", NULL };
	char *uname_argv[] = { "/bin/uname", NULL };
	char *ls_argv[] = { "/bin/ls", "/bin", NULL };

	if (parent_tls_mm_ping() != 0)
	{
		(*mm_fail)++;
		return -1;
	}

	if (parent_stat_ping() != 0)
	{
		say("SESSION_FAIL stat_ping\n");
		(*stat_fail)++;
		return -1;
	}

	if (overlap_stat_exec() != 0)
	{
		say("SESSION_FAIL stat_overlap_exec\n");
		(*stat_fail)++;
		return -1;
	}

	if (run_argv_capture(echo_argv, out, sizeof(out), &status) != 0)
	{
		say("SESSION_FAIL echo_exec\n");
		if (status == 0 && out[0] == '\0')
			(*fork_fail)++;
		else if (!WIFEXITED(status))
			(*wait_fail)++;
		else
			(*exec_fail)++;
		return -1;
	}
	if (strcmp(out, "hi\n") != 0)
	{
		say("SESSION_FAIL echo_stdout\n");
		(*cmd_fail)++;
		return -1;
	}

	if (run_echo_pipe_cat() != 0)
	{
		say("SESSION_FAIL echo_pipe_cat\n");
		(*cmd_fail)++;
		return -1;
	}

	if (run_argv_capture(true_argv, NULL, 0, &status) != 0)
	{
		say("SESSION_FAIL true_exec\n");
		(*exec_fail)++;
		return -1;
	}

	if (run_argv_capture(bb_argv, out, sizeof(out), &status) != 0)
	{
		say("SESSION_FAIL busybox_exec\n");
		(*exec_fail)++;
		return -1;
	}
	if (strcmp(out, "ok\n") != 0)
	{
		say("SESSION_FAIL busybox_stdout\n");
		(*cmd_fail)++;
		return -1;
	}

	if (run_argv_capture(uname_argv, NULL, 0, &status) != 0)
	{
		say("SESSION_FAIL uname_exec\n");
		(*exec_fail)++;
		return -1;
	}

	if (run_argv_capture(ls_argv, NULL, 0, &status) != 0)
	{
		say("SESSION_FAIL ls_exec\n");
		(*exec_fail)++;
		return -1;
	}

	return 0;
}

static void try_hostshare_report(int ok)
{
	const char *payload = ok ? "KTM_USERDEV_SESSION_STRESS_OK\n"
				 : "KTM_USERDEV_SESSION_STRESS_FAIL\n";
	(void)ktm_hostshare_report("ktm_session_stress.txt", payload);
}

int main(void)
{
	int fd;
	int fails = 0;
	int i;
	int started = 0, fork_fail = 0, exec_fail = 0, wait_fail = 0, mm_fail = 0,
	    cmd_fail = 0, stat_fail = 0;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before, after;

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_SESSION_STRESS_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_SESSION_STRESS_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "session_stress") != 0)
	{
		say("KTM_USERDEV_SESSION_STRESS_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	(void)ktm_checkpoint(fd, "session_stress_begin");
	for (i = 0; i < STORM_N; i++)
	{
		if (session_tick(&fork_fail, &exec_fail, &wait_fail, &mm_fail,
				 &cmd_fail, &stat_fail) == 0)
			started++;
	}

	if (started != STORM_N)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_ticks", (uint64_t)STORM_N,
			      (uint64_t)started) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_fork_errs", 0, (uint64_t)fork_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_exec_errs", 0, (uint64_t)exec_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_wait_errs", 0, (uint64_t)wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_mm_errs", 0, (uint64_t)mm_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_cmd_errs", 0, (uint64_t)cmd_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "session_stat_errs", 0, (uint64_t)stat_fail) != 0)
		fails++;

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else if (ktm_assert_true(fd, "no_zombie_growth",
				 after.zombies <= before.zombies) != 0)
		fails++;

	(void)ktm_case_end(fd, "session_stress", fails == 0 ? 0 : 1);
	ktm_close(fd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("KTM_USERDEV_SESSION_STRESS_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("KTM_USERDEV_SESSION_STRESS_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
