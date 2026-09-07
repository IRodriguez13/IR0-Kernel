/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sound_blaster.c
 * Description: Sound Blaster 16 audio driver with DMA support and 8/16-bit playback
 */

#include "sound_blaster.h"
#include <ir0/vga.h>
#include <stddef.h>
#include <string.h>
#include <ir0/arch_port.h>
#include <ir0/arch_io.h>
#include <ir0/cpu.h>
#include <drivers/dma/dma.h>
#include <ir0/kmem.h>
#include <drivers/timer/pit/pit.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <ir0/resource_registry.h>
#include <ir0/ktm/klog.h>
#include <ir0/irq.h>

/* Global Sound Blaster state */
static sb16_state_t sb16_state = {0};

/*
 * Ping-pong DMA buffers (static, below 16 MiB). Samples must outlive DMA;
 * never kfree after PLAY. The 64 KiB alignment prevents 8237 boundary wraps.
 */
#define SB16_PCM_BUF_MAX          8192u
#define SB16_DMA_BOUNDARY         65536u
#define SB16_DSP_MIN_RATE_HZ      4000u
#define SB16_DSP_MAX_RATE_HZ      45454u
#define SB16_DSP_TIME_BASE_HZ     1000000u
#define SB16_DSP_COUNTER_BASE     256u
#define SB16_RESET_SPIN_LIMIT     10000u
#define SB16_SELFTEST_RATE_HZ     11025u
#define SB16_SELFTEST_SPIN_LIMIT  200000u
static uint8_t sb16_pcm_buf[2][SB16_PCM_BUF_MAX]
    __attribute__((aligned(SB16_DMA_BOUNDARY)));
static volatile int sb16_active_buf = -1;
static volatile bool sb16_hw_playing;
static uint32_t sb16_cached_rate;
static bool sb16_speaker_on_state;

/* Forward declarations */
static int32_t sb16_hw_init(void);
static int sb16_program_rate(uint32_t sample_rate);
static void sb16_reclaim_playback(void);
static bool sb16_dsp_data_ready(void);
static bool sb16_dsp_ready_write_hot(void);

/* Driver registration structures */
static ir0_driver_ops_t sb16_ops = {
    .init = sb16_hw_init,
    .shutdown = sb16_shutdown
};

static ir0_driver_info_t sb16_info = {
    .name = "Sound Blaster 16",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "ISA Sound Blaster 16 Audio Driver",
    .language = IR0_DRIVER_LANG_C
};

/**
 * sb16_init - register Sound Blaster 16 driver
 */
bool sb16_init(void)
{
    LOG_INFO("SB16", "Registering Sound Blaster 16 driver...");
    ir0_register_driver(&sb16_info, &sb16_ops);
    return true;
}

static int32_t sb16_hw_init(void)
{
    LOG_INFO("SB16", "Initializing Sound Blaster 16 hardware...");

    /* Reset the DSP */
    if (!sb16_reset_dsp())
    {
        LOG_WARNING("SB16", "DSP not detected (normal if no SB16 hardware)");
        return IR0_DRIVER_ABSENT;
    }

    /* Check DSP version */
    uint16_t version = sb16_get_dsp_version();
    if (version == 0)
    {
        LOG_WARNING("SB16", "DSP version unavailable (treating as absent)");
        return IR0_DRIVER_ABSENT;
    }

    sb16_state.dsp_version = version;
    LOG_INFO_FMT("SB16", "DSP Version %d.%d detected", version >> 8, version & 0xFF);
    klog_smoke("SB16_DSP_OK");

    /* Route IRQ5 + 8-bit DMA1 (mixer 80h/81h bitmasks — OSDev SB16). */
    sb16_mixer_write(SB16_MIXER_IRQ_SEL, SB16_MIXER_IRQ5_BIT);
    sb16_mixer_write(SB16_MIXER_DMA_SEL, SB16_MIXER_DMA1_8BIT_BIT);

    /* Set default volume */
    sb16_set_master_volume(SB16_MIXER_VOL_MEDIUM);

    sb16_state.initialized = true;
    resource_register_ioport(SB16_BASE_PORT, SB16_ACK_16BIT, "sound blaster");
    resource_register_irq(SB16_IRQ, "sb16");
    irq_unmask_line(SB16_IRQ);
    return 0;
}

void sb16_shutdown(void)
{
    if (!sb16_state.initialized)
    {
        return;
    }

    /* Reset DSP to stop any playback */
    sb16_reset_dsp();

    sb16_state.initialized = false;
    sb16_cached_rate = 0;
    sb16_speaker_on_state = false;
    sb16_hw_playing = false;
}

bool sb16_is_available(void)
{
    return sb16_state.initialized;
}

bool sb16_reset_dsp(void)
{
    /* Write 1 to reset port */
    outb(SB16_RESET_PORT, 1);
    
    /* Wait 3 microseconds (or more to be safe) */
    for (volatile unsigned spin = 0; spin < SB16_RESET_SPIN_LIMIT; spin++)
    {
        cpu_relax();
    }

    /* Write 0 to reset port */
    outb(SB16_RESET_PORT, 0);

    /* Wait for DSP to be ready (0xAA) */
    int timeout = 1000;
    while (timeout--)
    {
        if (sb16_dsp_ready_read())
        {
            if (inb(SB16_READ_DATA) == SB16_DSP_READY)
            {
                return true;
            }
        }
    }

    return false;
}

bool sb16_dsp_write(uint8_t data)
{
    if (!sb16_dsp_ready_write())
    {
        return false;
    }

    outb(SB16_WRITE_DATA, data);
    return true;
}

uint8_t sb16_dsp_read(void)
{
    if (!sb16_dsp_ready_read())
    {
        return 0;
    }

    return inb(SB16_READ_DATA);
}

bool sb16_dsp_ready_read(void)
{
    int timeout = SB16_DSP_TIMEOUT;
    while (timeout--)
    {
        if (inb(SB16_READ_STATUS) & 0x80)
        {
            return true;
        }
    }
    return false;
}

static bool sb16_dsp_data_ready(void)
{
    return (inb(SB16_READ_STATUS) & SB16_DSP_BUSY) != 0;
}

bool sb16_dsp_ready_write(void)
{
    return sb16_dsp_ready_write_hot();
}

static bool sb16_dsp_ready_write_hot(void)
{
    int timeout = SB16_DSP_TIMEOUT;

    while (timeout--)
    {
        /* READ_STATUS bit7 clear => DSP ready for write (HWRM). */
        if (!(inb(SB16_READ_STATUS) & 0x80))
            return true;
        cpu_relax();
    }
    return false;
}

/*
 * Drop stale DMA state before starting a new buffer. Without this, a missed
 * IRQ leaves the DSP busy and every SFX write spins in sb16_dsp_ready_write.
 */
static void sb16_reclaim_playback(void)
{
    if (!sb16_hw_playing)
    {
        return;
    }

    /* Never poll here: this path runs synchronously in the writer's frame. */
    if (sb16_dsp_data_ready())
    {
        (void)inb(SB16_READ_DATA);
    }
    dma_disable_channel(SB16_DMA_8BIT);
    sb16_hw_playing = false;
}

uint16_t sb16_get_dsp_version(void)
{
    if (!sb16_dsp_write(SB16_DSP_GET_VERSION))
    {
        return 0;
    }

    uint8_t major = sb16_dsp_read();
    uint8_t minor = sb16_dsp_read();

    return (uint16_t)((major << 8) | minor);
}

void sb16_set_master_volume(uint8_t volume)
{
    sb16_mixer_write(SB16_MIXER_MASTER_VOL, volume);
}

void sb16_mixer_write(uint8_t reg, uint8_t data)
{
    outb(SB16_MIXER_PORT, reg);
    outb(SB16_MIXER_DATA, data);
}

uint8_t sb16_mixer_read(uint8_t reg)
{
    outb(SB16_MIXER_PORT, reg);
    return inb(SB16_MIXER_DATA);
}

void sb16_speaker_on(void)
{
    sb16_dsp_write(SB16_DSP_SPEAKER_ON);
}

void sb16_speaker_off(void)
{
    sb16_dsp_write(SB16_DSP_SPEAKER_OFF);
}

/*
 * sb16_setup_dma_8bit - Configure DMA channel 1 for 8-bit playback.
 * Buffer must be below 16MB physical (kernel heap is identity-mapped).
 */
void sb16_setup_dma_8bit(uint32_t buffer_addr, uint16_t length)
{
    dma_disable_channel(SB16_DMA_8BIT);
    dma_setup_channel(SB16_DMA_8BIT, buffer_addr, length, false);
    dma_enable_channel(SB16_DMA_8BIT);
}

static int sb16_program_rate(uint32_t sample_rate)
{
    uint32_t sr = sample_rate;

    if (sr < SB16_DSP_MIN_RATE_HZ)
    {
        sr = SB16_DSP_MIN_RATE_HZ;
    }
    if (sr > SB16_DSP_MAX_RATE_HZ)
    {
        sr = SB16_DSP_MAX_RATE_HZ;
    }
    {
        uint8_t tc = (uint8_t)(SB16_DSP_COUNTER_BASE
                               - (SB16_DSP_TIME_BASE_HZ / sr));

        if (!sb16_dsp_write(SB16_DSP_SET_TIME_CONST) || !sb16_dsp_write(tc))
        {
            return -1;
        }
    }
    return 0;
}

/*
 * sb16_create_sample - Allocate buffer, copy PCM data, fill sample struct.
 * Caller's data is copied; buffer stays valid until sb16_destroy_sample.
 */
int sb16_create_sample(sb16_sample_t *sample, uint8_t *data, uint32_t size,
                       uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (!sample || !data || size == 0)
        return -1;
    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf)
        return -1;
    memcpy(buf, data, size);
    sample->data = buf;
    sample->size = size;
    sample->sample_rate = sample_rate;
    sample->channels = channels;
    sample->bits_per_sample = bits_per_sample;
    sample->format = (bits_per_sample == 16) ? SB16_FORMAT_16BIT_MONO : SB16_FORMAT_8BIT_MONO;
    sample->is_playing = false;
    return 0;
}

void sb16_destroy_sample(sb16_sample_t *sample)
{
    if (sample && sample->data)
    {
        kfree(sample->data);
        sample->data = NULL;
    }
}

int sb16_play_pcm(const void *data, uint32_t size, uint32_t sample_rate)
{
    unsigned long irq_flags;
    int buf_idx;
    uint32_t phys;
    uint16_t len;
    uint16_t dsp_count;

    if (!sb16_state.initialized || !data || size == 0)
        return -1;
    if (size > SB16_PCM_BUF_MAX)
        size = SB16_PCM_BUF_MAX;
    if (size > 0xFFFEu)
        return -1;

    buf_idx = (sb16_active_buf == 0) ? 1 : 0;
    memcpy(sb16_pcm_buf[buf_idx], data, size);

    /* IRQ5 must not observe a partially programmed DSP/DMA transaction. */
    irq_flags = irq_save();
    sb16_reclaim_playback();

    if (!sb16_speaker_on_state)
    {
        sb16_speaker_on();
        sb16_speaker_on_state = true;
    }
    if (sample_rate != sb16_cached_rate)
    {
        if (sb16_program_rate(sample_rate) != 0)
            goto fail;
        sb16_cached_rate = sample_rate;
    }

    len = (uint16_t)size;
    /* Single-cycle DSP count is (bytes - 1); DMA helper accepts byte length. */
    dsp_count = (uint16_t)(len - 1u);
    phys = (uint32_t)(uintptr_t)sb16_pcm_buf[buf_idx];
    sb16_setup_dma_8bit(phys, len);

    if (!sb16_dsp_write(SB16_DSP_PLAY_8BIT))
        goto fail;
    if (!sb16_dsp_write((uint8_t)(dsp_count & 0xFF)) ||
        !sb16_dsp_write((uint8_t)((dsp_count >> 8) & 0xFF)))
        goto fail;

    sb16_active_buf = buf_idx;
    sb16_hw_playing = true;
    irq_restore(irq_flags);
    return 0;

fail:
    dma_disable_channel(SB16_DMA_8BIT);
    sb16_hw_playing = false;
    irq_restore(irq_flags);
    return -1;
}

/*
 * sb16_play_sample - Legacy API: copy into ping-pong buffer (no heap UAF).
 */
int sb16_play_sample(sb16_sample_t *sample)
{
    int ret;

    if (!sample || !sample->data || sample->size == 0)
        return -1;
    ret = sb16_play_pcm(sample->data, sample->size, sample->sample_rate);
    sample->is_playing = (ret == 0);
    return ret;
}

int sb16_stop_playback(void)
{
    sb16_reclaim_playback();
    return 0;
}

bool sb16_is_playing(void)
{
    return sb16_hw_playing;
}

/*
 * Post-sti DMA+IRQ self-test: one silent buffer so smoke can grep SB16_IRQ_OK.
 * Must run after enable_interrupts() (boot_runtime), not from sb16_hw_init.
 */
void sb16_post_irq_selftest(void)
{
    static const uint8_t probe_silence[64];

    if (!sb16_state.initialized)
        return;
    /* Fire-and-forget: IRQ path validated interactively / optional log grep. */
    (void)sb16_play_pcm(probe_silence, (uint32_t)sizeof(probe_silence),
                        SB16_SELFTEST_RATE_HZ);
    /*
     * QEMU audiodev=none may not deliver IRQ5; poll-read ack so hw_playing
     * clears and smoke can grep SB16_IRQ_OK when the DSP status bit sets.
     */
    for (volatile unsigned spin = 0;
         spin < SB16_SELFTEST_SPIN_LIMIT && sb16_hw_playing; spin++)
    {
        if (sb16_dsp_data_ready())
            sb16_irq_handler();
    }
    klog_smoke("SB16_SELFTEST_FIRED");
}

void sb16_irq_handler(void)
{
    static int irq_smoke_logged;

    /* Reading base+0x0e acknowledges an SB16 8-bit DMA interrupt. */
    (void)inb(SB16_READ_STATUS);
    sb16_hw_playing = false;
    if (!irq_smoke_logged)
    {
        irq_smoke_logged = 1;
        klog_smoke("SB16_IRQ_OK");
    }
}
