/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_events.c
 * Description: IR0 kernel source/header file
 */

/**
 * IR0 Kernel - Input event queue for /dev/events0 (Linux evdev)
 * Single producer (keyboard IRQ) / single consumer (read syscall)
 */
#include <stddef.h>
#include <ir0/input.h>
#include <ir0/time.h>
#include <ir0/clock.h>
#include <ir0/console.h>
#include <ir0/input_backend.h>
#include <string.h>
#include <ir0/arch_port.h>

/* Ring buffer: 64 input_event entries */
#define INPUT_EVENT_QUEUE_SIZE 64
static struct input_event event_queue[INPUT_EVENT_QUEUE_SIZE];
static volatile unsigned int ev_head;
static volatile unsigned int ev_tail;
static volatile int events_readers;

static inline uint64_t input_events_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void input_events_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

void input_events_reader_open(void)
{
	uint64_t irq_flags = input_events_irq_save();

	events_readers++;
	input_events_irq_restore(irq_flags);
}

void input_events_reader_close(void)
{
	int do_flush = 0;
	uint64_t irq_flags = input_events_irq_save();

	if (events_readers > 0)
		events_readers--;
	do_flush = (events_readers == 0);
	input_events_irq_restore(irq_flags);
	if (do_flush)
	{
		/*
		 * Last /dev/events0 reader: drop cooked LD state and any raw
		 * ASCII that may have landed while divert was ending. Flush
		 * alone no longer clears the kbd ring under ICANON.
		 */
		ir0_console_flush_input();
		input_kbd_clear();
	}
}

int input_events_readers_active(void)
{
	return events_readers > 0;
}

/* Called from keyboard IRQ handler - must be fast, no blocking */
void input_event_push(uint16_t type, uint16_t code, int32_t value)
{
    unsigned int next = (ev_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    if (next == ev_tail)
        return;  /* Buffer full, drop event */

    uint64_t ms = clock_get_uptime_milliseconds();
    event_queue[ev_head].time.tv_sec = (time_t)(ms / 1000);
    event_queue[ev_head].time.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    event_queue[ev_head].type = type;
    event_queue[ev_head].code = code;
    event_queue[ev_head].value = value;

    ev_head = next;
}

/* Returns number of events copied, 0 if none. Called from process context. */
size_t input_event_read(struct input_event *buf, size_t count)
{
    size_t n = 0;
    uint64_t irq_flags;

    if (!buf || count == 0)
        return 0;

    irq_flags = input_events_irq_save();
    while (n < count && ev_tail != ev_head)
    {
        buf[n] = event_queue[ev_tail];
        ev_tail = (ev_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
        n++;
    }
    input_events_irq_restore(irq_flags);
    return n;
}

int input_event_has_data(void)
{
    return ev_tail != ev_head;
}

size_t input_event_queue_depth(void)
{
    size_t head;
    size_t tail;
    uint64_t irq_flags;

    irq_flags = input_events_irq_save();
    head = ev_head;
    tail = ev_tail;
    input_events_irq_restore(irq_flags);

    if (head >= tail)
    {
        return head - tail;
    }
    return (INPUT_EVENT_QUEUE_SIZE - tail) + head;
}
