/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_musl_cred_abi.c
 * Description: Host tests — musl/Linux credential and signal ABI layout
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/syscall_linux.h>
#include <ir0/abi/musl_cred_abi.h>

void test_musl_cred_abi(void)
{
	TEST_BEGIN("musl_cred_abi");
	ASSERT(IR0_SIGSET_SIZE == 128);
	ASSERT(IR0_SIGACTION_MIN_SIZE >= 152);
	ASSERT(__NR_set_robust_list == 273);
	ASSERT(__NR_getgroups == 115);
	ASSERT(__NR_tkill == 200);
	ASSERT(__NR_tgkill == 234);
	TEST_END();
}
