/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_named_fifo_supervise.c
 * Description: Host contract test — runsv supervise named FIFO (mknod/stat)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/named_fifo.h>
#include <ir0/path.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <string.h>

static void reset_named_fifo_table(void)
{
	const char *paths[] = {
		"/etc/runit/sv/console/supervise/control",
		"/etc/runit/sv/console/supervise/ok",
		"/etc/runit/sv/logger/supervise/control",
		"/etc/runit/sv/logger/log/supervise/control",
		"/etc/runit/sv/console/supervise/control/",
		NULL
	};
	int i;

	for (i = 0; paths[i]; i++)
		(void)named_fifo_unlink(paths[i]);
}

void test_named_fifo_supervise(void)
{
	stat_t st;
	pipe_t *pipe;
	int rc;

	TEST_BEGIN("named_fifo_supervise_abi");
	ASSERT_EQ((unsigned)(S_IFIFO | 0600), (unsigned)(0010000U | 0600U));
	ASSERT(S_ISFIFO(S_IFIFO | 0600));
	ASSERT(!S_ISDIR(S_IFIFO | 0600));
	TEST_END();

	TEST_BEGIN("named_fifo_supervise_paths");
	ASSERT(named_fifo_is_runsv_supervise_path(
		"/etc/runit/sv/console/supervise/control"));
	ASSERT(named_fifo_is_runsv_supervise_path(
		"/etc/runit/sv/logger/log/supervise/ok"));
	ASSERT(named_fifo_is_runsv_supervise_regular_path(
		"/etc/runit/sv/console/supervise/pid.new"));
	ASSERT(named_fifo_is_runsv_supervise_regular_path(
		"/etc/runit/sv/logger/supervise/lock"));
	ASSERT(!named_fifo_is_runsv_supervise_path(
		"/etc/runit/sv/console/supervise/pid"));
	ASSERT(!named_fifo_is_runsv_supervise_path(
		"/etc/runit/sv/console/supervise/lock"));
	TEST_END();

	reset_named_fifo_table();

	TEST_BEGIN("named_fifo_supervise_mknod_stat");
	rc = named_fifo_create("/etc/runit/sv/console/supervise/control", 0600);
	ASSERT_EQ(rc, 0);

	rc = named_fifo_stat("/etc/runit/sv/console/supervise/control", &st);
	ASSERT_EQ(rc, 0);
	ASSERT(S_ISFIFO(st.st_mode));
	ASSERT_EQ((unsigned)(st.st_mode & 0777), 0600U);

	pipe = named_fifo_lookup("/etc/runit/sv/console/supervise/control");
	ASSERT(pipe != NULL);
	TEST_END();

	TEST_BEGIN("named_fifo_supervise_idempotent");
	rc = named_fifo_create("/etc/runit/sv/console/supervise/control", 0600);
	ASSERT_EQ(rc, 0);
	rc = named_fifo_stat("/etc/runit/sv/console/supervise/control", &st);
	ASSERT_EQ(rc, 0);
	ASSERT(S_ISFIFO(st.st_mode));
	TEST_END();

	TEST_BEGIN("named_fifo_supervise_normalize");
	rc = named_fifo_create(
		"/etc/runit/sv/console/supervise/control/../control", 0600);
	ASSERT_EQ(rc, 0);
	rc = named_fifo_stat(
		"//etc/runit/sv/console/supervise/control", &st);
	ASSERT_EQ(rc, 0);
	ASSERT(S_ISFIFO(st.st_mode));

	{
		char norm[256];

		ASSERT_EQ(normalize_path("/etc/foo/./bar", norm, sizeof(norm)), 0);
		ASSERT_STR_EQ(norm, "/etc/foo/bar");
	}
	TEST_END();

	reset_named_fifo_table();

	/*
	 * Regression: last FD close must not free named FIFO pipe_t; unlink
	 * (or table recycle) is the sole destroy path. Otherwise runsv hits
	 * alloc_free double-free when supervise/control is reopened.
	 */
	TEST_BEGIN("named_fifo_close_then_unlink_no_double_free");
	{
		pipe_t *p;
		uint64_t created0 = 0, destroyed0 = 0;
		uint64_t created1 = 0, destroyed1 = 0;

		pipe_stats_get(&created0, &destroyed0);
		rc = named_fifo_create("/etc/runit/sv/console/supervise/ok", 0600);
		ASSERT_EQ(rc, 0);
		p = named_fifo_lookup("/etc/runit/sv/console/supervise/ok");
		ASSERT(p != NULL);
		ASSERT_EQ(p->named, 1);

		pipe_acquire_end(p, 0);
		pipe_acquire_end(p, 1);
		pipe_close_end(p, 0);
		pipe_close_end(p, 1);

		pipe_stats_get(&created1, &destroyed1);
		ASSERT_EQ(destroyed1, destroyed0); /* still owned by inode */

		p = named_fifo_lookup("/etc/runit/sv/console/supervise/ok");
		ASSERT(p != NULL);
		ASSERT_EQ(p->fd_refs, 0);

		/* Reopen after last close must work (runsv supervise). */
		pipe_acquire_end(p, 0);
		ASSERT_EQ(p->fd_refs, 1);
		ASSERT_EQ(p->closed_read, 0);
		pipe_close_end(p, 0);

		rc = named_fifo_unlink("/etc/runit/sv/console/supervise/ok");
		ASSERT_EQ(rc, 0);
		pipe_stats_get(&created1, &destroyed1);
		ASSERT_EQ(destroyed1, destroyed0 + 1);
		ASSERT_EQ(created1, created0 + 1);
	}
	TEST_END();

	reset_named_fifo_table();
}
