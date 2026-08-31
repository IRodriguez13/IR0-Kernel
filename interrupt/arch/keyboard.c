/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.c
 * Description: PS/2 IRQ path; set-1 decode via <ir0/ps2_set1.h>; TTY ring.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include "keyboard.h"
#include <ir0/arch_cpu.h>

#define PS2_DATA_PORT 0x60
#include <config.h>
#include <ir0/errno.h>
#include <ir0/vga.h>
#include <ir0/input.h>
#include <ir0/input_backend.h>
#include <ir0/ktm/klog.h>
#include <ir0/console.h>
#include <ir0/ps2_set1.h>

#define PS2_STATUS_PORT        0x64
#define PS2_STATUS_OUTPUT_FULL 0x01
/* i8042: output buffer byte is from AUX (mouse) when set — Linux I8042_STR_AUXDATA */
#define PS2_STATUS_AUXDATA     0x20

void wakeup_from_idle(void);
void stdin_wake_check(void);
static void keyboard_buffer_add(char c);
static void keyboard_buffer_add_bytes(const uint8_t *data, uint8_t len);
static void keyboard_feed_scancode(uint8_t scancode);

#define KERNEL_KBD_RING_SIZE 256
static char keyboard_buffer[KERNEL_KBD_RING_SIZE];
static int keyboard_buffer_head = 0;
static int keyboard_buffer_tail = 0;

volatile char *shared_keyboard_buffer = (volatile char *)KEYBOARD_BUFFER_ADDR;
volatile int *shared_keyboard_buffer_pos =
    (volatile int *)(KEYBOARD_BUFFER_ADDR + KEYBOARD_BUFFER_SIZE);

static int system_in_idle_mode = 0;
static int wake_requested = 0;

static struct ps2_set1_state kbd_state;

/* PS/2 scancode set 1 -> Linux KEY_* (for /dev/events0, Doom) */
static const uint16_t scancode_to_keycode[256] = {
    [0x01] = KEY_ESC, [0x02] = KEY_1, [0x03] = KEY_2, [0x04] = KEY_3,
    [0x05] = KEY_4, [0x06] = KEY_5, [0x07] = KEY_6, [0x08] = KEY_7,
    [0x09] = KEY_8, [0x0A] = KEY_9, [0x0B] = KEY_0, [0x0C] = KEY_MINUS,
    [0x0D] = KEY_EQUAL, [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R,
    [0x14] = KEY_T, [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I,
    [0x18] = KEY_O, [0x19] = KEY_P, [0x1A] = KEY_LEFTBRACE, [0x1B] = KEY_RIGHTBRACE,
    [0x1C] = KEY_ENTER, [0x1D] = KEY_LEFTCTRL, [0x1E] = KEY_A, [0x1F] = KEY_S,
    [0x20] = KEY_D, [0x21] = KEY_F, [0x22] = KEY_G, [0x23] = KEY_H,
    [0x24] = KEY_J, [0x25] = KEY_K, [0x26] = KEY_L, [0x27] = KEY_SEMICOLON,
    [0x28] = KEY_APOSTROPHE, [0x29] = KEY_GRAVE, [0x2A] = KEY_LEFTSHIFT,
    [0x2B] = KEY_BACKSLASH, [0x2C] = KEY_Z, [0x2D] = KEY_X, [0x2E] = KEY_C,
    [0x2F] = KEY_V, [0x30] = KEY_B, [0x31] = KEY_N, [0x32] = KEY_M,
    [0x33] = KEY_COMMA, [0x34] = KEY_DOT, [0x35] = KEY_SLASH, [0x36] = KEY_RIGHTSHIFT,
    [0x37] = KEY_KPASTERISK, [0x38] = KEY_LEFTALT, [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPSLOCK, [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3,
    [0x3E] = KEY_F4, [0x3F] = KEY_F5, [0x40] = KEY_F6, [0x41] = KEY_F7,
    [0x42] = KEY_F8, [0x43] = KEY_F9, [0x44] = KEY_F10, [0x45] = KEY_NUMLOCK,
    [0x46] = KEY_SCROLLLOCK, [0x47] = KEY_KP7, [0x48] = KEY_KP8, [0x49] = KEY_KP9,
    [0x4A] = KEY_KPMINUS, [0x4B] = KEY_KP4, [0x4C] = KEY_KP5, [0x4D] = KEY_KP6,
    [0x4E] = KEY_KPPLUS, [0x4F] = KEY_KP1, [0x50] = KEY_KP2, [0x51] = KEY_KP3,
    [0x52] = KEY_KP0, [0x53] = KEY_KPDOT, [0x57] = KEY_F11, [0x58] = KEY_F12,
};

static const uint16_t ext_scancode_to_keycode[256] = {
    [0x1C] = KEY_KPENTER, [0x1D] = KEY_RIGHTCTRL, [0x35] = KEY_KPSLASH,
    [0x37] = KEY_SYSRQ, [0x38] = KEY_RIGHTALT, [0x47] = KEY_HOME,
    [0x48] = KEY_UP, [0x49] = KEY_PAGEUP, [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT, [0x4F] = KEY_END, [0x50] = KEY_DOWN,
    [0x51] = KEY_PAGEDOWN, [0x52] = KEY_INSERT, [0x53] = KEY_DELETE,
    [0x5B] = KEY_LEFTMETA, [0x5C] = KEY_RIGHTMETA,
};

#if DEBUG_KEYBOARD
static void kbd_dbg_trace(const struct ps2_set1_result *r)
{
	if (!r)
		return;

	if (r->raw == 0xE0 || r->raw == 0xE1 || r->prefix == 0xE1)
	{
		kprintf("[INPUT] raw_scancode=0x%02x prefix=0x%02x event=prefix\n",
			r->raw, r->prefix);
		return;
	}

	if (r->emitted_len == 1)
	{
		kprintf("[INPUT] raw_scancode=0x%02x prefix=0x%02x event=%s key=%s emitted=0x%02x\n",
			r->raw, r->prefix, r->down ? "down" : "up",
			ps2_set1_key_name(r->key), r->emitted[0]);
	}
	else
	{
		kprintf("[INPUT] raw_scancode=0x%02x prefix=0x%02x event=%s key=%s emitted_len=%u\n",
			r->raw, r->prefix, r->down ? "down" : "up",
			ps2_set1_key_name(r->key), (unsigned)r->emitted_len);
	}

	if (r->mods_before != r->mods_after)
	{
		kprintf("[INPUT] modifiers before=0x%x after=0x%x\n",
			(unsigned)r->mods_before, (unsigned)r->mods_after);
	}
}
#else
static void kbd_dbg_trace(const struct ps2_set1_result *r)
{
	(void)r;
}
#endif

int keyboard_set_layout(int layout)
{
	if (layout != KEYBOARD_LAYOUT_US && layout != KEYBOARD_LAYOUT_LATAM)
		return -EINVAL;
	kbd_state.layout = layout;
	return 0;
}

int keyboard_get_layout(void)
{
	return kbd_state.layout;
}

const char *keyboard_get_layout_name(int layout)
{
	if (layout == KEYBOARD_LAYOUT_LATAM)
		return "latam";
	return "us";
}

uint32_t keyboard_modifiers_mask(void)
{
	return ps2_set1_mods_mask(&kbd_state.mods);
}

int keyboard_ctrl_active(void)
{
	return ps2_set1_ctrl(&kbd_state.mods);
}

void keyboard_all_keys_up(void)
{
	ps2_set1_all_keys_up(&kbd_state);
}

static void keyboard_buffer_add(char c)
{
	int next = (keyboard_buffer_head + 1) % KERNEL_KBD_RING_SIZE;
	static int kbd_ascii_tag;

	ir0_console_keypress(c);

	if (!ir0_console_store_key_in_ring())
		return;

	if (next != keyboard_buffer_tail)
	{
		keyboard_buffer[keyboard_buffer_head] = c;
		keyboard_buffer_head = next;
		if (!kbd_ascii_tag && (unsigned char)c >= ' ')
		{
			kbd_ascii_tag = 1;
			klog_smoke("KBD_ASCII_OK");
		}
	}
	else
	{
		/* Ring overflow: resync momentary modifiers (documented). */
		keyboard_all_keys_up();
	}
}

static void keyboard_buffer_add_bytes(const uint8_t *data, uint8_t len)
{
	uint8_t i;

	if (!data || !len)
		return;
	for (i = 0; i < len; i++)
		keyboard_buffer_add((char)data[i]);
}

#ifdef __x86_64__

char keyboard_buffer_get(void)
{
	if (keyboard_buffer_head == keyboard_buffer_tail)
		return 0;

	char c = keyboard_buffer[keyboard_buffer_tail];
	keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KERNEL_KBD_RING_SIZE;
	return c;
}

int keyboard_buffer_has_data(void)
{
	return keyboard_buffer_head != keyboard_buffer_tail;
}

void keyboard_buffer_clear(void)
{
	keyboard_buffer_head = 0;
	keyboard_buffer_tail = 0;
}
#endif

void keyboard_poll_ps2(void)
{
	static int kbd_poll_tag;

	for (;;)
	{
		uint8_t status;
		uint8_t data;
		unsigned long flags;
		int claimed = 0;

		/*
		 * Claim the byte atomically. kernel_idle_poll() drains the
		 * controller with interrupts enabled, so IRQ1 can land between
		 * the status test and the data read; the byte is consumed by
		 * the handler and this read returns the i8042's last byte
		 * again, duplicating a keystroke ("uptimee", "/proc//uptime").
		 * Linux serialises the same pair under i8042_lock.
		 */
		flags = irq_save();
		status = inb(PS2_STATUS_PORT);
		if (status & PS2_STATUS_OUTPUT_FULL)
		{
			/*
			 * Classify BEFORE reading would be ideal; i8042 latches
			 * AUXDATA with the byte, so read status then data and
			 * branch on AUXDATA.
			 */
			data = inb(PS2_DATA_PORT);
			claimed = 1;
		}
		else
		{
			data = 0;
		}
		irq_restore(flags);

		if (!claimed)
			break;

#if DEBUG_PS2
		kprintf("[PS2] status=0x%02x data=0x%02x source=%s\n", status, data,
			(status & PS2_STATUS_AUXDATA) ? "mouse" : "keyboard");
#endif

		if (status & PS2_STATUS_AUXDATA)
		{
			input_mouse_feed_byte(data);
			continue;
		}

		if (!kbd_poll_tag)
		{
			kbd_poll_tag = 1;
			klog_smoke("KBD_POLL_OK");
		}
		if (ir0_console_in_userspace())
		{
			static int kbd_user_poll_once;

			if (!kbd_user_poll_once)
			{
				kbd_user_poll_once = 1;
				klog_smoke("KBD_USER_POLL_OK");
			}
		}
		keyboard_feed_scancode(data);
	}
}

void keyboard_handler64(void)
{
	static int kbd_irq_tag;

	if (!kbd_irq_tag)
	{
		kbd_irq_tag = 1;
		klog_smoke("KBD_IRQ_OK");
	}

	keyboard_poll_ps2();
	stdin_wake_check();
}

static void keyboard_feed_scancode(uint8_t scancode)
{
	struct ps2_set1_result r;
	uint8_t code;
	int down;
	int extended;
	uint16_t kc;

	ps2_set1_feed(&kbd_state, scancode, &r);
	kbd_dbg_trace(&r);

	/* Prefix-only: no EV_KEY yet. */
	if (scancode == 0xE0 || scancode == 0xE1 || r.prefix == 0xE1)
		return;

	extended = (r.prefix == 0xE0) ? 1 : 0;
	down = r.down ? 1 : 0;
	code = (uint8_t)(scancode & 0x7F);

	kc = extended ? ext_scancode_to_keycode[code] : scancode_to_keycode[code];
	if (kc)
	{
		input_event_push(EV_KEY, kc, down);
		input_event_push(EV_SYN, SYN_REPORT, 0);
	}

	if (r.emitted_len)
	{
		static int kbd_scancode_tag;

		if (!kbd_scancode_tag)
		{
			kbd_scancode_tag = 1;
			klog_smoke("KBD_SCANCODE_OK");
		}
		/*
		 * While /dev/events0 has readers (Doom, etc.), only EV_KEY —
		 * do not also inject ASCII into the cooked TTY line (qqqls).
		 */
		if (!input_events_readers_active())
		{
			keyboard_buffer_add_bytes(r.emitted, r.emitted_len);
		}
		else
		{
			/*
			 * A leaked /dev/events0 reader silently kills all
			 * console input, which looks like a dead getty. Say so
			 * on the serial log instead of dropping in silence.
			 */
			static unsigned int suppressed;

			if (++suppressed <= 8)
				klog_notice_fmt("INPUT",
						"[INPUT] ascii suppressed: events0 readers active (n=%u)\n",
						suppressed);
		}
	}
}

void keyboard_init(void)
{
	keyboard_buffer_head = 0;
	keyboard_buffer_tail = 0;
	ps2_set1_reset(&kbd_state);
	if (CONFIG_KEYBOARD_LAYOUT == KEYBOARD_LAYOUT_LATAM)
		kbd_state.layout = KEYBOARD_LAYOUT_LATAM;
	else
		kbd_state.layout = KEYBOARD_LAYOUT_US;
}

void set_idle_mode(int is_idle)
{
	system_in_idle_mode = is_idle;
	if (is_idle)
		wake_requested = 0;
}

int is_in_idle_mode(void)
{
	return system_in_idle_mode;
}

void wakeup_from_idle(void)
{
	if (system_in_idle_mode)
	{
		wake_requested = 1;
		system_in_idle_mode = 0;
	}
	else
	{
		print_colored("DEBUG: wakeup called but not in idle mode\n",
			      VGA_COLOR_RED, VGA_COLOR_BLACK);
	}
}

int is_wake_requested(void)
{
	return wake_requested;
}

void clear_wake_request(void)
{
	wake_requested = 0;
}
