/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58C — Minimal /dev/fb0 draw probe (no Doom).
 * Paints RGB horizontal bands via mmap; tag DEVFB0_DRAW_OK.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

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

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void fase58c_fail(const char *step)
{
	write_str("[KTM_FBDEV_GUI][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str("\n");
}

static void fill_band(uint8_t *map, uint32_t line_length, uint32_t bpp,
                      uint32_t y0, uint32_t y1, uint32_t xres,
                      uint8_t b, uint8_t g, uint8_t r)
{
	uint32_t y;
	uint32_t x;
	uint32_t bytes_per_pixel = bpp / 8;

	if (bytes_per_pixel == 0 || bytes_per_pixel > 4)
		return;

	for (y = y0; y < y1; y++)
	{
		uint8_t *line = map + (size_t)y * line_length;

		for (x = 0; x < xres; x++)
		{
			uint8_t *px = line + (size_t)x * bytes_per_pixel;

			px[0] = b;
			if (bytes_per_pixel > 1)
				px[1] = g;
			if (bytes_per_pixel > 2)
				px[2] = r;
			if (bytes_per_pixel > 3)
				px[3] = 0;
		}
	}
}

int main(void)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	size_t map_len;
	uint8_t *map;
	struct stat st;
	uint32_t third;
	int fd;

	write_str("KTM_FBDEV_GUI_START\n");
	write_str("KTM_FBDEV_GUI_HARNESS_ID=ktm_fbdev_gui_smoke.c\n");

	if (stat("/dev/fb0", &st) != 0)
	{
		fase58c_fail("stat_fb0");
		goto halt;
	}

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0)
	{
		fase58c_fail("open_fb0");
		goto halt;
	}
	write_str("DEVFS_FB0_OK\n");

	memset(&var, 0, sizeof(var));
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0)
	{
		close(fd);
		fase58c_fail("ioctl_var");
		goto halt;
	}

	memset(&fix, 0, sizeof(fix));
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0)
	{
		close(fd);
		fase58c_fail("ioctl_fix");
		goto halt;
	}

	if (var.xres == 0 || var.yres == 0 || var.bits_per_pixel == 0 ||
	    fix.line_length == 0 || fix.smem_len == 0)
	{
		close(fd);
		fase58c_fail("getinfo");
		goto halt;
	}

	write_str("KTM_FBDEV_GUI_FB_GETINFO_OK\n");

	map_len = (size_t)fix.smem_len;
	if (map_len > (4U * 1024U * 1024U))
		map_len = 4U * 1024U * 1024U;

	map = (uint8_t *)mmap(NULL, map_len, PROT_READ | PROT_WRITE,
			      MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
	{
		close(fd);
		fase58c_fail("mmap");
		goto halt;
	}

	third = var.yres / 3;
	if (third == 0)
		third = 1;

	/* Cyan / magenta / yellow bands — distinct from kernel boot RGB. */
	fill_band(map, fix.line_length, var.bits_per_pixel, 0, third, var.xres,
		  255, 255, 0);
	fill_band(map, fix.line_length, var.bits_per_pixel, third, third * 2,
		  var.xres, 255, 0, 255);
	if (third * 2 < var.yres)
	{
		fill_band(map, fix.line_length, var.bits_per_pixel, third * 2,
			  var.yres, var.xres, 0, 255, 255);
	}

	(void)munmap(map, map_len);
	close(fd);

	write_str("DEVFB0_DRAW_OK\n");
	write_str("KTM_FBDEV_GUI_OK\n");

halt:
	for (;;)
		(void)pause();

	return 0;
}
