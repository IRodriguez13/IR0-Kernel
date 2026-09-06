/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tls.h
 * Description: User TLS base facade (x86 FS.base / ARM TPIDR_EL0 / RISC-V tp).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void set_fs_base(uint64_t base);
void restore_user_fs_base(void);

static inline void set_tls(uint64_t base)
{
	set_fs_base(base);
}

static inline void set_user_tls(uint64_t base)
{
	set_tls(base);
}

static inline void tls_invalidate(void)
{
}
