/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE55A — doomgeneric prerequisite loop (fb + input + wad read).
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

struct input_event
{
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

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

static void fase55a_fail(const char *step)
{
    write_str("[KTM_DOOM_PREREQ][FAIL] step=");
    write_str(step ? step : "(null)");
    write_str("\n");
    write_str("KTM_DOOM_PREREQ_FAIL_REASON=");
    write_str(step ? step : "unknown");
    write_str("\n");
}

static int draw_frame(uint8_t *fb, const struct fb_var_screeninfo *var,
                      const struct fb_fix_screeninfo *fix)
{
    uint32_t x;
    uint32_t y;
    uint32_t bpp;

    if (!fb || !var || !fix)
        return -1;
    bpp = var->bits_per_pixel / 8;
    if (bpp == 0 || bpp > 4)
        return -1;

    for (y = 0; y < var->yres; y++)
    {
        for (x = 0; x < var->xres; x++)
        {
            uint32_t color = ((x ^ y) & 0xFFu) << 8;
            uint8_t *dst = fb + ((size_t)y * fix->line_length) + ((size_t)x * bpp);
            memcpy(dst, &color, bpp);
        }
    }
    return 0;
}

static int read_wad_smoke(void)
{
    int fd;
    uint8_t buf[4096];
    ssize_t n;
    size_t total = 0;

    fd = open("/usr/share/doom/doom1.wad", O_RDONLY);
    if (fd < 0)
        return -1;

    for (;;)
    {
        n = read(fd, buf, sizeof(buf));
        if (n < 0)
        {
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    close(fd);
    return (total >= 65536) ? 0 : -1;
}

int main(void)
{
    int fd_fb;
    int fd_in;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    uint8_t *fb_map;
    size_t map_len;
    struct input_event ev;
    int tries;

    write_str("KTM_DOOM_PREREQ_START\n");
    write_str("KTM_DOOM_PREREQ_HARNESS_ID=ktm_doom_prereq_smoke.c\n");

    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0)
    {
        fase55a_fail("open_fb0");
        goto halt;
    }
    write_str("DOOM_LOOP_FB_OPEN_OK\n");

    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &var) != 0 ||
        ioctl(fd_fb, FBIOGET_FSCREENINFO, &fix) != 0)
    {
        close(fd_fb);
        fase55a_fail("fb_getinfo");
        goto halt;
    }

    map_len = (size_t)fix.smem_len;
    if (map_len < 4096)
        map_len = 4096;
    fb_map = (uint8_t *)mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
    if (fb_map == MAP_FAILED)
    {
        close(fd_fb);
        fase55a_fail("fb_mmap");
        goto halt;
    }

    fd_in = open("/dev/events0", O_RDONLY | O_NONBLOCK);
    if (fd_in < 0)
    {
        (void)munmap(fb_map, map_len);
        close(fd_fb);
        fase55a_fail("open_events0");
        goto halt;
    }
    write_str("DOOM_LOOP_INPUT_OPEN_OK\n");

    if (read_wad_smoke() != 0)
    {
        close(fd_in);
        (void)munmap(fb_map, map_len);
        close(fd_fb);
        fase55a_fail("wad_read");
        goto halt;
    }
    write_str("DOOM_WAD_READ_OK\n");

    if (draw_frame(fb_map, &var, &fix) != 0)
    {
        close(fd_in);
        (void)munmap(fb_map, map_len);
        close(fd_fb);
        fase55a_fail("draw_frame");
        goto halt;
    }

    /*
     * Simulate minimal loop touchpoint: poll input a few cycles.
     * Frame was already drawn; event presence is not required.
     */
    for (tries = 0; tries < 20; tries++)
    {
        (void)read(fd_in, &ev, sizeof(ev));
        usleep(5000);
    }

    write_str("DOOM_FRAME_DRAW_OK\n");
    write_str("KTM_DOOM_PREREQ_DOOM_PREREQ_OK\n");
    write_str("KTM_DOOM_PREREQ_OK\n");

    close(fd_in);
    (void)munmap(fb_map, map_len);
    close(fd_fb);

halt:
    for (;;)
        (void)pause();
    return 0;
}
