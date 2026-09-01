/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — framebuffer facade for /dev/fb0 and userspace bridges.
 */

#include <ir0/fb.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>
#include <ir0/video_backend.h>
#include <string.h>

#define IR0_FB_VGA_TEXT_PHYS 0x000B8000u

static bool ir0_fb_fill_info(struct ir0_fb_info *info)
{
    uint32_t width;
    uint32_t height;
    uint32_t bpp;

    if (!info)
        return false;
    if (!video_backend_is_available())
        return false;
    if (!video_backend_get_info(&width, &height, &bpp))
        return false;

    info->width = width;
    info->height = height;
    info->bpp = bpp;
    info->pitch = video_backend_get_pitch();
    info->fb_phys = video_backend_get_fb_phys();
    info->fb_size = video_backend_get_fb_size();
    return true;
}

bool ir0_fb_is_available(void)
{
    struct ir0_fb_info info;

    if (!ir0_fb_fill_info(&info))
        return false;

    /*
     * Treat VGA text fallback as unavailable for fbdev consumers.
     * This keeps /dev/fb0 semantics aligned with linear framebuffer clients.
     */
    if (info.fb_phys == IR0_FB_VGA_TEXT_PHYS)
        return false;
    if (info.width < 320 || info.height < 200)
        return false;
    if (info.pitch == 0 || info.fb_size == 0)
        return false;

    return true;
}

bool ir0_fb_get_info(struct ir0_fb_info *info)
{
    if (!info)
        return false;
    if (!ir0_fb_is_available())
        return false;

    return ir0_fb_fill_info(info);
}

int64_t ir0_fb_write_bytes(size_t offset, const void *buf, size_t count)
{
    struct ir0_fb_info info;
    uint8_t *fb;

    if (!buf)
        return -EFAULT;
    if (!ir0_fb_get_info(&info))
        return -ENODEV;
    if (offset >= info.fb_size)
        return 0;

    fb = video_backend_get_fb();
    if (!fb)
        return -ENODEV;

    if (count > (size_t)info.fb_size - offset)
        count = (size_t)info.fb_size - offset;
    memcpy(fb + offset, buf, count);
    return (int64_t)count;
}

int ir0_fb_write_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t pixel)
{
    struct ir0_fb_info info;
    uint8_t *fb;
    uint32_t x_end;
    uint32_t y_end;
    uint32_t row;
    uint32_t col;
    uint32_t bytes_per_pixel;

    if (!ir0_fb_get_info(&info))
        return -ENODEV;
    if (w == 0 || h == 0)
        return 0;
    if (x >= info.width || y >= info.height)
        return -EINVAL;

    fb = video_backend_get_fb();
    if (!fb)
        return -ENODEV;

    bytes_per_pixel = info.bpp / 8;
    if (bytes_per_pixel == 0 || bytes_per_pixel > 4)
        return -EINVAL;

    x_end = x + w;
    y_end = y + h;
    if (x_end > info.width)
        x_end = info.width;
    if (y_end > info.height)
        y_end = info.height;

    for (row = y; row < y_end; row++)
    {
        uint8_t *line = fb + ((size_t)row * info.pitch);
        for (col = x; col < x_end; col++)
        {
            uint8_t *dst = line + ((size_t)col * bytes_per_pixel);
            memcpy(dst, &pixel, bytes_per_pixel);
        }
    }

    return 0;
}

void ir0_fb_boot_direct_draw(void)
{
    struct ir0_fb_info info;
    uint32_t third;

    if (!ir0_fb_get_info(&info))
    {
        klog_info("FB_BOOT", "unavailable");
        return;
    }

    klog_info_fmt("FB_BOOT",
                  "phys=0x%x w=0x%x h=0x%x bpp=0x%x pitch=0x%x",
                  (unsigned)info.fb_phys, (unsigned)info.width,
                  (unsigned)info.height, (unsigned)info.bpp,
                  (unsigned)info.pitch);

    third = info.height / 3;
    if (third == 0)
        third = 1;

    /*
     * BGR888 32bpp (QEMU multiboot default): byte0=B, byte1=G, byte2=R.
     * Red   = 0x0000FF00, green = 0x00FF0000, blue = 0x000000FF (LE uint32).
     */
    (void)ir0_fb_write_rect(0, 0, info.width, third, 0x0000FF00U);
    (void)ir0_fb_write_rect(0, third, info.width, third, 0x00FF0000U);
    if (third * 2 < info.height)
    {
        (void)ir0_fb_write_rect(0, third * 2, info.width,
                                info.height - (third * 2), 0x000000FFU);
    }

    klog_smoke("FB_BOOT_DIRECT_DRAW_OK");
}
