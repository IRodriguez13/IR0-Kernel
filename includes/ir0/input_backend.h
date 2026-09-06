/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_backend.h
 * Description: Input facade (mouse + keyboard) — no drivers/ in callers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IR0_KBD_LAYOUT_US    0
#define IR0_KBD_LAYOUT_LATAM 1

typedef struct ir0_mouse_state
{
	int32_t x;
	int32_t y;
	int8_t wheel;
	bool left_button;
	bool right_button;
	bool middle_button;
	bool button4;
	bool button5;
	bool has_wheel;
	bool has_5buttons;
	uint8_t resolution;
	uint8_t sample_rate;
	uint8_t type;
	bool initialized;
} ir0_mouse_state_t;

void input_mouse_handle_interrupt(void);
/* One AUX byte from i8042 (never fed to the keyboard decoder). */
void input_mouse_feed_byte(uint8_t data);
bool input_mouse_is_available(void);
bool input_mouse_get_state(ir0_mouse_state_t *out);
bool input_mouse_set_sensitivity(uint8_t sensitivity);

char input_kbd_get(void);
int input_kbd_has_data(void);
void input_kbd_clear(void);
/* Drop half-decoded scancode state (momentary mods + E0/E1); keep the ring. */
void input_kbd_resync_modifiers(void);
void input_kbd_poll_ps2(void);
int input_kbd_set_layout(int layout);
int input_kbd_get_layout(void);
const char *input_kbd_get_layout_name(int layout);
