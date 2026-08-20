/**
 * IR0 userspace — KTM exec-drain case (FASE44 exec-drain analogue)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_exec_drain_case.c
 * Description: fork → exec(/bin/f41true) → wait storm with /dev/ktm asserts.
 *              PID1 touches musl TLS/brk/mmap between cycles so FS=0 / brk
 *              ABI holes cannot hide behind a write()+wait-only parent.
 *              Optional virtio-9p host share report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

#ifndef STORM_N
/*
 * Historical PID1 #PF (addr=0x48, musl __ctype_b_loc with FS=0) showed up
 * after ~80 fork+exec cycles. Stay above that window.
 */
#define STORM_N 192
#endif

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

/*
 * Exercise PID1 TLS (ctype/errno) and heap/mmap after each child exec.
 * A clobbered IA32_FS_BASE or a brk that returns -EFAULT here SEGV/fails
 * instead of letting the drain "pass" on a write()-only parent.
 */
static int parent_tls_mm_ping(void)
{
	void *p;
	void *m;
	volatile int c;

	p = malloc(128);
	if (!p)
		return -1;
	memset(p, 0x5a, 128);
	c = isspace(' ') && isdigit('7');
	((char *)p)[0] = (char)c;
	free(p);

	m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		 -1, 0);
	if (m == MAP_FAILED)
		return -1;
	((volatile char *)m)[0] = (char)c;
	if (munmap(m, 4096) != 0)
		return -1;
	return 0;
}

static int exec_drain_batch(int n, int *started, int *fork_fail, int *exec_fail,
			    int *wait_fail, int *tls_fail)
{
	int i;
	char *argv[] = { "/bin/f41true", NULL };

	*started = 0;
	*fork_fail = 0;
	*exec_fail = 0;
	*wait_fail = 0;
	*tls_fail = 0;
	for (i = 0; i < n; i++)
	{
		pid_t pid = fork();

		if (pid == 0)
		{
			execve("/bin/f41true", argv, NULL);
			_exit(127);
		}
		if (pid < 0)
		{
			(*fork_fail)++;
			continue;
		}
		(*started)++;
		{
			int status = 0;

			if (waitpid(pid, &status, 0) < 0)
				(*wait_fail)++;
			else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
				(*exec_fail)++;
		}
		if (parent_tls_mm_ping() != 0)
			(*tls_fail)++;
	}
	return (*fork_fail == 0 && *wait_fail == 0 && *exec_fail == 0 &&
		*tls_fail == 0 && *started == n)
		       ? 0
		       : -1;
}

static void try_hostshare_report(int ok)
{
	const char *payload = ok ? "KTM_USERDEV_EXEC_DRAIN_OK\n" : "KTM_USERDEV_EXEC_DRAIN_FAIL\n";
	(void)ktm_hostshare_report("ktm_exec_drain.txt", payload);
}


int main(void)
{
	int fd;
	int fails = 0;
	int started = 0, fork_fail = 0, exec_fail = 0, wait_fail = 0, tls_fail = 0;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before, after;

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_EXEC_DRAIN_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_EXEC_DRAIN_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "exec_drain") != 0)
	{
		say("KTM_USERDEV_EXEC_DRAIN_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	(void)ktm_checkpoint(fd, "exec_drain_begin");
	if (exec_drain_batch(STORM_N, &started, &fork_fail, &exec_fail, &wait_fail,
			     &tls_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "exec_started", (uint64_t)STORM_N, (uint64_t)started) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "exec_fork_errs", 0, (uint64_t)fork_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "exec_wait_errs", 0, (uint64_t)wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "exec_status_errs", 0, (uint64_t)exec_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "exec_tls_errs", 0, (uint64_t)tls_fail) != 0)
		fails++;

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else if (ktm_assert_true(fd, "no_zombie_growth",
				 after.zombies <= before.zombies) != 0)
		fails++;

	(void)ktm_case_end(fd, "exec_drain", fails == 0 ? 0 : 1);
	ktm_close(fd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("KTM_USERDEV_EXEC_DRAIN_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("KTM_USERDEV_EXEC_DRAIN_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
