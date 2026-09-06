/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_sigreturn_sleep_eintr_abi.c
 * Description: Host ABI — blocked syscall resume must not use handler GPRs.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/errno.h>
#include <ir0/signal_syscall_resume.h>
#include <ir0/syscall_frame.h>
#include <stdint.h>
#include <string.h>

void test_sigreturn_sleep_eintr_frame_abi(void)
{
	arch_syscall_frame_t block;
	arch_syscall_frame_t out;
	uint64_t rax;
	const uint64_t handler_rdi = 17ULL; /* SIGCHLD in ash handler path */

	TEST_BEGIN("sigreturn_sleep_eintr_frame_abi");

	memset(&block, 0, sizeof(block));
	block.rdi = 0x7fdf8000ULL;
	block.rsi = 1000ULL;
	block.rdx = 0ULL;
	block.rip = 0x4017b2ULL;

	signal_resume_blocked_syscall_frame(&out, &rax, &block, 0, 0);
	ASSERT_EQ((int64_t)rax, (int64_t)(-EINTR));
	ASSERT_EQ(out.rdi, block.rdi);
	ASSERT_EQ(out.rsi, block.rsi);
	ASSERT_NE(out.rdi, handler_rdi);

	signal_resume_blocked_syscall_frame(&out, &rax, &block, 1, 0u);
	ASSERT_EQ(rax, 0u);
	ASSERT_EQ(out.rip, block.rip - 2);
	ASSERT_EQ(out.rdi, block.rdi);

	ASSERT(signal_blocked_syscall_is_console_read(0u, 0));
	ASSERT(!signal_blocked_syscall_is_console_read(0u, 1));
	ASSERT(!signal_blocked_syscall_is_console_read(1u, 0));

	TEST_END();
}
