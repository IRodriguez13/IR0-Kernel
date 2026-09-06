/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: config.h
 * Description: Unified kernel configuration and debug flags
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once


/*
 * Pull in menuconfig-generated defines when available.
 * When building without menuconfig, the file won't exist and
 * the fallback defaults below apply. Optional subsystems (USB,
 * Bluetooth, networking) default to off, matching Makefile ?= n
 * when .config is absent.
 */
#if defined(__has_include)
#if __has_include(<generated/autoconf.h>)
#include <generated/autoconf.h>
#elif __has_include(<autoconf.h>)
#include <autoconf.h>
#endif
#endif

/* MEMORY SUBSYSTEM DEBUG FLAGS                                              */

/* Memory Allocator Debug */
#if defined(CONFIG_DEBUG_MEMORY_ALLOCATOR) && CONFIG_DEBUG_MEMORY_ALLOCATOR
#define DEBUG_MEMORY_ALLOCATOR 1
#else
#define DEBUG_MEMORY_ALLOCATOR 0
#endif
#if defined(CONFIG_DEBUG_MEMORY_COALESCING) && CONFIG_DEBUG_MEMORY_COALESCING
#define DEBUG_MEMORY_COALESCING 1
#else
#define DEBUG_MEMORY_COALESCING 0
#endif
#if defined(CONFIG_DEBUG_MEMORY_STATS) && CONFIG_DEBUG_MEMORY_STATS
#define DEBUG_MEMORY_STATS 1
#else
#define DEBUG_MEMORY_STATS 0
#endif
#if defined(CONFIG_DEBUG_PMM) && CONFIG_DEBUG_PMM
#define DEBUG_PMM 1
#else
#define DEBUG_PMM 0
#endif


/* Paging Debug */
#if defined(CONFIG_DEBUG_PAGING) && CONFIG_DEBUG_PAGING
#define DEBUG_PAGING 1                /* Paging operations */
#else
#define DEBUG_PAGING 0
#endif
#if defined(CONFIG_DEBUG_PAGE_FAULTS) && CONFIG_DEBUG_PAGE_FAULTS
#define DEBUG_PAGE_FAULTS 1           /* Page fault handling */
#else
#define DEBUG_PAGE_FAULTS 0
#endif
#if defined(CONFIG_DEBUG_D1_DIAG) && CONFIG_DEBUG_D1_DIAG
#define DEBUG_D1_DIAG 1               /* D1.10/D1.12/D1.16 bring-up serial */
#else
#define DEBUG_D1_DIAG 0
#endif

/* PROCESS SUBSYSTEM DEBUG FLAGS                                             */

#if defined(CONFIG_DEBUG_PROCESS) && CONFIG_DEBUG_PROCESS
#define DEBUG_PROCESS 1               /* Process creation/destruction */
#else
#define DEBUG_PROCESS 0
#endif

#if defined(CONFIG_DEBUG_BOOT) && CONFIG_DEBUG_BOOT
#define DEBUG_BOOT 1                  /* Verbose [BOOT]/serial chatter */
#else
#define DEBUG_BOOT 0
#endif
#if defined(CONFIG_DEBUG_SCHEDULER) && CONFIG_DEBUG_SCHEDULER
#define DEBUG_SCHEDULER 1             /* Scheduler decisions */
#else
#define DEBUG_SCHEDULER 0
#endif
#if defined(CONFIG_DEBUG_FORK) && CONFIG_DEBUG_FORK
#define DEBUG_FORK 1                  /* Fork operations */
#else
#define DEBUG_FORK 0
#endif

/* mmap/VMA audit serial noise ([MMAP_AUDIT]/[FASE39] tags) */
#if defined(CONFIG_DEBUG_MMAP_AUDIT) && CONFIG_DEBUG_MMAP_AUDIT
#define DEBUG_MMAP_AUDIT 1
#else
#define DEBUG_MMAP_AUDIT 0
#endif

/* Legacy FASE49/50 Kconfig symbols removed — always off. */
#define DEBUG_FASE50 0
#define DEBUG_FASE49 0

/* Trap Flag / #DB — off by default; enable only for deliberate tracing. */
#ifndef IR0_ENABLE_SINGLE_STEP
#define IR0_ENABLE_SINGLE_STEP 0
#endif
#ifndef IR0_ENABLE_FORK_SINGLESTEP_TRACE
#define IR0_ENABLE_FORK_SINGLESTEP_TRACE 0
#endif

/* FILESYSTEM DEBUG FLAGS                                                    */

#if defined(CONFIG_DEBUG_VFS) && CONFIG_DEBUG_VFS
#define DEBUG_VFS 1                   /* VFS operations */
#else
#define DEBUG_VFS 0
#endif
#if defined(CONFIG_DEBUG_FS_MOUNT) && CONFIG_DEBUG_FS_MOUNT
#define DEBUG_FS_MOUNT 1              /* Mount/unmount operations */
#else
#define DEBUG_FS_MOUNT 0
#endif

/* SYSCALL DEBUG FLAGS                                                       */

#if defined(CONFIG_DEBUG_SYSCALLS) && CONFIG_DEBUG_SYSCALLS
#define DEBUG_SYSCALLS 1              /* All syscall invocations */
#else
#define DEBUG_SYSCALLS 0
#endif
#if defined(CONFIG_DEBUG_SYSCALL_PARAMS) && CONFIG_DEBUG_SYSCALL_PARAMS
#define DEBUG_SYSCALL_PARAMS 1        /* Detailed syscall parameters */
#else
#define DEBUG_SYSCALL_PARAMS 0
#endif
#if defined(CONFIG_DEBUG_KEYBOARD) && CONFIG_DEBUG_KEYBOARD
#define DEBUG_KEYBOARD 1              /* Serial log each stdin read */
#else
#define DEBUG_KEYBOARD 0
#endif
#if defined(CONFIG_DEBUG_PS2) && CONFIG_DEBUG_PS2
#define DEBUG_PS2 1
#else
#define DEBUG_PS2 0
#endif

/* KERNEL BEHAVIOR CONFIGURATION                                             */

/*
 * KERNEL_DEBUG_SHELL — retired. Product boot always kexecve("/sbin/init").
 * Kept as 0 so any residual #if KERNEL_DEBUG_SHELL compiles out.
 */
#define KERNEL_DEBUG_SHELL 0

#ifndef CONFIG_TICK_RATE_HZ
#define CONFIG_TICK_RATE_HZ 1000
#endif

/*
 * IR0_KERNEL_TESTS
 * Include in-kernel test suite (test_runner + contracts).
 * Default: undefined (make ir0) - tests not linked.
 * Makefile sets -DIR0_KERNEL_TESTS=1 for kernel-x64-test.bin / make tests.
 * See Makefile target kernel-x64-test.bin.
 */

/*
 * KERNEL_ENABLE_EXAMPLE_DRIVERS
 * Enable multi-language example drivers registration in kernel
 * 
 * This flag is automatically set by the Makefile when drivers are enabled.
 * Default: 0 (disabled)
 * 
 * To enable: make en-ext-drv && make ir0
 * The Makefile will automatically pass -DKERNEL_ENABLE_EXAMPLE_DRIVERS=1
 * 
 * Note: This flag controls both compilation and runtime registration
 */
#ifndef KERNEL_ENABLE_EXAMPLE_DRIVERS
#define KERNEL_ENABLE_EXAMPLE_DRIVERS 0
#endif

/* COMPATIBILITY - Keep existing definitions                                 */

/* These are included for backward compatibility with includes/ir0/config.h */
#ifdef DEBUG
#define KERNEL_DEBUG 1
#define VERBOSE_LOGGING 1
#else
#define KERNEL_DEBUG 0
#define VERBOSE_LOGGING 0
#endif

/* MEMORY CONFIGURATION */
#define DEFAULT_STACK_SIZE (4 * 1024)    // 4KB stack per process
#define KERNEL_HEAP_SIZE (16 * 1024 * 1024) // 16MB kernel heap
#define USER_HEAP_MAX_SIZE (256 * 1024 * 1024) // 256MB max per process
/*
 * Per-process kernel stack (Linux thread-stack model). Used for both the
 * syscall-insn entry (kernel_syscall_stack_top) and CPL3->CPL0 transitions
 * (TSS.rsp0) of that process. A private stack per task means a peer's syscall
 * can no longer clobber a task parked mid-syscall (poll/pause in-kernel loops),
 * which a single shared global syscall stack allowed.
 */
#define IR0_PROC_KSTACK_SIZE (32 * 1024)
/*
 * Linux-like: per-task kernel stacks live in a dedicated supervisor VA range
 * outside the low identity / user-brk window (see process_kernel_stack_alloc).
 * Slot = guard page + IR0_PROC_KSTACK_SIZE; grows down from kstack_top.
 */
#define IR0_KSTACK_VA_BASE    0xFFFFFE8000000000ULL
#define IR0_KSTACK_SLOT_SIZE  (64UL * 1024UL) /* 4K guard + 32K stack + slack */
#define IR0_KSTACK_MAX_SLOTS  8192U
/* Fill byte for unused kernel stack; see ktm_stack_peak_used(). */
#define IR0_KSTACK_POISON     0xAAU

/* MEMORY LAYOUT — virtual addresses and segment selectors */
#define USER_STACK_TOP      0x7FFFF000UL
/* 1 MiB — BusyBox tab completion / large ash frames need headroom below guard. */
#define USER_STACK_SIZE     0x100000
#define USER_STACK_BASE     (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_STACK_GUARD    (USER_STACK_BASE - 0x1000UL)
/*
 * Program break for processes without an ELF image starts at the mmap floor
 * (above PMM identity). Exec'd images set heap from PT_LOAD end (see
 * elf_compute_initial_brk) — contiguous with BSS, no artificial hole.
 * Anon mmap arena also starts here so mmap never lands in PMM identity.
 */
#define USER_HEAP_BASE      0x20000000UL
/* Anon mmap arena starts at the same floor; sys_mmap searches above heap_end. */
#define USER_MMAP_START     0x20000000UL
/*
 * Linux-like separation: anon mmap arena ends at least USER_STACK_MMAP_GAP below
 * the stack guard page (not flush against guard like pre-D1.15).
 */
#define USER_STACK_MMAP_GAP (1UL << 20)  /* 1 MiB */
#define USER_MMAP_END       (USER_STACK_GUARD - USER_STACK_MMAP_GAP)
#define INIT_DEBUG_STACK_BASE 0x1000000UL

/*
 * Shared keyboard ring (kernel IRQ fills; userspace/debug may consume).
 * Layout: KEYBOARD_BUFFER_SIZE bytes data, then int write position.
 */
#define KEYBOARD_BUFFER_ADDR    0x500000UL
#define KEYBOARD_BUFFER_SIZE    4096

#define KERNEL_CODE_SEL     0x08
#define KERNEL_DATA_SEL     0x10
#define USER_CODE_SEL       0x1B
#define USER_DATA_SEL       0x23

/*
 * Class B safety net in switch_to: KERNEL_CS + user RIP → apply syscall_frame.
 * Set IR0_CLASS_B_REPAIR=0 (CFLAGS) for deterministic KTM inject repro of
 * KERNEL_RET_BAD_RIP. Default on for product/desk builds.
 */
#ifndef IR0_CLASS_B_REPAIR
#define IR0_CLASS_B_REPAIR 1
#endif
#define RFLAGS_IF           0x202
/*
 * PMM frame pool: physical [32 MiB, 512 MiB). Must stay below USER_MMAP_START
 * (0x20000000): process CR3 identity-maps PMM as PA==VA for COW; covering the
 * mmap arena makes user mmap hit supervisor 2 MiB pages (SEGV). Boot map 0–512 MiB.
 */
#define PMM_PHYS_BASE       0x2000000
#define PMM_PHYS_SIZE       0x1E000000

/* PROCESS LIMITS */
#define MAX_PROCESSES 256
#define MAX_FDS_PER_PROCESS 64
#define MAX_NICE 19
#define MIN_NICE -20
#define DEFAULT_NICE 0


/* FILESYSTEM CONFIGURATION */
#define MAX_PATH_LENGTH 256
#define MAX_FILENAME_LENGTH 64
#define VFS_MAX_OPEN_FILES 128

/* HARDWARE LIMITS */
#define MAX_CPUS 8
#define MAX_IRQ_HANDLERS 256
#define TIMER_FREQUENCY CONFIG_TICK_RATE_HZ

/* FEATURE FLAGS — CONFIG_ENABLE_* from Makefile / autoconf take precedence.
 * When building without menuconfig, optional subsystems default to disabled.     */

#ifndef CONFIG_ENABLE_SMP
#define CONFIG_ENABLE_SMP 0
#endif
#define ENABLE_SMP CONFIG_ENABLE_SMP
#ifndef CONFIG_ENABLE_USB_HOST
#define CONFIG_ENABLE_USB_HOST 0
#endif
#ifndef CONFIG_INIT_USB_HOST
#define CONFIG_INIT_USB_HOST 0
#endif
#define ENABLE_USB CONFIG_ENABLE_USB_HOST

#ifndef CONFIG_SCHEDULER_POLICY
#define CONFIG_SCHEDULER_POLICY 0
#endif

/* KERNEL VERSION AND BUILD INFO (after CONFIG_* so uname version macros see them) */
#include <ir0/version.h>

#ifndef CONFIG_ROOT_BLOCK_DEVICE
#define CONFIG_ROOT_BLOCK_DEVICE "hda"
#endif

#ifndef CONFIG_ROOT_FILESYSTEM
#define CONFIG_ROOT_FILESYSTEM "minix"
#endif

#ifndef CONFIG_ENABLE_NETWORKING
#define CONFIG_ENABLE_NETWORKING 0
#endif
#ifndef CONFIG_DRV_NIC_RTL8139
#define CONFIG_DRV_NIC_RTL8139 CONFIG_ENABLE_NETWORKING
#endif
#ifndef CONFIG_DRV_NIC_E1000
#define CONFIG_DRV_NIC_E1000 0
#endif
#ifndef CONFIG_DRV_NIC_VIRTIO_NET
#define CONFIG_DRV_NIC_VIRTIO_NET 0
#endif
#ifndef CONFIG_ARCH_X86_64
#define CONFIG_ARCH_X86_64 1
#endif
#ifndef CONFIG_ARCH_ARM64
#define CONFIG_ARCH_ARM64 0
#endif

#ifndef CONFIG_ENABLE_SOUND
#define CONFIG_ENABLE_SOUND 1
#endif

#ifndef CONFIG_CONSOLE_SERIAL_MIRROR
#define CONFIG_CONSOLE_SERIAL_MIRROR 1
#endif

#ifndef CONFIG_ENABLE_VBE
#define CONFIG_ENABLE_VBE 1
#endif

#ifndef CONFIG_ENABLE_MOUSE
#define CONFIG_ENABLE_MOUSE 1
#endif
#ifndef CONFIG_KEYBOARD_LAYOUT
#define CONFIG_KEYBOARD_LAYOUT 0
#endif

#ifndef CONFIG_ENABLE_BLUETOOTH
#define CONFIG_ENABLE_BLUETOOTH 0
#endif
#ifndef CONFIG_ENABLE_STORAGE_ATA
#define CONFIG_ENABLE_STORAGE_ATA 1
#endif
#ifndef CONFIG_ENABLE_STORAGE_ATA_BLOCK
#define CONFIG_ENABLE_STORAGE_ATA_BLOCK CONFIG_ENABLE_STORAGE_ATA
#endif
#ifndef CONFIG_ENABLE_FS_MINIX
#define CONFIG_ENABLE_FS_MINIX 1
#endif
#ifndef CONFIG_ENABLE_FS_TMPFS
#define CONFIG_ENABLE_FS_TMPFS 1
#endif
#ifndef CONFIG_ENABLE_FS_SIMPLEFS
#define CONFIG_ENABLE_FS_SIMPLEFS 1
#endif
#ifndef CONFIG_ENABLE_FS_FAT16
#define CONFIG_ENABLE_FS_FAT16 1
#endif
#ifndef CONFIG_ENABLE_FS_EXT2
#define CONFIG_ENABLE_FS_EXT2 1
#endif
#ifndef CONFIG_ENABLE_PC_SPEAKER
#define CONFIG_ENABLE_PC_SPEAKER 1
#endif

#ifndef CONFIG_ENABLE_EXAMPLE_DRIVERS
#define CONFIG_ENABLE_EXAMPLE_DRIVERS 0
#endif

#ifndef CONFIG_INIT_PS2_CONTROLLER
#define CONFIG_INIT_PS2_CONTROLLER 1
#endif
#ifndef CONFIG_INIT_PC_SPEAKER
#define CONFIG_INIT_PC_SPEAKER CONFIG_ENABLE_PC_SPEAKER
#endif
#ifndef CONFIG_INIT_STORAGE_ATA
#define CONFIG_INIT_STORAGE_ATA CONFIG_ENABLE_STORAGE_ATA
#endif
#ifndef CONFIG_INIT_STORAGE_ATA_BLOCK
#define CONFIG_INIT_STORAGE_ATA_BLOCK CONFIG_ENABLE_STORAGE_ATA_BLOCK
#endif
#ifndef CONFIG_INIT_SOUND_DRIVERS
#define CONFIG_INIT_SOUND_DRIVERS CONFIG_ENABLE_SOUND
#endif
#ifndef CONFIG_INIT_MOUSE_DRIVER
#define CONFIG_INIT_MOUSE_DRIVER CONFIG_ENABLE_MOUSE
#endif
#ifndef CONFIG_INIT_NETWORK_STACK
#define CONFIG_INIT_NETWORK_STACK CONFIG_ENABLE_NETWORKING
#endif
#ifndef CONFIG_INIT_BLUETOOTH_DRIVER
#define CONFIG_INIT_BLUETOOTH_DRIVER CONFIG_ENABLE_BLUETOOTH
#endif

/*
 * Driver init policy:
 * - Core boot drivers are always initialized from kmain.
 * - Selectable hardware stacks use INIT_* symbols and init_all_drivers().
 */
#define CONFIG_DRIVER_CORE_SERIAL 1
#define CONFIG_DRIVER_CORE_CLOCK 1
#define CONFIG_DRIVER_CORE_INTERRUPTS 1

/* Product serial stays quiet; enable via menuconfig / -D for ktm deep runs. */
#ifndef CONFIG_KTM_SERIAL_VERBOSE
#define CONFIG_KTM_SERIAL_VERBOSE 0
#endif

#ifndef CONFIG_LOG_PROFILE_QUIET
#define CONFIG_LOG_PROFILE_QUIET 0
#endif
#ifndef CONFIG_LOG_PROFILE_NORMAL
#define CONFIG_LOG_PROFILE_NORMAL 1
#endif
#ifndef CONFIG_LOG_PROFILE_DEBUG
#define CONFIG_LOG_PROFILE_DEBUG 0
#endif
#ifndef CONFIG_LOG_PROFILE_TRACE
#define CONFIG_LOG_PROFILE_TRACE 0
#endif

/* Legacy aliases for code that still uses the old names */
#define ENABLE_NETWORKING CONFIG_ENABLE_NETWORKING
#define ENABLE_GRAPHICS   CONFIG_ENABLE_VBE
#define ENABLE_SOUND      CONFIG_ENABLE_SOUND

