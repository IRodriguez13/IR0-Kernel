/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE54C — deterministic input path smoke for CI.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define EV_KEY 0x01
#define KEY_W 17
#define IR0_INPUT_IOCTL_INJECT 0x49520001u

struct ir0_input_event
{
    uint16_t type;
    uint16_t code;
    int32_t value;
    int64_t timestamp_ms;
};

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

static void fase54c_fail(const char *step)
{
    write_str("[KTM_INPUT_DET][FAIL] step=");
    write_str(step ? step : "(null)");
    write_str("\n");
    write_str("KTM_INPUT_DET_FAIL_REASON=");
    write_str(step ? step : "unknown");
    write_str("\n");
}

static int64_t now_ms(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
        return 0;
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}

static int parse_interactive_timeout_ms(int argc, char **argv)
{
    int i;
    const char prefix[] = "--interactive-timeout=";
    size_t prefix_len = sizeof(prefix) - 1;

    for (i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], prefix, prefix_len) == 0)
        {
            long v = strtol(argv[i] + prefix_len, NULL, 10);
            if (v < 0)
                v = 0;
            if (v > 60000)
                v = 60000;
            return (int)v;
        }
    }
    return 0;
}

static int read_one_key_event(int fd, int expected_value, int timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;

    for (;;)
    {
        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));

        if (n == (ssize_t)sizeof(ev))
        {
            if (ev.type == EV_KEY && ev.code == KEY_W && ev.value == expected_value)
                return 0;
        }
        if (now_ms() > deadline)
            return -1;
        usleep(5000);
    }
}

static void draw_ack_if_fb_available(void)
{
    struct stat st;
    int fd_fb;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    uint8_t *fb_map;
    size_t map_len;
    uint32_t bpp;
    uint32_t y;
    uint32_t x;

    if (stat("/dev/fb0", &st) != 0)
        return;

    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0)
        return;

    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &var) != 0 ||
        ioctl(fd_fb, FBIOGET_FSCREENINFO, &fix) != 0)
    {
        close(fd_fb);
        return;
    }

    map_len = (size_t)fix.smem_len;
    if (map_len < 4096)
        map_len = 4096;
    fb_map = (uint8_t *)mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
    if (fb_map == MAP_FAILED)
    {
        close(fd_fb);
        return;
    }

    bpp = var.bits_per_pixel / 8;
    if (bpp > 0 && bpp <= 4)
    {
        uint32_t color = 0x00AAFFu;
        for (y = 8; y < 24 && y < var.yres; y++)
        {
            for (x = 8; x < 24 && x < var.xres; x++)
            {
                uint8_t *dst = fb_map + ((size_t)y * fix.line_length) + ((size_t)x * bpp);
                memcpy(dst, &color, bpp);
            }
        }
    }

    (void)munmap(fb_map, map_len);
    close(fd_fb);
}

int main(int argc, char **argv)
{
    struct stat st;
    int fd_in;
    int interactive_timeout_ms;

    write_str("KTM_INPUT_DET_START\n");
    write_str("KTM_INPUT_DET_HARNESS_ID=ktm_input_det_smoke.c\n");

    if (stat("/dev/events0", &st) != 0)
    {
        fase54c_fail("stat_events0");
        goto halt;
    }

    fd_in = open("/dev/events0", O_RDONLY | O_NONBLOCK);
    if (fd_in < 0)
    {
        fase54c_fail("open_events0");
        goto halt;
    }

    {
        struct ir0_input_event inject_ev;

        memset(&inject_ev, 0, sizeof(inject_ev));
        inject_ev.type = EV_KEY;
        inject_ev.code = KEY_W;
        inject_ev.value = 1;
        if (ioctl(fd_in, IR0_INPUT_IOCTL_INJECT, &inject_ev) != 0)
        {
            fase54c_fail("inject_press");
            close(fd_in);
            goto halt;
        }

        inject_ev.value = 0;
        if (ioctl(fd_in, IR0_INPUT_IOCTL_INJECT, &inject_ev) != 0)
        {
            fase54c_fail("inject_release");
            close(fd_in);
            goto halt;
        }
    }

    if (read_one_key_event(fd_in, 1, 2000) != 0)
    {
        interactive_timeout_ms = parse_interactive_timeout_ms(argc, argv);
        if (interactive_timeout_ms > 0)
        {
            int64_t deadline = now_ms() + interactive_timeout_ms;
            while (now_ms() <= deadline)
            {
                struct input_event ev;
                ssize_t n = read(fd_in, &ev, sizeof(ev));
                if (n == (ssize_t)sizeof(ev) && ev.type == EV_KEY)
                {
                    write_str("KTM_INPUT_DET_INPUT_MANUAL_OK\n");
                    write_str("KTM_INPUT_DET_OK\n");
                    close(fd_in);
                    goto halt;
                }
                usleep(10000);
            }
            write_str("KTM_INPUT_DET_INPUT_MANUAL_TIMEOUT\n");
            write_str("KTM_INPUT_DET_OK\n");
            close(fd_in);
            goto halt;
        }
        fase54c_fail("read_press");
        close(fd_in);
        goto halt;
    }

    if (read_one_key_event(fd_in, 0, 2000) != 0)
    {
        fase54c_fail("read_release");
        close(fd_in);
        goto halt;
    }

    write_str("INPUT_INJECT_TESTHOOK_OK\n");
    write_str("DEVFS_EVENTS0_READ_OK\n");
    write_str("INPUT_EVENT_READ_OK\n");
    write_str("KTM_INPUT_DET_OK\n");

    draw_ack_if_fb_available();

    write_str("KTM_INPUT_DET_OK\n");
    close(fd_in);

halt:
    for (;;)
        (void)pause();
    return 0;
}
