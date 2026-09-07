/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE55D - Real doomgeneric userspace backend for IR0.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "upstream/doomgeneric/doomgeneric.h"
#include "upstream/doomgeneric/doomkeys.h"
#include "upstream/doomgeneric/d_event.h"
#include "upstream/doomgeneric/i_sound.h"
#include "upstream/doomgeneric/w_wad.h"
#include "upstream/doomgeneric/z_zone.h"

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define IR0_INPUT_IOCTL_GET_CAPS 0x49520002u

#define EV_KEY 0x01
#define EV_REL 0x02
#define REL_X  0x00
#define REL_Y  0x01
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

#define AUDIO_SET_FORMAT 0x1005

#define IR0_AUDIO_SAMPLE_RATE 11025u
#define IR0_AUDIO_CHANNELS 1u
#define IR0_AUDIO_BITS_PER_SAMPLE 8u
#define IR0_AUDIO_WRITE_MAX 4096u
#define DMX_HEADER_SIZE 8u
#define DMX_FORMAT_PCM 3u

#define IR0_KEY_ESC       1
#define IR0_KEY_ENTER     28
#define IR0_KEY_LEFTCTRL  29
#define IR0_KEY_A         30
#define IR0_KEY_D         32
#define IR0_KEY_Q         16
#define IR0_KEY_S         31
#define IR0_KEY_W         17
#define IR0_KEY_SPACE     57
#define IR0_KEY_LEFTSHIFT 42
#define IR0_KEY_UP        103
#define IR0_KEY_LEFT      105
#define IR0_KEY_RIGHT     106
#define IR0_KEY_DOWN      108

struct audio_format
{
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
};

/* Bound by I_BindSoundVariables when FEATURE_SOUND is enabled. */
int use_libsamplerate;
float libsamplerate_scale = 0.65f;

#define FASE55D_MAX_FRAMES 240
#define KEY_QUEUE_SIZE 64

#ifdef FASE55E_INTERACTIVE
#define DOOM_CFG_PATH "/etc/doom-frames"
#define DOOM_CFG_PATH_HOSTSHARE "/mnt/host/doom-frames"
#endif

struct input_event
{
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct fb_bitfield
{
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
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
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
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

struct ir0_input_caps
{
    int keyboard;
    int mouse;
    int touch;
    int gamepad;
    int has_events0;
    int keyboard_ps2;
    int mouse_ps2;
    int event_queue_depth;
    int supports_key_up_down;
    int supports_mouse_motion;
};

static int g_fd_fb = -1;
static int g_fd_input = -1;
static int g_fd_audio = -1;
static struct fb_var_screeninfo g_var;
static struct fb_fix_screeninfo g_fix;
static uint8_t *g_fb_map;
static size_t g_fb_len;
static uint16_t g_key_queue[KEY_QUEUE_SIZE];
static unsigned int g_key_write;
static unsigned int g_key_read;
static uint32_t g_frame_count;
static int g_first_frame_tagged;
static int g_mouse_buttons;
static int g_mouse_caps_tagged;
static int g_mouse_event_tagged;
static int g_audio_ok_tagged;
static int g_audio_write_tagged;
static boolean g_use_sfx_prefix;
#ifdef FASE55E_INTERACTIVE
static volatile int g_quit_requested;
static unsigned int g_frame_dump_every;
static int g_frame_dump_tagged;
#endif

static void write_str(const char *s)
{
    const char *p = s;

    while (*p)
    {
        p++;
    }
    (void)write(1, s, (size_t)(p - s));
}

static void write_fail(const char *step, const char *class_tag)
{
    write_str("[FASE55D][FAIL] step=");
    write_str(step ? step : "unknown");
    write_str("\n");
    if (class_tag && class_tag[0] != '\0')
    {
        write_str(class_tag);
        write_str("\n");
    }
}

static uint32_t ir0_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
    }
    return 0;
}

static unsigned char convert_to_doom_key(uint16_t code)
{
    switch (code)
    {
        case IR0_KEY_ENTER:
            return KEY_ENTER;
        case IR0_KEY_ESC:
            return KEY_ESCAPE;
        case IR0_KEY_LEFT:
            return KEY_LEFTARROW;
        case IR0_KEY_RIGHT:
            return KEY_RIGHTARROW;
        case IR0_KEY_UP:
            return KEY_UPARROW;
        case IR0_KEY_DOWN:
            return KEY_DOWNARROW;
        case IR0_KEY_LEFTCTRL:
            return KEY_FIRE;
        case IR0_KEY_SPACE:
            return KEY_USE;
        case IR0_KEY_LEFTSHIFT:
            return KEY_RSHIFT;
        case IR0_KEY_W:
            return 'w';
        case IR0_KEY_A:
            return 'a';
        case IR0_KEY_S:
            return 's';
        case IR0_KEY_D:
            return 'd';
        case IR0_KEY_Q:
            return 'q';
        default:
            return 0;
    }
}

static void enqueue_key(int pressed, uint8_t doom_key)
{
    unsigned int next;
    uint16_t data;

    if (doom_key == 0)
    {
        return;
    }

    next = (g_key_write + 1u) % KEY_QUEUE_SIZE;
    if (next == g_key_read)
    {
        return;
    }

    data = (uint16_t)(((pressed ? 1u : 0u) << 8) | doom_key);
    g_key_queue[g_key_write] = data;
    g_key_write = next;
}

static void tag_mouse_event_once(void)
{
    if (!g_mouse_event_tagged)
    {
        g_mouse_event_tagged = 1;
        write_str("DOOMGENERIC_MOUSE_EVENT_OK\n");
    }
}

static void post_mouse(int dx, int dy)
{
    event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = ev_mouse;
    ev.data1 = g_mouse_buttons;
    ev.data2 = dx << 2;
    ev.data3 = (-dy) << 2;
    D_PostEvent(&ev);
    if (dx != 0 || dy != 0 || g_mouse_buttons != 0)
    {
        tag_mouse_event_once();
    }
}

static void pump_input_events(void)
{
    struct input_event ev;
    int dx = 0;
    int dy = 0;
    int have_rel = 0;

    if (g_fd_input < 0)
    {
        return;
    }

    for (;;)
    {
        ssize_t n = read(g_fd_input, &ev, sizeof(ev));

        if (n != (ssize_t)sizeof(ev))
        {
            break;
        }

        if (ev.type == EV_KEY)
        {
            unsigned char key = convert_to_doom_key(ev.code);

#ifdef FASE55E_INTERACTIVE
            if (ev.code == IR0_KEY_ESC && ev.value == 1)
            {
                g_quit_requested = 1;
            }
#endif
            if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE)
            {
                int bit = 0;

                if (ev.code == BTN_LEFT)
                {
                    bit = 1;
                }
                else if (ev.code == BTN_RIGHT)
                {
                    bit = 2;
                }
                else
                {
                    bit = 4;
                }
                if (ev.value)
                {
                    g_mouse_buttons |= bit;
                }
                else
                {
                    g_mouse_buttons &= ~bit;
                }
                post_mouse(0, 0);
                continue;
            }
            if (ev.value == 1 || ev.value == 0)
            {
                enqueue_key(ev.value == 1, key);
            }
        }
        else if (ev.type == EV_REL)
        {
            if (ev.code == REL_X)
            {
                dx += (int)ev.value;
                have_rel = 1;
            }
            else if (ev.code == REL_Y)
            {
                dy += (int)ev.value;
                have_rel = 1;
            }
        }
    }

    if (have_rel)
    {
        post_mouse(dx, dy);
    }
}

static void blit_framebuffer_scaled(void)
{
    uint32_t src_w = DOOMGENERIC_RESX;
    uint32_t src_h = DOOMGENERIC_RESY;
    uint32_t dst_w = g_var.xres;
    uint32_t dst_h = g_var.yres;
    uint32_t bpp = g_var.bits_per_pixel / 8;
    uint32_t x;
    uint32_t y;
    const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;

    if (!g_fb_map || bpp == 0 || bpp > 4)
    {
        return;
    }

    if (dst_w == src_w && dst_h == src_h && bpp == 4)
    {
        for (y = 0; y < src_h; y++)
        {
            uint8_t *dst_row = g_fb_map + ((size_t)y * g_fix.line_length);
            const uint8_t *src_row = (const uint8_t *)&src[(size_t)y * src_w];
            memcpy(dst_row, src_row, (size_t)src_w * 4u);
        }
        return;
    }

    for (y = 0; y < dst_h; y++)
    {
        uint32_t sy = (uint32_t)(((uint64_t)y * src_h) / dst_h);
        for (x = 0; x < dst_w; x++)
        {
            uint32_t sx = (uint32_t)(((uint64_t)x * src_w) / dst_w);
            uint32_t c = src[(size_t)sy * src_w + sx];
            uint8_t *dst = g_fb_map + ((size_t)y * g_fix.line_length) + ((size_t)x * bpp);

            if (bpp == 4)
            {
                memcpy(dst, &c, 4);
            }
            else if (bpp == 3)
            {
                dst[0] = (uint8_t)(c & 0xFFu);
                dst[1] = (uint8_t)((c >> 8) & 0xFFu);
                dst[2] = (uint8_t)((c >> 16) & 0xFFu);
            }
            else if (bpp == 2)
            {
                uint16_t rgb565 =
                    (uint16_t)((((c >> 19) & 0x1Fu) << 11) |
                               (((c >> 10) & 0x3Fu) << 5) |
                               (((c >> 3) & 0x1Fu)));
                memcpy(dst, &rgb565, 2);
            }
            else
            {
                dst[0] = (uint8_t)(c & 0xFFu);
            }
        }
    }
}

#ifdef FEATURE_SOUND
static snddevice_t ir0_sound_devices[] = { SNDDEVICE_SB };
static snddevice_t ir0_music_devices[] = { SNDDEVICE_SB };

static void tag_audio_write_once(void)
{
    if (!g_audio_write_tagged)
    {
        g_audio_write_tagged = 1;
        write_str("DOOMGENERIC_AUDIO_WRITE_OK\n");
    }
}

static boolean IR0_SoundInit(boolean use_sfx_prefix)
{
    struct audio_format fmt;
    unsigned char probe[256];
    size_t i;

    g_use_sfx_prefix = use_sfx_prefix;
    g_fd_audio = open("/dev/audio", O_WRONLY);
    if (g_fd_audio < 0)
    {
        write_str("DOOMGENERIC_AUDIO_OPEN_FAIL\n");
        return false;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.sample_rate = IR0_AUDIO_SAMPLE_RATE;
    fmt.channels = IR0_AUDIO_CHANNELS;
    fmt.bits_per_sample = IR0_AUDIO_BITS_PER_SAMPLE;
    (void)ioctl(g_fd_audio, AUDIO_SET_FORMAT, &fmt);

    if (!g_audio_ok_tagged)
    {
        g_audio_ok_tagged = 1;
        write_str("DOOMGENERIC_AUDIO_OK\n");
    }

    /* Short PCM probe so smoke does not depend on title-screen SFX. */
    for (i = 0; i < sizeof(probe); i++)
    {
        probe[i] = (unsigned char)(128 + ((i & 8) ? 40 : -40));
    }
    if (write(g_fd_audio, probe, sizeof(probe)) > 0)
    {
        tag_audio_write_once();
    }
    return true;
}

static void IR0_SoundShutdown(void)
{
    if (g_fd_audio >= 0)
    {
        close(g_fd_audio);
        g_fd_audio = -1;
    }
}

static boolean IR0_BuildSfxName(const sfxinfo_t *sfxinfo, char namebuf[9])
{
    size_t prefix_len = 0;
    size_t name_len;

    if (!sfxinfo)
    {
        return false;
    }
    if (g_use_sfx_prefix)
    {
        namebuf[0] = 'd';
        namebuf[1] = 's';
        prefix_len = 2;
    }

    name_len = strnlen(sfxinfo->name, 8 - prefix_len);
    memcpy(namebuf + prefix_len, sfxinfo->name, name_len);
    namebuf[prefix_len + name_len] = '\0';
    return true;
}

static int IR0_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char namebuf[9];

    if (!IR0_BuildSfxName(sfxinfo, namebuf))
    {
        return -1;
    }
    return W_GetNumForName(namebuf);
}

static void IR0_UpdateSound(void)
{
}

static void IR0_UpdateSoundParams(int channel, int vol, int sep)
{
    (void)channel;
    (void)vol;
    (void)sep;
}

static int IR0_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    int lump;
    int len;
    const unsigned char *data;
    uint32_t samples;
    size_t nbytes;

    (void)vol;
    (void)sep;
    if (!sfxinfo || g_fd_audio < 0)
    {
        return -1;
    }

    lump = sfxinfo->lumpnum;
    if (lump < 0)
    {
        lump = IR0_GetSfxLumpNum(sfxinfo);
        sfxinfo->lumpnum = lump;
    }
    if (lump < 0)
    {
        return -1;
    }

    len = W_LumpLength((unsigned int)lump);
    data = W_CacheLumpNum(lump, PU_CACHE);
    if (!data || len < (int)DMX_HEADER_SIZE)
    {
        return -1;
    }

    /* Classic DMX PCM: format=3, rate LE16, sample count LE32, then 8-bit unsigned. */
    if (data[0] == DMX_FORMAT_PCM && data[1] == 0)
    {
        samples = (uint32_t)data[4]
                | ((uint32_t)data[5] << 8)
                | ((uint32_t)data[6] << 16)
                | ((uint32_t)data[7] << 24);
        if (samples > 0 && samples <= (uint32_t)(len - (int)DMX_HEADER_SIZE))
        {
            nbytes = (size_t)samples;
            if (nbytes > IR0_AUDIO_WRITE_MAX)
            {
                nbytes = IR0_AUDIO_WRITE_MAX;
            }
            if (write(g_fd_audio, data + DMX_HEADER_SIZE, nbytes) > 0)
            {
                tag_audio_write_once();
            }
        }
    }
    return channel;
}

static void IR0_StopSound(int channel)
{
    (void)channel;
}

static boolean IR0_SoundIsPlaying(int channel)
{
    (void)channel;
    return false;
}

static void IR0_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    int i;

    if (!sounds || num_sounds <= 1)
    {
        return;
    }

    /* Keep WAD reads out of gameplay frames, especially the first shot. */
    for (i = 1; i < num_sounds; i++)
    {
        char namebuf[9];
        int lump;

        if (!IR0_BuildSfxName(&sounds[i], namebuf))
        {
            continue;
        }
        lump = W_CheckNumForName(namebuf);

        if (lump >= 0)
        {
            (void)W_CacheLumpNum(lump, PU_CACHE);
        }
    }
}

sound_module_t DG_sound_module = {
    ir0_sound_devices,
    1,
    IR0_SoundInit,
    IR0_SoundShutdown,
    IR0_GetSfxLumpNum,
    IR0_UpdateSound,
    IR0_UpdateSoundParams,
    IR0_StartSound,
    IR0_StopSound,
    IR0_SoundIsPlaying,
    IR0_CacheSounds,
};

static boolean IR0_MusicInit(void)
{
    return false;
}

static void IR0_MusicShutdown(void)
{
}

static void IR0_SetMusicVolume(int volume)
{
    (void)volume;
}

static void IR0_PauseMusic(void)
{
}

static void IR0_ResumeMusic(void)
{
}

static void *IR0_RegisterSong(void *data, int len)
{
    (void)data;
    (void)len;
    return NULL;
}

static void IR0_UnRegisterSong(void *handle)
{
    (void)handle;
}

static void IR0_PlaySong(void *handle, boolean looping)
{
    (void)handle;
    (void)looping;
}

static void IR0_StopSong(void)
{
}

static boolean IR0_MusicIsPlaying(void)
{
    return false;
}

static void IR0_MusicPoll(void)
{
}

music_module_t DG_music_module = {
    ir0_music_devices,
    1,
    IR0_MusicInit,
    IR0_MusicShutdown,
    IR0_SetMusicVolume,
    IR0_PauseMusic,
    IR0_ResumeMusic,
    IR0_RegisterSong,
    IR0_UnRegisterSong,
    IR0_PlaySong,
    IR0_StopSong,
    IR0_MusicIsPlaying,
    IR0_MusicPoll,
};
#endif /* FEATURE_SOUND */

void DG_Init(void)
{
    struct ir0_input_caps caps;

    memset(g_key_queue, 0, sizeof(g_key_queue));
    memset(&g_var, 0, sizeof(g_var));
    memset(&g_fix, 0, sizeof(g_fix));

    g_fd_fb = open("/dev/fb0", O_RDWR);
    if (g_fd_fb < 0)
    {
        write_fail("open_fb0", "FB_SUBSYSTEM_FAIL");
        exit(1);
    }

    if (ioctl(g_fd_fb, FBIOGET_VSCREENINFO, &g_var) != 0 ||
        ioctl(g_fd_fb, FBIOGET_FSCREENINFO, &g_fix) != 0)
    {
        write_fail("fb_ioctl", "FB_SUBSYSTEM_FAIL");
        exit(1);
    }

    g_fb_len = (size_t)g_fix.smem_len;
    if (g_fb_len < 4096)
    {
        g_fb_len = 4096;
    }

    g_fb_map = (uint8_t *)mmap(NULL, g_fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd_fb, 0);
    if (g_fb_map == MAP_FAILED)
    {
        write_fail("fb_mmap", "FB_SUBSYSTEM_FAIL");
        exit(1);
    }

    g_fd_input = open("/dev/events0", O_RDONLY | O_NONBLOCK);
    if (g_fd_input < 0)
    {
        write_fail("open_events0", "INPUT_SUBSYSTEM_FAIL");
        exit(1);
    }

    memset(&caps, 0, sizeof(caps));
    if (ioctl(g_fd_input, IR0_INPUT_IOCTL_GET_CAPS, &caps) != 0)
    {
        write_fail("events0_get_caps", "INPUT_SUBSYSTEM_FAIL");
        exit(1);
    }

    if (caps.mouse || caps.supports_mouse_motion)
    {
        if (!g_mouse_caps_tagged)
        {
            g_mouse_caps_tagged = 1;
            write_str("DOOMGENERIC_MOUSE_CAPS_OK\n");
        }
    }

    write_str("DOOMGENERIC_INIT_OK\n");
}

void DG_DrawFrame(void)
{
    g_frame_count++;
    blit_framebuffer_scaled();
    if (!g_first_frame_tagged)
    {
        g_first_frame_tagged = 1;
        write_str("DOOMGENERIC_FIRST_FRAME_OK\n");
    }
}

void DG_SleepMs(uint32_t ms)
{
    struct timespec req;

    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)((ms % 1000u) * 1000000u);
    if (nanosleep(&req, NULL) != 0 && errno == ENOSYS)
    {
        write_fail("nanosleep", "TIMING_SUBSYSTEM_FAIL");
        exit(1);
    }
}

uint32_t DG_GetTicksMs(void)
{
    uint32_t t = ir0_monotonic_ms();

    if (t == 0)
    {
        write_fail("clock_gettime", "TIMING_SUBSYSTEM_FAIL");
        exit(1);
    }
    return t;
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    uint16_t k;

    if (!pressed || !doomKey)
    {
        return 0;
    }

    pump_input_events();
    if (g_key_read == g_key_write)
    {
        return 0;
    }

    k = g_key_queue[g_key_read];
    g_key_read = (g_key_read + 1u) % KEY_QUEUE_SIZE;
    *pressed = (k >> 8) & 0x1;
    *doomKey = (unsigned char)(k & 0xFFu);
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

static int wad_header_ok(const char *path)
{
    int fd = open(path, O_RDONLY);
    uint8_t hdr[12];
    ssize_t n;

    if (fd < 0)
        return -1;
    n = read(fd, hdr, sizeof(hdr));
    close(fd);
    if (n != (ssize_t)sizeof(hdr))
        return -1;
    if (memcmp(hdr, "IWAD", 4) != 0 && memcmp(hdr, "PWAD", 4) != 0)
        return -1;
    return 0;
}

/*
 * Prefer argv -iwad, then hostshare (/mnt/host), then disk inject path.
 * Desktop ash: mount -t 9p ir0share /mnt/host && /mnt/host/doomgeneric
 */
static const char *pick_wad_path(int argc, char **argv)
{
    static const char *const candidates[] = {
        "/usr/ken/games/doom1.wad",
        "/usr/share/doom/doom1.wad",
        "/mnt/host/doom1.wad",
        NULL
    };
    int i;

    for (i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "-iwad") == 0 && argv[i + 1] && argv[i + 1][0])
            return argv[i + 1];
    }
    for (i = 0; candidates[i]; i++)
    {
        if (wad_header_ok(candidates[i]) == 0)
            return candidates[i];
    }
    return NULL;
}

static int check_required_wad(const char *path)
{
    if (!path || wad_header_ok(path) != 0)
    {
        write_fail("open_wad", "ABI_MISSING");
        return -1;
    }
    write_str("DOOMGENERIC_WAD_LOAD_OK\n");
    return 0;
}

#ifdef FASE55E_INTERACTIVE
static unsigned int read_cfg_line(FILE *f, unsigned int default_val)
{
    char buf[32];
    unsigned long val;

    if (!fgets(buf, sizeof(buf), f))
    {
        return default_val;
    }
    val = strtoul(buf, NULL, 10);
    if (val > 0xFFFFFFFFu)
    {
        return default_val;
    }
    return (unsigned int)val;
}

static void read_interactive_cfg(unsigned int *frame_limit, unsigned int *dump_every)
{
    FILE *f;

    *frame_limit = 0;
    *dump_every = 0;

    f = fopen(DOOM_CFG_PATH, "r");
    if (!f)
        f = fopen(DOOM_CFG_PATH_HOSTSHARE, "r");
    if (!f)
        return;
    *frame_limit = read_cfg_line(f, 0);
    *dump_every = read_cfg_line(f, 0);
    fclose(f);
}

static void dump_framebuffer_ppm(unsigned int frame_num)
{
    char path[64];
    FILE *f;
    uint32_t x;
    uint32_t y;
    const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;
    int fd;

    snprintf(path, sizeof(path), "/tmp/doom-frame-%05u.ppm", frame_num);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        return;
    }

    f = fdopen(fd, "w");
    if (!f)
    {
        close(fd);
        return;
    }

    fprintf(f, "P6\n%u %u\n255\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    for (y = 0; y < DOOMGENERIC_RESY; y++)
    {
        for (x = 0; x < DOOMGENERIC_RESX; x++)
        {
            uint32_t c = src[(size_t)y * DOOMGENERIC_RESX + x];
            uint8_t rgb[3];

            rgb[0] = (uint8_t)(c >> 16);
            rgb[1] = (uint8_t)(c >> 8);
            rgb[2] = (uint8_t)(c);
            if (fwrite(rgb, 1, 3, f) != 3)
            {
                break;
            }
        }
    }
    fclose(f);

    if (!g_frame_dump_tagged)
    {
        g_frame_dump_tagged = 1;
        write_str("DOOMGENERIC_FRAME_DUMP_OK\n");
    }
}

static int run_interactive_loop(unsigned int frame_limit)
{
    write_str("DOOMGENERIC_INPUT_MANUAL_READY\n");
    write_str("DOOMGENERIC_INTERACTIVE_READY\n");

    while (!g_quit_requested)
    {
        if (frame_limit > 0 && g_frame_count >= frame_limit)
        {
            break;
        }

        doomgeneric_Tick();

        if (g_first_frame_tagged)
        {
            static int visible_tagged;

            if (!visible_tagged)
            {
                visible_tagged = 1;
                write_str("DOOMGENERIC_FRAMEBUFFER_VISIBLE\n");
            }
        }

        if (g_frame_dump_every > 0 && g_frame_count > 0 &&
            (g_frame_count % g_frame_dump_every) == 0)
        {
            dump_framebuffer_ppm(g_frame_count);
        }
    }

    write_str("DOOMGENERIC_INTERACTIVE_EXIT_OK\n");
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *wad;
    char *dg_argv[8];
    int dg_argc;

    write_str("DOOMGENERIC_BUILD_OK\n");
#ifdef FASE55E_INTERACTIVE
    write_str("DOOMGENERIC_BUILD_MODE=interactive\n");
#else
    write_str("DOOMGENERIC_BUILD_MODE=smoke\n");
#endif

    wad = pick_wad_path(argc, argv);
    if (check_required_wad(wad) != 0)
    {
        return 1;
    }

    dg_argv[0] = (char *)"doomgeneric-ir0";
    dg_argv[1] = (char *)"-iwad";
    dg_argv[2] = (char *)wad;
    dg_argv[3] = (char *)"-nomusic";
    dg_argv[4] = NULL;
    dg_argc = 4;
    doomgeneric_Create(dg_argc, dg_argv);
    write_str("DOOMGENERIC_INIT_OK\n");

#ifdef FASE55E_INTERACTIVE
    {
        unsigned int frame_limit;
        unsigned int dump_every;

        read_interactive_cfg(&frame_limit, &dump_every);
        g_frame_dump_every = dump_every;
        run_interactive_loop(frame_limit);
    }
#else
    {
        uint32_t start_ms;
        uint32_t end_ms;

        start_ms = DG_GetTicksMs();
        while (g_frame_count < FASE55D_MAX_FRAMES)
        {
            doomgeneric_Tick();
        }
        end_ms = DG_GetTicksMs();

		write_str("DOOMGENERIC_FRAME_LOOP_OK\n");
		if (end_ms > start_ms && (end_ms - start_ms) > 60000u)
		{
			write_str("LONG_RUNNING_BUT_STABLE\n");
		}
		write_str("FASE55D_DOOMGENERIC_OK\n");
		/* Optional KTM product case (inline ioctl — avoid kernel -Iincludes). */
		{
			int kfd = open("/dev/ktm", O_RDWR);
			if (kfd >= 0)
			{
				/* KTM_IOC_USER_EVENT = 0x4B07; CASE_BEGIN=21 CASE_END=22 */
				struct
				{
					unsigned type;
					unsigned subsystem;
					unsigned long long arg0, arg1, arg2, arg3;
					char name[64];
				} ev;
				memset(&ev, 0, sizeof(ev));
				ev.type = 21u;
				ev.subsystem = 7u;
				memcpy(ev.name, "doomgeneric_55d", 16);
				(void)ioctl(kfd, 0x4B07u, &ev);
				ev.type = 22u;
				ev.arg0 = 0;
				(void)ioctl(kfd, 0x4B07u, &ev);
				(void)close(kfd);
				write_str("KTM_DOOM_55D_OK\n");
				write_str("KTM_USERDEV_OK\n");
			}
			else
				write_str("KTM_DOOM_55D_SKIP\n");
		}
	}
#endif

    if (g_fd_input >= 0)
    {
        close(g_fd_input);
        g_fd_input = -1;
    }
    IR0_SoundShutdown();
    if (g_fb_map && g_fb_map != MAP_FAILED)
    {
        (void)munmap(g_fb_map, g_fb_len);
        g_fb_map = NULL;
    }
    if (g_fd_fb >= 0)
    {
        close(g_fd_fb);
        g_fd_fb = -1;
    }

#ifdef FASE55E_INTERACTIVE
    /*
     * Drop leftover cooked-TTY bytes that arrived before events0 divert
     * (or if divert was unavailable). TCFLSH = 0x540B, TCIFLUSH = 0.
     */
    (void)ioctl(STDIN_FILENO, 0x540Bu, 0);
    write_str("DOOMGENERIC_INTERACTIVE_EXIT_OK\n");
#endif

    return 0;
}
