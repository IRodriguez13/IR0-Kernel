/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_mouse_pkt.c
 * Description: Portable PS/2 mouse packet assembler (resync + 3/4-byte).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ps2_mouse_pkt.h>

void ps2_mouse_pkt_reset(struct ps2_mouse_pkt_state *st, uint8_t expected_bytes)
{
	if (!st)
		return;
	st->index = 0;
	st->expected = (expected_bytes == 4) ? 4 : 3;
	st->buf[0] = st->buf[1] = st->buf[2] = st->buf[3] = 0;
}

void ps2_mouse_pkt_feed(struct ps2_mouse_pkt_state *st, uint8_t data,
			struct ps2_mouse_pkt *out)
{
	if (!st || !out)
		return;

	out->complete = 0;
	out->flags = 0;
	out->dx = 0;
	out->dy = 0;
	out->wheel = 0;
	out->extra_buttons = 0;

	/*
	 * Resync on byte 0: every valid PS/2 mouse packet has bit 3 set.
	 * Drop leading garbage until a header appears.
	 */
	if (st->index == 0 && !(data & PS2_MOUSE_PKT_ALWAYS_1))
		return;

	st->buf[st->index++] = data;
	if (st->index < st->expected)
		return;

	out->flags = st->buf[0];
	out->dx = (int16_t)st->buf[1];
	out->dy = (int16_t)st->buf[2];
	if (out->flags & 0x10)
		out->dx |= (int16_t)0xFF00;
	if (out->flags & 0x20)
		out->dy |= (int16_t)0xFF00;

	if (st->expected == 4)
	{
		out->wheel = (int8_t)(st->buf[3] & 0x0F);
		if (out->wheel & 0x08)
			out->wheel |= (int8_t)0xF0;
		out->extra_buttons = (uint8_t)((st->buf[3] >> 4) & 0x03);
	}

	out->complete = 1;
	st->index = 0;
}
