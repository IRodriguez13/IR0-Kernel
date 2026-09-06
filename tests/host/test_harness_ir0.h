/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_harness_ir0.h
 * Description: Host harness macros without stdlib.h (avoids dev_t vs IR0 types conflicts).
 */

#ifndef IR0_TEST_HARNESS_IR0_H
#define IR0_TEST_HARNESS_IR0_H

#include <stdio.h>
#include <string.h>

extern int _ir0_test_failed;
extern int _ir0_test_count;
extern int _ir0_test_pass;

#define TEST_BEGIN(name) do { \
	_ir0_test_failed = 0; \
	_ir0_test_count++; \
	fprintf(stderr, "[TEST] %s ... ", (name)); \
	fflush(stderr); \
} while (0)

#define TEST_END() do { \
	if (_ir0_test_failed) { \
		fprintf(stderr, "FAIL\n"); \
		_ir0_test_pass = 0; \
	} else { \
		fprintf(stderr, "PASS\n"); \
	} \
} while (0)

#define ASSERT(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "\n[TEST] ASSERT failed: %s (%s:%d)\n", \
			#cond, __FILE__, __LINE__); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_EQ(a, b) do { \
	if ((a) != (b)) { \
		fprintf(stderr, "\n[TEST] ASSERT_EQ failed: %s == %s (%s:%d) (%ld != %ld)\n", \
			#a, #b, __FILE__, __LINE__, (long)(a), (long)(b)); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_NE(a, b) do { \
	if ((a) == (b)) { \
		fprintf(stderr, "\n[TEST] ASSERT_NE failed: %s != %s (%s:%d) (both %ld)\n", \
			#a, #b, __FILE__, __LINE__, (long)(a)); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
	if (strcmp((a), (b)) != 0) { \
		fprintf(stderr, "\n[TEST] ASSERT_STR_EQ failed: \"%s\" != \"%s\" (%s:%d)\n", \
			(a), (b), __FILE__, __LINE__); \
		_ir0_test_failed = 1; \
	} \
} while (0)

#endif
