/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cmdline.h
 * Description: Multiboot cmdline parser for ir0.loglevel= / ir0.trace= / boot flags.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Apply Kconfig LOG_PROFILE_* default, then override from Multiboot cmdline
 * (flags bit 2 / cmdline pointer). Safe before serial is up — only mutates
 * klog profile/level/trace mask and boot feature flags (e.g. ash smoke tags).
 */
void ir0_cmdline_apply_log_profile(void);

/* Non-zero when Multiboot cmdline contains ir0.ash_smoke=1 (QEMU smokes only). */
int ir0_cmdline_ash_smoke_enabled(void);
