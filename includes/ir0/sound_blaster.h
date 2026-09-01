/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sound_blaster.h
 * Description: Sound Blaster 16 facade for portable audio backend (no drivers/).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SB16_MIXER_MASTER_VOL 0x22

typedef enum
{
	SB16_FORMAT_8BIT_MONO = 0,
	SB16_FORMAT_8BIT_STEREO = 1,
	SB16_FORMAT_16BIT_MONO = 2,
	SB16_FORMAT_16BIT_STEREO = 3
} sb16_format_t;

typedef struct
{
	uint8_t *data;
	uint32_t size;
	uint32_t sample_rate;
	uint8_t channels;
	uint8_t bits_per_sample;
	sb16_format_t format;
	bool is_playing;
} sb16_sample_t;

bool sb16_is_available(void);
void sb16_set_master_volume(uint8_t volume);
uint8_t sb16_mixer_read(uint8_t reg);
void sb16_speaker_on(void);
void sb16_speaker_off(void);
int sb16_create_sample(sb16_sample_t *sample, uint8_t *data, uint32_t size,
		       uint32_t sample_rate, uint8_t channels,
		       uint8_t bits_per_sample);
void sb16_destroy_sample(sb16_sample_t *sample);
int sb16_play_sample(sb16_sample_t *sample);
