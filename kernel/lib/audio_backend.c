/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: audio_backend.c
 * Description: Audio facade glue over Sound Blaster 16 driver.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/audio_backend.h>
#include <config.h>

#if CONFIG_ENABLE_SOUND
#include <ir0/sound_blaster.h>
#endif

bool audio_backend_is_available(void)
{
#if CONFIG_ENABLE_SOUND
	return sb16_is_available();
#else
	return false;
#endif
}

int audio_backend_play_pcm(const void *buf, size_t count, uint32_t sample_rate,
			   uint8_t channels, uint8_t bits_per_sample)
{
#if CONFIG_ENABLE_SOUND
	int ret;

	if (!buf || count == 0)
		return 0;
	if (!sb16_is_available())
		return (int)count;
	(void)channels;
	(void)bits_per_sample;
	ret = sb16_play_pcm(buf, (uint32_t)count, sample_rate);
	return (ret == 0) ? (int)count : -1;
#else
	(void)buf;
	(void)count;
	(void)sample_rate;
	(void)channels;
	(void)bits_per_sample;
	return -1;
#endif
}

void audio_backend_set_master_volume(uint8_t volume)
{
#if CONFIG_ENABLE_SOUND
	sb16_set_master_volume(volume);
#else
	(void)volume;
#endif
}

uint8_t audio_backend_get_master_volume(void)
{
#if CONFIG_ENABLE_SOUND
	return sb16_mixer_read(SB16_MIXER_MASTER_VOL);
#else
	return 0;
#endif
}

void audio_backend_speaker_on(void)
{
#if CONFIG_ENABLE_SOUND
	sb16_speaker_on();
#endif
}

void audio_backend_speaker_off(void)
{
#if CONFIG_ENABLE_SOUND
	sb16_speaker_off();
#endif
}
