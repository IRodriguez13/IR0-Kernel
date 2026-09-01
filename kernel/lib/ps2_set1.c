/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_set1.c
 * Description: PS/2 set 1 make/break decoder; L/R modifiers; ASCII/CSI emit.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ps2_set1.h>

#ifndef KEYBOARD_LAYOUT_US
#define KEYBOARD_LAYOUT_US 0
#endif
#ifndef KEYBOARD_LAYOUT_LATAM
#define KEYBOARD_LAYOUT_LATAM 1
#endif

static const char ascii_us[] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

static const char ascii_shift_us[] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

/* LATAM: same printable ASCII subset as keyboard.c (no dead keys). */
static const char ascii_latam[] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\'', 0, 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, '+', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', 0, '{', '|', 0, '}', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '-', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

static const char ascii_shift_latam[] = {
	0, 0, '!', '"', '#', '$', '%', '&', '/', '(', ')', '=', '?', 0, 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0, '*', 0, 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', 0, '[', 0, 0, ']', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', ';', ':', '_', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

void ps2_set1_reset(struct ps2_set1_state *st)
{
	if (!st)
		return;
	st->mods.left_ctrl = 0;
	st->mods.right_ctrl = 0;
	st->mods.left_shift = 0;
	st->mods.right_shift = 0;
	st->mods.left_alt = 0;
	st->mods.right_alt = 0;
	st->mods.caps_lock = 0;
	st->mods.num_lock = 0;
	st->mods.scroll_lock = 0;
	st->e0_prefix = 0;
	st->e1_skip = 0;
	st->layout = KEYBOARD_LAYOUT_US;
}

void ps2_set1_all_keys_up(struct ps2_set1_state *st)
{
	if (!st)
		return;
	/*
	 * Resync only — device reinit, console switch, queue overflow,
	 * or controller error. Does not replace correct break-code handling.
	 */
	st->mods.left_ctrl = 0;
	st->mods.right_ctrl = 0;
	st->mods.left_shift = 0;
	st->mods.right_shift = 0;
	st->mods.left_alt = 0;
	st->mods.right_alt = 0;
	/* Toggle locks intentionally preserved across all_keys_up. */
	st->e0_prefix = 0;
	st->e1_skip = 0;
}

int ps2_set1_ctrl(const struct keyboard_modifiers *m)
{
	return m && (m->left_ctrl || m->right_ctrl);
}

int ps2_set1_shift(const struct keyboard_modifiers *m)
{
	return m && (m->left_shift || m->right_shift);
}

int ps2_set1_alt(const struct keyboard_modifiers *m)
{
	return m && (m->left_alt || m->right_alt);
}

uint32_t ps2_set1_mods_mask(const struct keyboard_modifiers *m)
{
	uint32_t mask = KBD_MOD_NONE;

	if (!m)
		return KBD_MOD_NONE;
	if (m->left_ctrl)
		mask |= KBD_MOD_LEFT_CTRL;
	if (m->right_ctrl)
		mask |= KBD_MOD_RIGHT_CTRL;
	if (m->left_shift)
		mask |= KBD_MOD_LEFT_SHIFT;
	if (m->right_shift)
		mask |= KBD_MOD_RIGHT_SHIFT;
	if (m->left_alt)
		mask |= KBD_MOD_LEFT_ALT;
	if (m->right_alt)
		mask |= KBD_MOD_RIGHT_ALT;
	if (m->caps_lock)
		mask |= KBD_MOD_CAPS_LOCK;
	if (m->num_lock)
		mask |= KBD_MOD_NUM_LOCK;
	if (m->scroll_lock)
		mask |= KBD_MOD_SCROLL_LOCK;
	return mask;
}

const char *ps2_set1_key_name(uint8_t key)
{
	switch (key)
	{
	case PS2_KEY_LEFT_CTRL:
		return "LEFT_CTRL";
	case PS2_KEY_RIGHT_CTRL:
		return "RIGHT_CTRL";
	case PS2_KEY_LEFT_SHIFT:
		return "LEFT_SHIFT";
	case PS2_KEY_RIGHT_SHIFT:
		return "RIGHT_SHIFT";
	case PS2_KEY_LEFT_ALT:
		return "LEFT_ALT";
	case PS2_KEY_RIGHT_ALT:
		return "RIGHT_ALT";
	case PS2_KEY_CAPS_LOCK:
		return "CAPS_LOCK";
	case PS2_KEY_NUM_LOCK:
		return "NUM_LOCK";
	case PS2_KEY_SCROLL_LOCK:
		return "SCROLL_LOCK";
	case PS2_KEY_OTHER:
		return "OTHER";
	default:
		return "NONE";
	}
}

static void emit_byte(struct ps2_set1_result *out, uint8_t b)
{
	if (!out || out->emitted_len >= sizeof(out->emitted))
		return;
	out->emitted[out->emitted_len++] = b;
}

static void emit_str(struct ps2_set1_result *out, const char *s)
{
	if (!s)
		return;
	while (*s)
		emit_byte(out, (uint8_t)*s++);
}

static char ctrl_ascii(char ch)
{
	if (ch >= 'a' && ch <= 'z')
		ch = (char)(ch - 'a' + 'A');
	if ((ch >= '@' && ch <= '_') || ch == ' ')
		return (char)(ch & 0x1f);
	return ch;
}

static int apply_momentary(struct keyboard_modifiers *m, uint8_t code,
			   int extended, int down, uint8_t *key_out)
{
	*key_out = PS2_KEY_OTHER;

	if (extended)
	{
		if (code == 0x1D)
		{
			m->right_ctrl = down ? 1 : 0;
			*key_out = PS2_KEY_RIGHT_CTRL;
			return 1;
		}
		if (code == 0x38)
		{
			m->right_alt = down ? 1 : 0;
			*key_out = PS2_KEY_RIGHT_ALT;
			return 1;
		}
		return 0;
	}

	switch (code)
	{
	case 0x1D:
		m->left_ctrl = down ? 1 : 0;
		*key_out = PS2_KEY_LEFT_CTRL;
		return 1;
	case 0x2A:
		m->left_shift = down ? 1 : 0;
		*key_out = PS2_KEY_LEFT_SHIFT;
		return 1;
	case 0x36:
		m->right_shift = down ? 1 : 0;
		*key_out = PS2_KEY_RIGHT_SHIFT;
		return 1;
	case 0x38:
		m->left_alt = down ? 1 : 0;
		*key_out = PS2_KEY_LEFT_ALT;
		return 1;
	default:
		return 0;
	}
}

static int apply_toggle(struct keyboard_modifiers *m, uint8_t code, int down,
			uint8_t *key_out)
{
	if (!down)
		return 0;

	switch (code)
	{
	case 0x3A:
		m->caps_lock = m->caps_lock ? 0 : 1;
		*key_out = PS2_KEY_CAPS_LOCK;
		return 1;
	case 0x45:
		m->num_lock = m->num_lock ? 0 : 1;
		*key_out = PS2_KEY_NUM_LOCK;
		return 1;
	case 0x46:
		m->scroll_lock = m->scroll_lock ? 0 : 1;
		*key_out = PS2_KEY_SCROLL_LOCK;
		return 1;
	default:
		return 0;
	}
}

static void emit_extended_csi(struct ps2_set1_result *out, uint8_t code)
{
	switch (code)
	{
	case 0x48:
		emit_str(out, "\x1b[A");
		break;
	case 0x50:
		emit_str(out, "\x1b[B");
		break;
	case 0x4D:
		emit_str(out, "\x1b[C");
		break;
	case 0x4B:
		emit_str(out, "\x1b[D");
		break;
	case 0x47:
		emit_str(out, "\x1b[H");
		break;
	case 0x4F:
		emit_str(out, "\x1b[F");
		break;
	case 0x53:
		emit_str(out, "\x1b[3~");
		break;
	case 0x49:
		emit_str(out, "\x1b[5~");
		break;
	case 0x51:
		emit_str(out, "\x1b[6~");
		break;
	default:
		break;
	}
}

static void emit_ascii(struct ps2_set1_state *st, struct ps2_set1_result *out,
		       uint8_t code)
{
	const char *base;
	const char *shift_tbl;
	char ch;
	int shifted;

	/* ESC → ASCII ESC so TTY apps (BusyBox vi) can leave insert/command modes. */
	if (code == 0x01)
	{
		emit_byte(out, (uint8_t)0x1b);
		return;
	}
	if (code == 0x0E)
	{
		emit_byte(out, (uint8_t)'\b');
		return;
	}
	if (code == 0x0F)
	{
		emit_byte(out, (uint8_t)'\t');
		return;
	}
	if (code == 0x1C)
	{
		emit_byte(out, (uint8_t)'\r');
		return;
	}
	if (code == 0x39)
	{
		/* Ctrl+Space → NUL: do not enqueue (legacy keyboard.c). */
		if (!ps2_set1_ctrl(&st->mods))
			emit_byte(out, (uint8_t)' ');
		return;
	}
	/* Ctrl+L → form-feed for BusyBox lineedit clear-screen */
	if (code == 0x26 && ps2_set1_ctrl(&st->mods))
	{
		emit_byte(out, (uint8_t)'\f');
		return;
	}

	if (st->layout == KEYBOARD_LAYOUT_LATAM)
	{
		base = ascii_latam;
		shift_tbl = ascii_shift_latam;
	}
	else
	{
		base = ascii_us;
		shift_tbl = ascii_shift_us;
	}

	if (code >= sizeof(ascii_us))
		return;

	shifted = ps2_set1_shift(&st->mods);
	ch = shifted ? shift_tbl[code] : base[code];
	if (ch == 0)
		return;

	/* Caps Lock toggles case for a–z / A–Z only. */
	if (st->mods.caps_lock)
	{
		if (ch >= 'a' && ch <= 'z')
			ch = (char)(ch - 'a' + 'A');
		else if (ch >= 'A' && ch <= 'Z')
			ch = (char)(ch - 'A' + 'a');
	}

	if (ps2_set1_ctrl(&st->mods))
		ch = ctrl_ascii(ch);

	emit_byte(out, (uint8_t)ch);
}

void ps2_set1_feed(struct ps2_set1_state *st, uint8_t raw,
		   struct ps2_set1_result *out)
{
	uint8_t code;
	int down;
	int extended;
	uint8_t key = PS2_KEY_NONE;

	if (out)
	{
		out->raw = raw;
		out->prefix = 0;
		out->down = 0;
		out->key = PS2_KEY_NONE;
		out->mods_before = st ? ps2_set1_mods_mask(&st->mods) : 0;
		out->mods_after = out->mods_before;
		out->emitted_len = 0;
	}

	if (!st)
		return;

	/* Pause/Break: E1 1D 45 E1 9D C5 — discard remaining bytes. */
	if (st->e1_skip)
	{
		st->e1_skip--;
		if (out)
			out->prefix = 0xE1;
		return;
	}

	if (raw == 0xE1)
	{
		st->e1_skip = 5;
		st->e0_prefix = 0;
		if (out)
			out->prefix = 0xE1;
		return;
	}

	if (raw == 0xE0)
	{
		st->e0_prefix = 1;
		if (out)
			out->prefix = 0xE0;
		return;
	}

	extended = st->e0_prefix ? 1 : 0;
	st->e0_prefix = 0;
	down = (raw < 0x80) ? 1 : 0;
	code = (uint8_t)(raw & 0x7F);

	if (out)
	{
		out->prefix = extended ? 0xE0 : 0;
		out->down = (uint8_t)down;
		out->mods_before = ps2_set1_mods_mask(&st->mods);
	}

	if (apply_momentary(&st->mods, code, extended, down, &key))
	{
		if (out)
		{
			out->key = key;
			out->mods_after = ps2_set1_mods_mask(&st->mods);
		}
		return;
	}

	if (!extended && apply_toggle(&st->mods, code, down, &key))
	{
		if (out)
		{
			out->key = key;
			out->mods_after = ps2_set1_mods_mask(&st->mods);
		}
		return;
	}

	if (out)
		out->key = PS2_KEY_OTHER;

	/* Break of a normal key: update nothing else, no TTY emit. */
	if (!down)
	{
		if (out)
			out->mods_after = ps2_set1_mods_mask(&st->mods);
		return;
	}

	if (extended)
		emit_extended_csi(out, code);
	else
		emit_ascii(st, out, code);

	if (out)
		out->mods_after = ps2_set1_mods_mask(&st->mods);
}
