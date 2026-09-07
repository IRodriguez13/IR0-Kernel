/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sound_blaster.h
 * Description: IR0 kernel source/header file
 */

#ifndef SOUND_BLASTER_H
#define SOUND_BLASTER_H

#include <stdint.h>
#include <stdbool.h>

// Sound Blaster 16 I/O Ports
#define SB16_BASE_PORT          0x220
#define SB16_MIXER_PORT         (SB16_BASE_PORT + 0x04)
#define SB16_MIXER_DATA         (SB16_BASE_PORT + 0x05)
#define SB16_RESET_PORT         (SB16_BASE_PORT + 0x06)
#define SB16_READ_DATA          (SB16_BASE_PORT + 0x0A)
#define SB16_WRITE_DATA         (SB16_BASE_PORT + 0x0C)
#define SB16_READ_STATUS        (SB16_BASE_PORT + 0x0E)
#define SB16_ACK_16BIT          (SB16_BASE_PORT + 0x0F)

// DSP Status bits
#define SB16_DSP_BUSY           0x80
#define SB16_DSP_READY          0xAA

// DSP Timeouts and delays
#define SB16_DSP_TIMEOUT        1000

// Mixer values
#define SB16_MIXER_VOL_MEDIUM   0x88

// DMA Channels
#define SB16_DMA_8BIT           1
#define SB16_DMA_16BIT          5

// IRQ
#define SB16_IRQ                5

// Mixer registers
#define SB16_MIXER_MASTER_VOL   0x22
#define SB16_MIXER_IRQ_SEL      0x80
#define SB16_MIXER_DMA_SEL      0x81
#define SB16_MIXER_IRQ5_BIT     0x02
#define SB16_MIXER_DMA1_8BIT_BIT 0x02
#define SB16_MIXER_PCM_VOL      0x04
#define SB16_MIXER_CD_VOL       0x28
#define SB16_MIXER_LINE_VOL     0x2E
#define SB16_MIXER_MIC_VOL      0x0A

// DSP Commands
#define SB16_DSP_SET_TIME_CONST 0x40
#define SB16_DSP_SET_SAMPLE_RATE 0x41
#define SB16_DSP_SPEAKER_ON     0xD1
#define SB16_DSP_SPEAKER_OFF    0xD3
#define SB16_DSP_PLAY_8BIT      0x14
#define SB16_DSP_PLAY_16BIT     0xB0
#define SB16_DSP_PAUSE_8BIT     0xD0
#define SB16_DSP_PAUSE_16BIT    0xD5
#define SB16_DSP_RESUME_8BIT    0xD4
#define SB16_DSP_RESUME_16BIT   0xD6
#define SB16_DSP_GET_VERSION    0xE1

// Audio formats
typedef enum {
    SB16_FORMAT_8BIT_MONO = 0,
    SB16_FORMAT_8BIT_STEREO = 1,
    SB16_FORMAT_16BIT_MONO = 2,
    SB16_FORMAT_16BIT_STEREO = 3
} sb16_format_t;

// Audio sample structure
typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    sb16_format_t format;
    bool is_playing;
} sb16_sample_t;

// Sound Blaster state
typedef struct {
    bool initialized;
    bool speaker_enabled;
    uint8_t master_volume;
    uint8_t pcm_volume;
    uint16_t dsp_version;
    uint32_t current_sample_rate;
    sb16_format_t current_format;
    sb16_sample_t *current_sample;
} sb16_state_t;

// Function prototypes
bool sb16_init(void);
void sb16_shutdown(void);
bool sb16_is_available(void);
bool sb16_reset_dsp(void);
uint16_t sb16_get_dsp_version(void);

// Volume control
void sb16_set_master_volume(uint8_t volume);
uint8_t sb16_get_master_volume(void);
void sb16_set_pcm_volume(uint8_t volume);
uint8_t sb16_get_pcm_volume(void);

// Speaker control
void sb16_speaker_on(void);
void sb16_speaker_off(void);
bool sb16_is_speaker_on(void);

// Sample management
int sb16_create_sample(sb16_sample_t *sample, uint8_t *data, uint32_t size, 
                       uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
void sb16_destroy_sample(sb16_sample_t *sample);

// Playback control
int sb16_play_pcm(const void *data, uint32_t size, uint32_t sample_rate);
int sb16_play_sample(sb16_sample_t *sample);
int sb16_stop_playback(void);
int sb16_pause_playback(void);
int sb16_resume_playback(void);
bool sb16_is_playing(void);
void sb16_post_irq_selftest(void);

// Low-level DSP functions
bool sb16_dsp_write(uint8_t data);
uint8_t sb16_dsp_read(void);
bool sb16_dsp_ready_write(void);
bool sb16_dsp_ready_read(void);

// Mixer functions
void sb16_mixer_write(uint8_t reg, uint8_t data);
uint8_t sb16_mixer_read(uint8_t reg);

// DMA setup
void sb16_setup_dma_8bit(uint32_t buffer_addr, uint16_t length);
void sb16_setup_dma_16bit(uint32_t buffer_addr, uint16_t length);

// Interrupt handler
void sb16_irq_handler(void);

#endif // SOUND_BLASTER_H
