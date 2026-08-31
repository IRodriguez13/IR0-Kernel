/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: uapi.h
 * Description: Userspace ABI for /dev/ktm (ioctl + event layout).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Ioctl request numbers (32-bit; musl may sign-extend). */
#define KTM_IOC_RUN_SCENARIO   0x4B01u
#define KTM_IOC_RUN_INVARIANTS 0x4B02u
#define KTM_IOC_TAKE_SNAPSHOT  0x4B03u
#define KTM_IOC_CONFIG_FAULT   0x4B04u
#define KTM_IOC_RESET          0x4B05u
#define KTM_IOC_GET_CAPS       0x4B06u
#define KTM_IOC_USER_EVENT     0x4B07u
/*
 * Dump the tail of the typed event ring to the serial log. @arg packs the
 * subsystem bitmask in the high 32 bits and the event count in the low 32
 * (0 count = no limit, 0 mask = every subsystem); see KTM_DUMP_ARG.
 * Non-destructive: the consumer cursor used by ktm_event_copy_out is
 * untouched, so a failing test can dump without disturbing a scenario that
 * is still draining events.
 */
#define KTM_IOC_DUMP_EVENTS    0x4B08u
#define KTM_DUMP_ARG(mask, count) \
	((unsigned long)(count) | ((unsigned long)(mask) << 32))

#define KTM_UAPI_VERSION       1u

#define KTM_CAP_EVENTS         (1u << 0)
#define KTM_CAP_TEST           (1u << 1)
#define KTM_CAP_FAULT          (1u << 2)
#define KTM_CAP_USERDEV        (1u << 3)

/* Matches kernel ktm_event_t layout (read(2) on /dev/ktm). */
typedef struct ktm_uapi_event
{
	uint64_t sequence;
	uint64_t timestamp;
	uint32_t cpu;
	int32_t pid;
	uint16_t type;
	uint16_t subsystem;
	uint64_t arg0;
	uint64_t arg1;
	uint64_t arg2;
	uint64_t arg3;
} ktm_uapi_event_t;

typedef struct ktm_user_scenario
{
	char name[64];
	uint64_t seed;
	uint32_t flags;
	int32_t result;
} ktm_user_scenario_t;

typedef struct ktm_ioc_snapshot
{
	uint32_t scope;
	uint32_t flags;
	uint64_t total_frames;
	uint64_t used_frames;
	uint64_t free_frames;
	uint64_t processes;
	uint64_t zombies;
	uint64_t open_fds;
	uint64_t pipes;
} ktm_ioc_snapshot_t;

typedef struct ktm_user_invariants
{
	uint32_t mask;
	int32_t result;
} ktm_user_invariants_t;

typedef struct ktm_user_fault
{
	char name[32];
	uint32_t mode;
	uint32_t value;
	uint32_t seed;
} ktm_user_fault_t;

/* Match ktm_fault_mode_t in ir0/ktm/fault.h */
#define KTM_FAULT_MODE_ONCE         1u
#define KTM_FAULT_MODE_AFTER_N      2u
#define KTM_FAULT_MODE_EVERY_N      3u
#define KTM_FAULT_MODE_PROBABILITY  4u

typedef struct ktm_user_caps
{
	uint32_t version;
	uint32_t caps;
} ktm_user_caps_t;

/*
 * USER_EVENT payload. name[] labels CASE begin/end and ASSERT for transport.
 * type uses KTM_EVENT_* values from event.h / mirrored below for userspace.
 */
typedef struct ktm_user_event
{
	uint32_t type;
	uint32_t subsystem;
	uint64_t arg0;
	uint64_t arg1;
	uint64_t arg2;
	uint64_t arg3;
	char name[64];
} ktm_user_event_t;

/* Userspace-visible event type numbers (keep in sync with event.h). */
#define KTM_UAPI_EVENT_INFO          1u
#define KTM_UAPI_EVENT_TEST_BEGIN    4u
#define KTM_UAPI_EVENT_TEST_END      5u
#define KTM_UAPI_EVENT_ASSERT_PASS   6u
#define KTM_UAPI_EVENT_ASSERT_FAIL   7u
#define KTM_UAPI_EVENT_CHECKPOINT    17u
#define KTM_UAPI_EVENT_CASE_BEGIN    21u
#define KTM_UAPI_EVENT_CASE_END      22u
#define KTM_UAPI_EVENT_USER_ASSERT   23u

#define KTM_UAPI_SUBSYS_TEST         7u
