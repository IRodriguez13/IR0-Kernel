/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE54B — Minimal fbdev + input interaction harness.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define EV_KEY 0x01
#define KEY_W 17
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_UP 103
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_DOWN 108

struct fb_var_screeninfo
{
	uint32_t xres;
	uint32_t yres;
	uint32_t xres_virtual;
	uint32_t yres_virtual;
	uint32_t xoffset;
	uint32_t yoffset;
	uint32_t bits_per_pixel;
	uint32_t grayscale;
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t transp;
	uint32_t nonstd;
	uint32_t activate;
	uint32_t height;
	uint32_t width;
	uint32_t accel_flags;
	uint32_t pixclock;
	uint32_t left_margin;
	uint32_t right_margin;
	uint32_t upper_margin;
	uint32_t lower_margin;
	uint32_t hsync_len;
	uint32_t vsync_len;
	uint32_t sync;
	uint32_t vmode;
	uint32_t rotate;
	uint32_t colorspace;
};

struct fb_fix_screeninfo
{
	char id[16];
	unsigned long smem_start;
	uint32_t smem_len;
	uint32_t type;
	uint32_t type_aux;
	uint32_t visual;
	uint16_t xpanstep;
	uint16_t ypanstep;
	uint16_t ywrapstep;
	uint32_t line_length;
	unsigned long mmio_start;
	uint32_t mmio_len;
	uint32_t accel;
	uint16_t capabilities;
	uint16_t reserved[2];
};

struct input_event
{
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void fase54b_fail(const char *step, const char *reason)
{
	write_str("[KTM_INPUT][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("KTM_INPUT_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static void draw_block(uint8_t *fb, const struct fb_var_screeninfo *var,
		       const struct fb_fix_screeninfo *fix,
		       uint32_t x, uint32_t y, uint32_t color)
{
	uint32_t bpp;
	uint32_t row;
	uint32_t col;

	if (!fb || !var || !fix)
		return;

	bpp = var->bits_per_pixel / 8;
	if (bpp == 0 || bpp > 4)
		return;

	for (row = 0; row < 16; row++)
	{
		uint32_t py = y + row;
		if (py >= var->yres)
			break;
		for (col = 0; col < 16; col++)
		{
			uint32_t px = x + col;
			uint8_t *dst;
			if (px >= var->xres)
				break;
			dst = fb + ((size_t)py * fix->line_length) + ((size_t)px * bpp);
			memcpy(dst, &color, bpp);
		}
	}
}

static int run_slice(void)
{
	struct stat st;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct input_event ev;
	uint8_t *fb_map;
	size_t map_len;
	int fd_fb;
	int fd_in;
	int i;
	int got_event;
	uint32_t pos_x;
	uint32_t pos_y;

	if (stat("/dev/fb0", &st) != 0)
	{
		fase54b_fail("stat_fb0", "stat");
		return -1;
	}
	fd_fb = open("/dev/fb0", O_RDWR);
	if (fd_fb < 0)
	{
		fase54b_fail("open_fb0", "open");
		return -1;
	}

	memset(&var, 0, sizeof(var));
	memset(&fix, 0, sizeof(fix));
	if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &var) != 0 ||
	    ioctl(fd_fb, FBIOGET_FSCREENINFO, &fix) != 0)
	{
		close(fd_fb);
		fase54b_fail("fb_getinfo", "ioctl");
		return -1;
	}

	map_len = (size_t)fix.smem_len;
	if (map_len < 4096)
		map_len = 4096;
	fb_map = (uint8_t *)mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
	if (fb_map == MAP_FAILED)
	{
		close(fd_fb);
		fase54b_fail("fb_mmap", "mmap");
		return -1;
	}

	draw_block(fb_map, &var, &fix, 8, 8, 0x00FF00u);

	if (stat("/dev/events0", &st) != 0)
	{
		(void)munmap(fb_map, map_len);
		close(fd_fb);
		fase54b_fail("stat_events0", "stat");
		return -1;
	}
	write_str("INPUT_FACADE_OK\n");
	write_str("DEVFS_INPUT0_OK\n");

	fd_in = open("/dev/events0", O_RDONLY | O_NONBLOCK);
	if (fd_in < 0)
	{
		write_str("KTM_INPUT_INPUT_UNAVAILABLE\n");
		(void)munmap(fb_map, map_len);
		close(fd_fb);
		return 0;
	}

	got_event = 0;
	pos_x = 24;
	pos_y = 24;
	for (i = 0; i < 300; i++)
	{
		ssize_t n = read(fd_in, &ev, sizeof(ev));
		if (n == (ssize_t)sizeof(ev) && ev.type == EV_KEY)
		{
			got_event = 1;
			if (ev.value == 1)
			{
				if ((ev.code == KEY_A || ev.code == KEY_LEFT) && pos_x >= 8)
					pos_x -= 8;
				else if (ev.code == KEY_D || ev.code == KEY_RIGHT)
					pos_x += 8;
				else if ((ev.code == KEY_W || ev.code == KEY_UP) && pos_y >= 8)
					pos_y -= 8;
				else if (ev.code == KEY_S || ev.code == KEY_DOWN)
					pos_y += 8;
				draw_block(fb_map, &var, &fix, pos_x, pos_y, 0x0000FFu);
			}
			write_str("INPUT_EVENT_READ_OK\n");
			break;
		}
		usleep(10000);
	}

	if (got_event)
		write_str("KTM_INPUT_FB_INTERACTIVE_OK\n");
	else
		write_str("KTM_INPUT_INPUT_UNAVAILABLE\n");

	close(fd_in);
	(void)munmap(fb_map, map_len);
	close(fd_fb);
	return 0;
}

int main(void)
{
	write_str("KTM_INPUT_START\n");
	write_str("KTM_INPUT_HARNESS_ID=ktm_input_smoke.c\n");

	if (run_slice() != 0)
		goto halt;

	write_str("KTM_INPUT_OK\n");

halt:
	for (;;)
		(void)pause();
	return 0;
}
