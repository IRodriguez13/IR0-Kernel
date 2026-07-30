/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_inet_ioctl.h
 * Description: Linux SIOCGIF and SIOCSIF plus SIOCADDRT/SIOCDELRT for BusyBox.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/types.h>

/*
 * Linux x86-64 sockios.h request numbers.
 * Source: include/uapi/linux/sockios.h
 */
#define IR0_SIOCADDRT     0x890B
#define IR0_SIOCDELRT     0x890C
#define IR0_SIOCGIFNAME   0x8910
#define IR0_SIOCGIFCONF   0x8912
#define IR0_SIOCGIFFLAGS  0x8913
#define IR0_SIOCSIFFLAGS  0x8914
#define IR0_SIOCGIFADDR   0x8915
#define IR0_SIOCSIFADDR   0x8916
#define IR0_SIOCSIFDSTADDR 0x8918
#define IR0_SIOCGIFBRDADDR 0x8919
#define IR0_SIOCSIFBRDADDR 0x891a
#define IR0_SIOCGIFNETMASK 0x891b
#define IR0_SIOCSIFNETMASK 0x891c
#define IR0_SIOCGIFMETRIC 0x891d
#define IR0_SIOCSIFMETRIC 0x891e
#define IR0_SIOCGIFMTU    0x8921
#define IR0_SIOCSIFMTU    0x8922
#define IR0_SIOCSIFHWADDR 0x8924
#define IR0_SIOCGIFHWADDR 0x8927
#define IR0_SIOCGIFINDEX  0x8933
#define IR0_SIOCGIFTXQLEN 0x8942
#define IR0_SIOCSIFTXQLEN 0x8943

int64_t sock_inet_ioctl(uint64_t request, void *arg);
