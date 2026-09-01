/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_mouse.h
 * Description: PS/2 mouse driver facade for input backend
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	PS2_MOUSE_TYPE_STANDARD = 0x00,
	PS2_MOUSE_TYPE_WHEEL = 0x03,
	PS2_MOUSE_TYPE_5BUTTON = 0x04
} ps2_mouse_type_t;

typedef struct
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
	ps2_mouse_type_t type;
	bool initialized;
} ps2_mouse_state_t;

bool ps2_mouse_init(void);
void ps2_mouse_shutdown(void);
bool ps2_mouse_is_available(void);
ps2_mouse_state_t *ps2_mouse_get_state(void);
void ps2_mouse_handle_interrupt(void);
void ps2_mouse_feed_byte(uint8_t data);
bool ps2_mouse_set_sample_rate(uint8_t rate);
