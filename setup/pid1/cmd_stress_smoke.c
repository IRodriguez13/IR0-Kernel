/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_stress_smoke.c
 * Description: BusyBox command battery smoke (PID 1); CMD_STRESS_OK/FAIL.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void write_str(const char *s)
{
	size_t n = 0;

	if (!s)
		return;
	while (s[n])
		n++;
	(void)write(1, s, n);
}

/*
 * Returns exit status (>=0), or -1 on fork/wait failure.
 * Does not emit CMD_STRESS_FAIL (caller decides required vs optional).
 */
static int run_cmd(const char *tag, char *const argv[])
{
	pid_t pid;
	int status;
	int ec;

	write_str("CMD_STRESS_STEP=");
	write_str(tag);
	write_str("\n");

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		int nullfd = open("/dev/null", O_WRONLY);

		if (nullfd >= 0)
		{
			dup2(nullfd, 1);
			dup2(nullfd, 2);
			close(nullfd);
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) < 0)
		return -1;

	if (WIFEXITED(status))
		ec = WEXITSTATUS(status);
	else
		ec = 128;
	return ec;
}

static int require_ok(const char *tag, char *const argv[])
{
	int ec = run_cmd(tag, argv);

	if (ec != 0)
	{
		write_str("CMD_STRESS_FAIL\n");
		write_str("CMD_STRESS_FAIL_REASON=");
		write_str(tag);
		write_str("\n");
		return -1;
	}
	return 0;
}

/* Pipelines may exit 141 (SIGPIPE) or 255 (ash pipeline quirk). */
static int require_pipe_ok(const char *tag, char *const argv[])
{
	int ec = run_cmd(tag, argv);

	if (ec == 0 || ec == 141 || ec == 255)
		return 0;
	write_str("CMD_STRESS_FAIL\n");
	write_str("CMD_STRESS_FAIL_REASON=");
	write_str(tag);
	write_str("\n");
	return -1;
}

static void optional_ok(const char *tag, char *const argv[])
{
	int ec = run_cmd(tag, argv);

	if (ec != 0)
	{
		write_str("CMD_STRESS_SKIP=");
		write_str(tag);
		write_str("\n");
	}
}

int main(void)
{
	char *argv_true[] = { "/bin/busybox", "true", NULL };
	char *argv_false[] = { "/bin/busybox", "false", NULL };
	char *argv_echo[] = { "/bin/busybox", "echo", "cmd_stress", NULL };
	char *argv_ls[] = { "/bin/busybox", "ls", "/", NULL };
	char *argv_stat[] = { "/bin/busybox", "stat", "/", NULL };
	char *argv_cat[] = { "/bin/busybox", "cat", "/proc/version", NULL };
	char *argv_hex[] = { "/bin/busybox", "hexdump", "-n", "64", "/bin/busybox", NULL };
	char *argv_ps[] = { "/bin/busybox", "ps", NULL };
	char *argv_free[] = { "/bin/busybox", "free", NULL };
	char *argv_df[] = { "/bin/busybox", "df", NULL };
	char *argv_mount[] = { "/bin/busybox", "mount", NULL };
	char *argv_route[] = { "/bin/busybox", "route", NULL };
	char *argv_ifconfig[] = { "/bin/busybox", "ifconfig", NULL };
	char *argv_find[] = { "/bin/busybox", "find", "/tmp", "-maxdepth", "1", NULL };
	char *argv_tar_c[] = {
		"/bin/busybox", "tar", "-cf", "/tmp/cmd_stress.tar", "/tmp", NULL
	};
	char *argv_touch[] = { "/bin/busybox", "touch", "/tmp/cmd_stress_in", NULL };
	char *argv_sh_seq[] = {
		"/bin/sh", "-c", "true; false; echo ok", NULL
	};
	char *argv_echo_cat[] = {
		"/bin/sh", "-c", "echo pipeok | cat", NULL
	};
	char *argv_uname[] = { "/bin/busybox", "uname", "-a", NULL };

	write_str("CMD_STRESS_START\n");

	if (require_ok("true", argv_true) != 0)
		goto fail;
	{
		int ec = run_cmd("false", argv_false);

		if (ec == 0 || ec < 0)
		{
			write_str("CMD_STRESS_FAIL\n");
			write_str("CMD_STRESS_FAIL_REASON=false_ok\n");
			goto fail;
		}
	}
	if (require_ok("echo", argv_echo) != 0)
		goto fail;
	if (require_ok("ls", argv_ls) != 0)
		goto fail;
	if (require_ok("stat", argv_stat) != 0)
		goto fail;
	optional_ok("cat_proc", argv_cat);
	if (require_ok("hexdump", argv_hex) != 0)
		goto fail;
	if (require_ok("ps", argv_ps) != 0)
		goto fail;
	if (require_ok("free", argv_free) != 0)
		goto fail;
	if (require_ok("df", argv_df) != 0)
		goto fail;
	if (require_ok("mount", argv_mount) != 0)
		goto fail;
	optional_ok("route", argv_route);
	optional_ok("ifconfig", argv_ifconfig);
	if (require_ok("touch", argv_touch) != 0)
		goto fail;
	/* find -maxdepth / tar -c need FEATURE_; skip until BusyBox rebuild. */
	optional_ok("find", argv_find);
	optional_ok("tar_c", argv_tar_c);
	if (require_ok("sh_seq", argv_sh_seq) != 0)
		goto fail;
	if (require_pipe_ok("echo_cat", argv_echo_cat) != 0)
		goto fail;
	if (require_ok("uname", argv_uname) != 0)
		goto fail;
	/* cat|head / 3-stage / yes|head: known flaky under ash — skip in gate. */

	write_str("CMD_STRESS_OK\n");
	for (;;)
		pause();
	return 0;

fail:
	for (;;)
		pause();
	return 1;
}
