/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE54A — Minimal /dev/fb0 framebuffer harness.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR 2

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

static void fase54a_fail(const char *step, const char *reason)
{
	write_str("[KTM_FBDEV][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("KTM_FBDEV_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static int check_fbdev_slice(void)
{
	struct stat st;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	size_t map_len;
	unsigned char *map;
	int fd;

	if (stat("/dev/fb0", &st) != 0)
	{
		fase54a_fail("stat_fb0", "stat");
		return -1;
	}
	write_str("DEVFS_FB0_OK\n");

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0)
	{
		if (errno == ENODEV)
		{
			write_str("KTM_FBDEV_FB_UNAVAILABLE\n");
			return 0;
		}
		fase54a_fail("open_fb0", "open");
		return -1;
	}
	write_str("KTM_FBDEV_FBDEV_PRESENT\n");

	memset(&var, 0, sizeof(var));
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0)
	{
		close(fd);
		fase54a_fail("ioctl_var", "ioctl");
		return -1;
	}

	memset(&fix, 0, sizeof(fix));
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0)
	{
		close(fd);
		fase54a_fail("ioctl_fix", "ioctl");
		return -1;
	}

	if (var.xres == 0 || var.yres == 0 || var.bits_per_pixel == 0 ||
	    fix.line_length == 0 || fix.smem_len == 0)
	{
		close(fd);
		fase54a_fail("getinfo_validation", "zero_field");
		return -1;
	}
	if (fix.type != FB_TYPE_PACKED_PIXELS || fix.visual != FB_VISUAL_TRUECOLOR)
	{
		close(fd);
		fase54a_fail("getinfo_validation", "format");
		return -1;
	}
	write_str("FB_FACADE_OK\n");
	write_str("KTM_FBDEV_FB_GETINFO_OK\n");

	map_len = (size_t)fix.smem_len;
	if (map_len > 4096u)
		map_len = 4096u;
	if (map_len < 4u)
	{
		close(fd);
		fase54a_fail("draw_validation", "size");
		return -1;
	}

	map = (unsigned char *)mmap(NULL, map_len, PROT_READ | PROT_WRITE,
				    MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
	{
		close(fd);
		fase54a_fail("draw_mmap", "mmap");
		return -1;
	}

	/*
	 * Minimal checker-like mark in top-left region through mmap path.
	 * This validates fbdev userspace rendering without kernel-side memcpy.
	 */
	map[0] = 0xFF;
	map[1] = 0x00;
	map[2] = 0x00;
	map[3] = 0x00;
	map[4] = 0x00;
	map[5] = 0xFF;
	map[6] = 0x00;
	map[7] = 0x00;

	(void)munmap(map, map_len);
	close(fd);

	write_str("KTM_FBDEV_FB_DRAW_OK\n");
	return 0;
}

int main(void)
{
	write_str("KTM_FBDEV_START\n");
	write_str("KTM_FBDEV_HARNESS_ID=ktm_fbdev_smoke.c\n");

	if (check_fbdev_slice() != 0)
		goto halt;

	write_str("KTM_FBDEV_OK\n");

halt:
	for (;;)
		(void)pause();

	return 0;
}
