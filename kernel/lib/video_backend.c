/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: video_backend.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Video backend API implementation
 */

#include <ir0/video_backend.h>
#include <config.h>
#if CONFIG_ENABLE_VBE
#include <ir0/vbe.h>
#endif

int video_backend_init_from_multiboot(uint32_t multiboot_info)
{
#if CONFIG_ENABLE_VBE
    return vbe_init_from_multiboot(multiboot_info);
#else
    (void)multiboot_info;
    return -1;
#endif
}

int video_backend_init_fallback(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_init();
#else
    return -1;
#endif
}

bool video_backend_is_available(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_is_available();
#else
    return false;
#endif
}

int video_backend_fail_reason(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_fail_reason;
#else
    return 0;
#endif
}

bool video_backend_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp)
{
#if CONFIG_ENABLE_VBE
    return vbe_get_info(width, height, bpp);
#else
    (void)width;
    (void)height;
    (void)bpp;
    return false;
#endif
}

uint8_t *video_backend_get_fb(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_get_fb();
#else
    return (uint8_t *)0;
#endif
}

uint32_t video_backend_get_pitch(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_get_pitch();
#else
    return 0;
#endif
}

uint32_t video_backend_get_fb_phys(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_get_fb_phys();
#else
    return 0;
#endif
}

uint32_t video_backend_get_fb_size(void)
{
#if CONFIG_ENABLE_VBE
    return vbe_get_fb_size();
#else
    return 0;
#endif
}
