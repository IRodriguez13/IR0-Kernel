/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: video_console.h
 * Description: VGA/framebuffer text console driver facade
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#define CONSOLE_WIDTH  80
#define CONSOLE_HEIGHT 25

/* Product FB uses native Terminus 14×28 — no nearest-neighbor upscale. */
#define CONSOLE_FB_SCALE_DEFAULT 1
#define CONSOLE_FB_SCALE_MAX     1
#define CONSOLE_FB_BORDER_COLOR  0x00u

struct console_geometry
{
	unsigned pixel_width;
	unsigned pixel_height;
	unsigned cell_width;
	unsigned cell_height;
	unsigned columns;
	unsigned rows;
	unsigned scale;
};

void console_put_cell(int row, int col, char c, uint8_t color);
void console_scroll_up(uint8_t clear_color);
void console_clear(uint8_t color);
void console_init(void);
int console_use_framebuffer(void);
int console_get_width(void);
int console_get_height(void);
int console_get_fb_scale(void);
void console_get_geometry(struct console_geometry *geo);
