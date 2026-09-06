/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console_backend.h
 * Description: Console output facade — no drivers/ includes (impl in kernel/console_backend.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_CONSOLE_COLOR_DEFAULT 0x07u /* light grey on black */

void console_backend_init(void);
void console_backend_clear(uint8_t color);
int console_backend_uses_framebuffer(void);
int console_backend_fb_scale(void);
void console_backend_scroll(int lines);
void console_backend_write(const char *str, size_t len, uint8_t color);
void console_backend_show_cursor(uint8_t color);
int console_backend_cursor_x(void);
void console_backend_userspace_handoff(void);
void console_backend_set_tty_serial_mirror(int on);
int console_backend_printk_to_screen(void);
/* Re-enable kernel text output to the active console (panic path only). */
void console_backend_panic_screen_on(void);
void console_backend_typewriter_init(void);
