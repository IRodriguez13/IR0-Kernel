/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: time.h
 * Description: IR0 kernel source/header file
 */

/*
 * IR0 Kernel - POSIX time structures (OSDev / Time And Date)
 * struct timespec, struct timeval for nanosleep, gettimeofday
 */
#pragma once

#include <ir0/types.h>

/* POSIX timespec - seconds + nanoseconds */
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

/* POSIX timeval - seconds + microseconds */
typedef long suseconds_t;
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

/* POSIX itimerval — setitimer/getitimer (ITIMER_REAL etc.) */
struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2
