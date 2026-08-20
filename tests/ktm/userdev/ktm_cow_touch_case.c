/**
 * IR0 userspace — KTM hybrid pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_cow_touch_case.c
 * Description: PID1 — fork + child write shared page (COW touch MVP);
 *              MAP_FIXED across 6 MiB identity seam + anonymous mmap touch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

static volatile char g_cow_page[4096] __attribute__((aligned(4096)));

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

int main(void)
{
	int fd;
	int fails = 0;
	pid_t pid;
	int status = -1;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before, after;

	memset((void *)g_cow_page, 'A', sizeof(g_cow_page));

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "cow_touch") != 0)
	{
		say("KTM_USERDEV_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	pid = fork();
	if (pid < 0)
	{
		(void)ktm_assert_true(fd, "fork_ok", 0);
		fails++;
		goto done;
	}
	if (pid == 0)
	{
		g_cow_page[0] = 'B';
		_exit(g_cow_page[0] == 'B' ? 0 : 2);
	}

	(void)ktm_assert_true(fd, "fork_ok", 1);
	(void)ktm_checkpoint(fd, "child_spawned");

	if (waitpid(pid, &status, 0) != pid)
	{
		(void)ktm_assert_true(fd, "waitpid_ok", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(fd, "waitpid_ok", 1);
		(void)ktm_assert_true(fd, "child_exit_ok",
				      WIFEXITED(status) && WEXITSTATUS(status) == 0);
		if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
			fails++;
	}

	/* Parent page must remain 'A' if COW (or shared-then-copy) worked. */
	(void)ktm_assert_true(fd, "parent_page_intact", g_cow_page[0] == 'A');
	if (g_cow_page[0] != 'A')
		fails++;

	/*
	 * Split-proof: MAP_FIXED into the 6 MiB supervisor-2MB identity slot
	 * (ISD ELF/brk window). Anonymous mmap stays in the high arena.
	 */
	{
		int mmap6_ok = 0;
		int mmap_ok = 0;
		char *mmap6;
		char *mmap_p;

		mmap6 = mmap((void *)0x600000UL, 4096, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
		if (mmap6 != MAP_FAILED)
		{
			mmap6[0] = 0x5A;
			mmap6[4095] = 0x5A;
			mmap6_ok = (mmap6[0] == 0x5A && mmap6[4095] == 0x5A);
			(void)munmap(mmap6, 4096);
		}
		(void)ktm_assert_true(fd, "mmap_fixed_6m_split", mmap6_ok);
		if (!mmap6_ok)
			fails++;

		mmap_p = mmap(NULL, 16 * 1024, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mmap_p != MAP_FAILED)
		{
			mmap_p[0] = 0x5A;
			mmap_p[16 * 1024 - 1] = 0x5A;
			mmap_ok = (mmap_p[0] == 0x5A &&
				   mmap_p[16 * 1024 - 1] == 0x5A);
			(void)munmap(mmap_p, 16 * 1024);
		}
		(void)ktm_assert_true(fd, "mmap_anon_touch", mmap_ok);
		if (!mmap_ok)
			fails++;
	}

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else
	{
		/* COW may grow used_frames; only assert process count / zombies. */
		(void)ktm_assert_true(fd, "no_process_leak",
				      ktm_snapshot_no_process_leak(&before, &after) == 0);
		if (ktm_snapshot_no_process_leak(&before, &after) != 0)
			fails++;
		(void)ktm_assert_true(fd, "no_zombie_growth",
				      after.zombies <= before.zombies);
		if (after.zombies > before.zombies)
			fails++;
	}

done:
	(void)ktm_case_end(fd, "cow_touch", fails == 0 ? 0 : 1);
	ktm_close(fd);

	if (fails == 0)
	{
		say("KTM_USERDEV_COW_OK\n");
		say("KTM_USERDEV_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FAIL\n");
	return 1;
}
