/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 unified text console renderer — single cursor_pos for /dev/console,
 * TTY echo, and shell write. Classic 80x25 VT; FB cells scaled (FASE59B).
 */

#include "console_renderer.h"
#include "console.h"
#include <ir0/serial_io.h>
#include <ir0/vga.h>
#include <stdint.h>

#define CSI_NONE    0
#define CSI_ESC     1
#define CSI_BRACKET 2

static uint8_t render_color = CONSOLE_RENDERER_COLOR_DEFAULT;
static int render_cursor_visible;
static int render_cursor_row;
static int render_cursor_col;
static char render_cursor_under_ch = ' ';
static uint8_t render_cursor_under_color = CONSOLE_RENDERER_COLOR_DEFAULT;
static int csi_state;
static int csi_param;
static int csi_params[4];
static int csi_param_count;
static int csi_priv; /* ESC[? … private mode */
static int render_cursor_enabled = 1; /* DECTCEM: 0 = civis, 1 = cnorm */

static void csi_reset(void)
{
	csi_state = CSI_NONE;
	csi_param = 0;
	csi_param_count = 0;
	csi_priv = 0;
}

static void csi_clear_from_cursor(int cols, int rows, uint8_t color)
{
	extern int cursor_pos;
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	int r;
	int c;

	for (r = row; r < rows; r++)
	{
		int start = (r == row) ? col : 0;

		for (c = start; c < cols; c++)
			console_put_cell(r, c, ' ', color);
	}
}

/* CSI J mode 1 — erase from start of screen through cursor (inclusive). */
static void csi_clear_to_cursor(int cols, int rows, uint8_t color)
{
	extern int cursor_pos;
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	int r;
	int c;

	(void)rows;
	for (r = 0; r <= row; r++)
	{
		int end = (r == row) ? col : (cols - 1);

		for (c = 0; c <= end; c++)
			console_put_cell(r, c, ' ', color);
	}
}

static void csi_clear_screen(int cols, int rows, uint8_t color)
{
	int r;
	int c;

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
			console_put_cell(r, c, ' ', color);
}

/*
 * SGR state must survive across CSI sequences. BusyBox ls emits ESC[1;34m
 * (bold then blue); if bold is applied only as a one-shot brighten before
 * the fg is set, directories stay VGA blue-on-near-black and vanish. Nano uses
 * A_REVERSE (SGR 7) for title/status/selection — ignore that and the UI
 * looks broken regardless of FB scale.
 */
static uint8_t sgr_fg = CONSOLE_RENDERER_COLOR_DEFAULT & 0x0F;
static uint8_t sgr_bg = (CONSOLE_RENDERER_COLOR_DEFAULT >> 4) & 0x0F;
static int sgr_bold;
static int sgr_reverse;

static void sgr_compose(uint8_t *color)
{
	uint8_t fg = sgr_fg & 0x0F;
	uint8_t bg = sgr_bg & 0x0F;

	if (sgr_bold && fg < 8)
		fg = (uint8_t)(fg + 8);
	if (sgr_reverse)
	{
		uint8_t tmp = fg;

		fg = bg;
		bg = tmp;
	}
	*color = (uint8_t)((bg << 4) | fg);
}

static void sgr_reset_attrs(void)
{
	sgr_fg = CONSOLE_RENDERER_COLOR_DEFAULT & 0x0F;
	sgr_bg = (CONSOLE_RENDERER_COLOR_DEFAULT >> 4) & 0x0F;
	sgr_bold = 0;
	sgr_reverse = 0;
}

static void sgr_apply(int *params, int count, uint8_t *color)
{
	int i;

	if (count == 0)
	{
		sgr_reset_attrs();
		sgr_compose(color);
		return;
	}

	for (i = 0; i < count; i++)
	{
		int p = params[i];

		if (p == 0)
			sgr_reset_attrs();
		else if (p == 1)
			sgr_bold = 1;
		else if (p == 22)
			sgr_bold = 0;
		else if (p == 7)
			sgr_reverse = 1;
		else if (p == 27)
			sgr_reverse = 0;
		else if (p >= 30 && p <= 37)
			sgr_fg = (uint8_t)(p - 30);
		else if (p >= 90 && p <= 97)
			sgr_fg = (uint8_t)(p - 90 + 8);
		else if (p >= 40 && p <= 47)
			sgr_bg = (uint8_t)(p - 40);
		else if (p >= 100 && p <= 107)
			sgr_bg = (uint8_t)(p - 100 + 8);
		else if (p == 39)
			sgr_fg = CONSOLE_RENDERER_COLOR_DEFAULT & 0x0F;
		else if (p == 49)
			sgr_bg = (CONSOLE_RENDERER_COLOR_DEFAULT >> 4) & 0x0F;
	}
	sgr_compose(color);
}

static void csi_apply(char cmd, int cols, int rows, uint8_t *color)
{
	extern int cursor_pos;
	int n = 1;
	int row;
	int col;
	int i;

	if (csi_param_count > 0)
		n = csi_params[0];
	if (n <= 0)
		n = 1;

	switch (cmd)
	{
	case 'm':
		sgr_apply(csi_params, csi_param_count, color);
		break;
	case 'A':
		if (cursor_pos >= n * cols)
			cursor_pos -= n * cols;
		else
			cursor_pos = (cursor_pos / cols) * cols;
		break;
	case 'B':
		cursor_pos += n * cols;
		if (cursor_pos >= cols * rows)
			cursor_pos = (rows - 1) * cols + (cursor_pos % cols);
		break;
	case 'h':
	case 'l':
		/* ESC[?25h / ESC[?25l — show / hide cursor (DECTCEM). */
		if (csi_priv && n == 25)
			render_cursor_enabled = (cmd == 'h') ? 1 : 0;
		break;
	case 'J':
	{
		/* ECMA-48 / xterm: 0=from cursor, 1=to cursor, 2=entire screen. */
		int mode = csi_param_count > 0 ? csi_params[0] : 0;

		if (mode == 2 || mode == 3)
		{
			csi_clear_screen(cols, rows, *color);
			if (mode == 2)
				cursor_pos = 0;
		}
		else if (mode == 1)
			csi_clear_to_cursor(cols, rows, *color);
		else
			csi_clear_from_cursor(cols, rows, *color);
		break;
	}
	case 'D':
		if (cursor_pos >= n)
			cursor_pos -= n;
		else
			cursor_pos = (cursor_pos / cols) * cols;
		break;
	case 'C':
		cursor_pos += n;
		if (cursor_pos >= cols * rows)
			cursor_pos = cols * rows - 1;
		break;
	case 'K':
	{
		int mode = csi_param_count > 0 ? csi_params[0] : 0;

		row = cursor_pos / cols;
		if (mode == 2)
		{
			for (i = 0; i < cols; i++)
				console_put_cell(row, i, ' ', *color);
		}
		else if (mode == 1)
		{
			col = cursor_pos % cols;
			for (i = 0; i <= col; i++)
				console_put_cell(row, i, ' ', *color);
		}
		else
		{
			col = cursor_pos % cols;
			for (i = col; i < cols; i++)
				console_put_cell(row, i, ' ', *color);
		}
		break;
	}
	case 'G':
		col = (csi_param_count > 0 ? csi_params[0] : 1) - 1;
		if (col < 0)
			col = 0;
		if (col >= cols)
			col = cols - 1;
		cursor_pos = (cursor_pos / cols) * cols + col;
		break;
	case 'H':
	case 'f':
	{
		int crow = 1;
		int ccol = 1;

		if (csi_param_count >= 1)
			crow = csi_params[0];
		if (csi_param_count >= 2)
			ccol = csi_params[1];
		if (crow < 1)
			crow = 1;
		if (ccol < 1)
			ccol = 1;
		crow--;
		ccol--;
		if (crow >= rows)
			crow = rows - 1;
		if (ccol >= cols)
			ccol = cols - 1;
		cursor_pos = crow * cols + ccol;
		break;
	}
	default:
		break;
	}
}

static int csi_feed(char c, int cols, int rows, uint8_t *color)
{
	if (csi_state == CSI_ESC)
	{
		if (c == '[')
		{
			csi_state = CSI_BRACKET;
			csi_param = 0;
			csi_param_count = 0;
			return 1;
		}
		csi_reset();
		return 0;
	}

	if (csi_state == CSI_BRACKET)
	{
		if (c >= '0' && c <= '9')
		{
			csi_param = csi_param * 10 + (c - '0');
			return 1;
		}
		if (c == ';')
		{
			if (csi_param_count < 4)
				csi_params[csi_param_count++] = csi_param;
			csi_param = 0;
			return 1;
		}
		if (c == '?')
		{
			csi_priv = 1;
			return 1;
		}
		if (c >= 0x40 && c <= 0x7e)
		{
			if (csi_param_count < 4)
				csi_params[csi_param_count++] = csi_param;
			csi_apply(c, cols, rows, color);
			csi_reset();
			return 1;
		}
		csi_reset();
		return 0;
	}

	if ((unsigned char)c == 0x1b)
	{
		csi_state = CSI_ESC;
		return 1;
	}

	return 0;
}

static int render_cols(void)
{
	if (console_use_framebuffer())
		return console_get_width();
	return CONSOLE_WIDTH;
}

static int render_rows(void)
{
	if (console_use_framebuffer())
		return console_get_height();
	return CONSOLE_HEIGHT;
}

static void render_erase_cursor(int cols, int rows, uint8_t color)
{
	(void)color;
	if (!render_cursor_visible)
		return;
	if (render_cursor_row < 0 || render_cursor_row >= rows)
		return;
	if (render_cursor_col < 0 || render_cursor_col >= cols)
		return;
	/* Restore glyph wiped by the block cursor (ONLCR: CR paints col 0). */
	console_put_cell(render_cursor_row, render_cursor_col,
			 render_cursor_under_ch, render_cursor_under_color);
	render_cursor_visible = 0;
}

void console_renderer_reset(uint8_t color)
{
	extern int cursor_pos;

	csi_reset();
	sgr_reset_attrs();
	/* Caller may pass an explicit clear color; keep SGR defaults unless
	 * it matches the composed default (typical product path). */
	render_color = color;
	if (color == CONSOLE_RENDERER_COLOR_DEFAULT)
		sgr_compose(&render_color);
	render_cursor_visible = 0;
	render_cursor_row = 0;
	render_cursor_col = 0;
	cursor_pos = 0;
	console_clear(color);
}

void console_renderer_putchar(char c, uint8_t color)
{
	extern int cursor_pos;
	int cols = render_cols();
	int rows = render_rows();
	int row;
	int col;
	uint8_t draw;

	(void)color;
	draw = render_color;

	/* Guard against soft geometry / early boot (never divide by zero). */
	if (cols <= 0)
		cols = CONSOLE_WIDTH;
	if (rows <= 0)
		rows = CONSOLE_HEIGHT;

	if (csi_feed(c, cols, rows, &render_color))
	{
		draw = render_color;
		/*
		 * Only refresh the hardware cursor when a full CSI sequence
		 * completes (or civis). Intermediate ESC/[ / digits must not
		 * paint — nano floods DECTCEM around every glyph.
		 */
		if (csi_state == CSI_NONE)
		{
			if (render_cursor_enabled)
				console_renderer_show_cursor(draw);
			else
				render_erase_cursor(cols, rows, draw);
		}
		return;
	}

	render_erase_cursor(cols, rows, draw);

	if (c == '\n')
	{
		csi_reset();
		cursor_pos = (cursor_pos / cols + 1) * cols;
		if (cursor_pos >= cols * rows)
		{
			console_scroll_up(draw);
			cursor_pos = (rows - 1) * cols;
		}
	}
	else if (c == '\r')
	{
		csi_reset();
		cursor_pos = (cursor_pos / cols) * cols;
	}
	else if (c == '\b' || c == 127)
	{
		if (cursor_pos > 0)
		{
			cursor_pos--;
			row = cursor_pos / cols;
			col = cursor_pos % cols;
			console_put_cell(row, col, ' ', draw);
		}
	}
	else if (c == '\t')
	{
		col = cursor_pos % cols;
		{
			int next = (col + 8) & ~7;

			if (next >= cols)
			{
				cursor_pos = (cursor_pos / cols + 1) * cols;
				if (cursor_pos >= cols * rows)
				{
					console_scroll_up(draw);
					cursor_pos = (rows - 1) * cols;
				}
			}
			else
			{
				cursor_pos = (cursor_pos / cols) * cols + next;
			}
		}
	}
	else if ((unsigned char)c >= ' ')
	{
		row = cursor_pos / cols;
		col = cursor_pos % cols;
		console_put_cell(row, col, c, draw);
		cursor_pos++;
		if (cursor_pos >= cols * rows)
		{
			console_scroll_up(draw);
			cursor_pos = (rows - 1) * cols;
		}
	}

	if (render_cursor_enabled)
		console_renderer_show_cursor(draw);
	else
		render_erase_cursor(cols, rows, draw);
}

void console_renderer_show_cursor(uint8_t color)
{
	extern int cursor_pos;
	int cols = render_cols();
	int rows = render_rows();
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	uint8_t cur_color;
	uint16_t cell;

	(void)color;
	if (!render_cursor_enabled)
	{
		render_erase_cursor(cols, rows, color);
		return;
	}
	render_erase_cursor(cols, rows, color);

	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return;

	cell = console_get_cell(row, col);
	render_cursor_under_ch = (char)(cell & 0xFF);
	render_cursor_under_color = (uint8_t)(cell >> 8);
	/* Invert fg/bg; draw without clobbering the shadow under-glyph. */
	cur_color = (uint8_t)(((render_cursor_under_color & 0xF0) >> 4) |
			      ((render_cursor_under_color & 0x0F) << 4));
	console_draw_cell(row, col, render_cursor_under_ch, cur_color);
	render_cursor_visible = 1;
	render_cursor_row = row;
	render_cursor_col = col;
}

int console_renderer_get_cursor_x(void)
{
	extern int cursor_pos;
	int cols = render_cols();

	if (cols <= 0)
		return 0;
	return cursor_pos % cols;
}

int console_renderer_get_cursor_y(void)
{
	extern int cursor_pos;
	int cols = render_cols();

	if (cols <= 0)
		return 0;
	return cursor_pos / cols;
}

void console_renderer_import_cursor_pos(int cols)
{
	extern int cursor_pos;

	if (cols <= 0)
		return;
	if (cursor_pos < 0)
		cursor_pos = 0;
}
