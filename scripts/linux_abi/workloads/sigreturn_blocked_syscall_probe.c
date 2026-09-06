/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sigreturn_blocked_syscall_probe.c
 * Description: Blocked read(2) + signal + rt_sigreturn (EINTR / SA_RESTART) audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void audit_step(unsigned step, const char *op, long ret, int err)
{
	char buf[192];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][sigreturn_blocked_syscall] "
		     "step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static void usr1_handler(int sig)
{
	(void)sig;
}

static int test_eintr_no_restart(void)
{
	int pfd[2];
	pid_t pid;
	char c;
	struct sigaction sa;

	if (pipe(pfd) != 0)
		return 1;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = usr1_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		return 1;

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0)
	{
		close(pfd[0]);
		usleep(250000);
		(void)kill(getppid(), SIGUSR1);
		usleep(250000);
		c = 'a';
		(void)write(pfd[1], &c, 1);
		close(pfd[1]);
		_exit(0);
	}

	close(pfd[1]);
	errno = 0;
	{
		long ret = (long)read(pfd[0], &c, 1);
		int err = errno;

		audit_step(0, "read_eintr", ret, err);
		if (ret != -1 || err != EINTR)
		{
			(void)waitpid(pid, NULL, 0);
			close(pfd[0]);
			return 1;
		}
	}

	errno = 0;
	{
		long ret = (long)read(pfd[0], &c, 1);
		int err = errno;

		audit_step(1, "read_after_eintr", ret, err);
		if (ret != 1 || c != 'a')
		{
			(void)waitpid(pid, NULL, 0);
			close(pfd[0]);
			return 1;
		}
	}

	(void)waitpid(pid, NULL, 0);
	close(pfd[0]);
	return 0;
}

static int test_sa_restart(void)
{
	int pfd[2];
	pid_t pid;
	char c;
	struct sigaction sa;

	if (pipe(pfd) != 0)
		return 1;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = usr1_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		return 1;

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0)
	{
		close(pfd[0]);
		usleep(250000);
		(void)kill(getppid(), SIGUSR1);
		usleep(250000);
		c = 'b';
		(void)write(pfd[1], &c, 1);
		close(pfd[1]);
		_exit(0);
	}

	close(pfd[1]);
	errno = 0;
	{
		long ret = (long)read(pfd[0], &c, 1);
		int err = errno;

		audit_step(2, "read_sa_restart", ret, err);
		if (ret != 1 || c != 'b')
		{
			(void)waitpid(pid, NULL, 0);
			close(pfd[0]);
			return 1;
		}
	}

	(void)waitpid(pid, NULL, 0);
	close(pfd[0]);
	return 0;
}

int main(void)
{
	if (test_eintr_no_restart() != 0)
		return 1;
	if (test_sa_restart() != 0)
		return 1;

	(void)write(1, "[SIGRETURNEINTROK]\n", 19);
	return 0;
}
