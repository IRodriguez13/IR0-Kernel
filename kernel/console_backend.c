/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console backend facade implementation
 */

#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/video_console.h>
#include <ir0/typewriter.h>
#include <ir0/serial_io.h>
#include <ir0/vga.h>
#include <ir0/ktm/klog.h>
#include <config.h>

#ifndef CONFIG_CONSOLE_SERIAL_MIRROR
#define CONFIG_CONSOLE_SERIAL_MIRROR 1
#endif

#define CONSOLE_BACKEND_READY_MSG "IR0 console ready\n"

static int printk_to_screen = 1;
static int userspace_gui_first_draw_tag;
/* After handoff, do not mirror TTY glyphs to serial (human console vs bootlog). */
static int tty_serial_mirror = 1;

void console_backend_init(void)
{
    console_init();
}

void console_backend_typewriter_init(void)
{
    typewriter_init();
    typewriter_set_mode(TYPEWRITER_DISABLED);
}

void console_backend_clear(uint8_t color)
{
    typewriter_console_clear(color);
}

int console_backend_uses_framebuffer(void)
{
    return console_use_framebuffer();
}

int console_backend_fb_scale(void)
{
    return console_get_fb_scale();
}

int console_backend_printk_to_screen(void)
{
    return printk_to_screen;
}

void console_backend_panic_screen_on(void)
{
    /*
     * Re-enable kernel text output to the active console. After the userspace
     * handoff printk_to_screen is 0, so a panic drew nothing on the FB/GTK
     * screen (print()/clear_screen() were gated). Turn it back on so the panic
     * banner and the mirrored dump reach the screen, not just serial.
     */
    printk_to_screen = 1;
}

void console_backend_set_tty_serial_mirror(int on)
{
    tty_serial_mirror = on ? 1 : 0;
}

void console_backend_userspace_handoff(void)
{
    printk_to_screen = 0;
#if !CONFIG_CONSOLE_SERIAL_MIRROR
    tty_serial_mirror = 0;
#endif
    /*
     * Product console must be instant. TYPEWRITER_* delays are a demo effect
     * only — never leave FAST/NORMAL/SLOW enabled after userspace attach.
     * Keep TTY→COM1 mirror when CONFIG_CONSOLE_SERIAL_MIRROR=y so headless
     * smokes can grep prompts; serial_putchar itself must not spin forever
     * if the host stdio pipe backs up.
     */
    typewriter_set_mode(TYPEWRITER_DISABLED);
    typewriter_console_clear(IR0_CONSOLE_COLOR_DEFAULT);

    if (console_backend_uses_framebuffer())
        klog_smoke("CONSOLE_BACKEND_FB_OK");
    else
        klog_smoke("CONSOLE_BACKEND_VGA_OK");

    klog_smoke("PRINTK_SERIAL_CONSOLE_FB_HANDOFF_OK");
    console_backend_write(CONSOLE_BACKEND_READY_MSG,
                          sizeof(CONSOLE_BACKEND_READY_MSG) - 1,
                          IR0_CONSOLE_COLOR_DEFAULT);
    klog_smoke("CONSOLE_GUI_VISIBLE_OK");
}

void console_backend_scroll(int lines)
{
    typewriter_console_scroll(lines);
}

void console_backend_write(const char *str, size_t len, uint8_t color)
{
    size_t i;

    if (!str)
        return;

    if (!printk_to_screen && console_backend_uses_framebuffer() &&
        len > 0 && !userspace_gui_first_draw_tag)
    {
        userspace_gui_first_draw_tag = 1;
        klog_smoke("BUSYBOX_GUI_TEXT_FIRST_DRAW_OK");
        klog_smoke("ASH_VISIBLE_QEMU_OK");
    }

    for (i = 0; i < len; i++)
    {
        char c = str[i];

        if (c == '\n')
        {
            if (tty_serial_mirror)
            {
                serial_putchar('\r');
                serial_putchar('\n');
            }
            typewriter_vga_print("\n", color);
        }
        else
        {
            if (tty_serial_mirror)
                serial_putchar(c);
            typewriter_vga_print_char(c, color);
        }
    }
}

void console_backend_show_cursor(uint8_t color)
{
    typewriter_show_cursor(color);
}

int console_backend_cursor_x(void)
{
    return typewriter_cursor_x();
}
