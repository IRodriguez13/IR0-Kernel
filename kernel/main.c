// SPDX-License-Identifier: GPL-3.0-only
/*
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: main.c
 * Description: Kernel idle helpers and kmain boot orchestrator.
 *
 *    00000000: 01010011 01101001 01100011 00100000 01110000 01100001
 *    00000006: 01110010 01110110 01101001 01110011 00100000 01101101
 *    0000000c: 01100001 01100111 01101110 01100001
 *
 */

#include <stdint.h>
#include <config.h>
#include <ir0/arch_port.h>
#include <ir0/clock_wait.h>
#include <ir0/console.h>
#include <ir0/input_backend.h>
#include <ir0/poll.h>
#include <ir0/sched.h>
#include "kernel.h"
#include "boot_init.h"
#include "syscalls.h"
#include "syscalls/io_syscalls.h"

#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

/*
 * kernel_idle_poll_nosched - Same wakes as kernel_idle_poll without scheduling.
 * Used from clock_wait / blocked syscall loops that own a single yield point.
 */
void kernel_idle_poll_nosched(void)
{
#if CONFIG_ENABLE_NETWORKING
	net_stack_poll();
#endif
#if CONFIG_ENABLE_BLUETOOTH
	ir0_bluetooth_poll();
#endif
	(void)poll_wake_check_nosched();
	sleep_wake_check();
	input_kbd_poll_ps2();
	(void)stdin_wake_check_nosched();
	pipe_wake_check();
	(void)ir0_console_take_resched();
}

/*
 * kernel_idle_poll - Wake blocked tasks and poll optional subsystems.
 * Shared by the RR idle kernel process and the kmain fallback loop.
 */
void kernel_idle_poll(void)
{
	int woke = 0;

	enable_interrupts();
#if CONFIG_ENABLE_NETWORKING
	net_stack_poll();
#endif
#if CONFIG_ENABLE_BLUETOOTH
	ir0_bluetooth_poll();
#endif
	if (poll_wake_check_nosched())
		woke = 1;
	sleep_wake_check();
	input_kbd_poll_ps2();
	if (stdin_wake_check_nosched())
		woke = 1;
	pipe_wake_check();
	if (ir0_console_take_resched())
		woke = 1;
	if (woke)
		sched_schedule_next();
}

/*
 * kernel_idle_loop - Always-runnable kernel task (Linux idle analogue).
 * Enqueued after /sbin/init so PID 1 runs first; takes over when init exits.
 */
void kernel_idle_loop(void)
{
	for (;;)
	{
		enable_interrupts();
		kernel_idle_poll();
		/*
		 * IRQ preempt only returns to ring 3 today. Idle must yield
		 * explicitly when a higher-priority task is runnable.
		 */
		if (sched_count_runnable() > 1)
			sched_schedule_next();
		else
			cpu_idle();
	}
}

void kmain(uint32_t multiboot_info)
{
	boot_early(multiboot_info);
	boot_memory_serial(multiboot_info);
	boot_drivers_rootfs();
	boot_runtime();
	boot_diagnostics();
	boot_enter_userspace();

	for (;;)
	{
		kernel_idle_poll();
		cpu_idle();
	}
}
