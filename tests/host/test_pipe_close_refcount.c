/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_pipe_close_refcount.c
 * Description: Regression — pipe_close_end frees once; wake before free.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/pipe.h>

void test_pipe_close_end_last_ref_frees_once(void)
{
	pipe_t *p;
	uint64_t created0 = 0, destroyed0 = 0;
	uint64_t created1 = 0, destroyed1 = 0;

	TEST_BEGIN("pipe_close_end frees only on last fd_ref");
	pipe_stats_get(&created0, &destroyed0);
	p = pipe_create();
	ASSERT(p != 0);
	/* Match sys_pipe2: create then acquire each end. */
	pipe_acquire_end(p, 0);
	pipe_acquire_end(p, 1);
	ASSERT_EQ(p->fd_refs, 2);
	ASSERT_EQ(p->readers, 1);
	ASSERT_EQ(p->writers, 1);

	pipe_close_end(p, 0);
	ASSERT_EQ(p->fd_refs, 1);
	ASSERT_EQ(p->closed_read, 1);

	pipe_stats_get(&created1, &destroyed1);
	ASSERT_EQ(destroyed1, destroyed0);

	pipe_close_end(p, 1);
	p = 0;

	pipe_stats_get(&created1, &destroyed1);
	ASSERT_EQ(created1, created0 + 1);
	ASSERT_EQ(destroyed1, destroyed0 + 1);
	TEST_END();
}

void test_pipe_pipeline_two_closes_destroy_once(void)
{
	pipe_t *p;
	uint64_t destroyed0 = 0, destroyed1 = 0;

	TEST_BEGIN("pipeline-style two ends → one destroy");
	pipe_stats_get(0, &destroyed0);
	p = pipe_create();
	ASSERT(p != 0);
	pipe_acquire_end(p, 0);
	pipe_acquire_end(p, 1);
	pipe_close_end(p, 1);
	pipe_close_end(p, 0);
	p = 0;
	pipe_stats_get(0, &destroyed1);
	ASSERT_EQ(destroyed1, destroyed0 + 1);
	TEST_END();
}
