/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: validation.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Critical Debugging Macros
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: critical.h
 * Description: Unified header for critical debugging macros across all kernel subsystems
 */

/**
 * IR0 Kernel - Critical Debugging Macros
 * 
 * Unified header for critical debugging macros across all kernel subsystems.
 * Expande las macros existentes para proporcionar información detallada de ubicación
 * en todos los puntos críticos del kernel.
 */

#pragma once

#include <ir0/oops.h>
#include <stdbool.h>
#include <stddef.h>

// CRITICAL MACROS - Extended from oops.h

/**
 * CHECK_PTR - Validates pointer is not NULL before use
 * Expands to panicex with exact location if check fails
 */
#define CHECK_PTR(ptr, context) \
    do { \
        if (unlikely(!(ptr))) { \
            panicex("NULL pointer in " context, PANIC_MEM, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_RANGE - Validates value is within bounds
 */
#define CHECK_RANGE(val, min, max, context) \
    do { \
        if (unlikely((val) < (min) || (val) > (max))) { \
            panicex("Range check failed in " context, PANIC_KERNEL_BUG, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_ALIGN - Validates pointer alignment
 */
#define CHECK_ALIGN(ptr, alignment, context) \
    do { \
        if (unlikely(((uintptr_t)(ptr) & ((alignment) - 1)) != 0)) { \
            panicex("Alignment check failed in " context, PANIC_MEM, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_SIZE - Validates size parameter
 */
#define CHECK_SIZE(size, max_size, context) \
    do { \
        if (unlikely((size) == 0 || (size) > (max_size))) { \
            panicex("Invalid size in " context, PANIC_MEM, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_BOUNDS - Validates array/buffer bounds
 */
#define CHECK_BOUNDS(index, max, context) \
    do { \
        if (unlikely((index) >= (max))) { \
            panicex("Bounds check failed in " context, PANIC_KERNEL_BUG, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_SYSCALL - Validates syscall number
 */
#define CHECK_SYSCALL(num, max, context) \
    do { \
        if (unlikely((num) < 0 || (num) >= (max))) { \
            panicex("Invalid syscall number in " context, PANIC_KERNEL_BUG, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_FD - Validates file descriptor
 */
#define CHECK_FD(fd, max, context) \
    do { \
        if (unlikely((fd) < 0 || (fd) >= (max))) { \
            panicex("Invalid file descriptor in " context, PANIC_KERNEL_BUG, \
                   __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * CHECK_BUFFER - Validates buffer pointer and size
 */
#define CHECK_BUFFER(buf, size, context) \
    do { \
        CHECK_PTR(buf, context); \
        CHECK_SIZE(size, (size_t)-1, context); \
    } while(0)

/**
 * VALIDATE_BUFFER - Validates buffer and returns error code if invalid
 * Returns -1 if buf is NULL or count is 0 (for functions that return int)
 */
#define VALIDATE_BUFFER(buf, count) \
    ({ \
        int __ret = 0; \
        if (unlikely(!(buf) || (count) == 0)) \
            __ret = -1; \
        __ret; \
    })

/**
 * VALIDATE_BUFFER_PTR - Validates buffer and returns error code if invalid
 * Returns NULL if buf is NULL or count is 0 (for functions that return pointer)
 */
#define VALIDATE_BUFFER_PTR(buf, count) \
    ({ \
        void *__ret = NULL; \
        if (unlikely(!(buf) || (count) == 0)) \
            __ret = NULL; \
        else \
            __ret = (void *)1; /* Non-NULL indicates valid */ \
        __ret; \
    })

/**
 * VALIDATE_STRING - Validates string pointer and non-empty
 * Returns -1 if string is NULL or empty
 */
#define VALIDATE_STRING(str) \
    ({ \
        int __ret = 0; \
        if (unlikely(!(str) || *(str) == '\0')) \
            __ret = -1; \
        __ret; \
    })

/**
 * VALIDATE_CALLBACK - Validates callback function pointer
 * Returns -1 if callback is NULL
 */
#define VALIDATE_CALLBACK(cb) \
    ({ \
        int __ret = 0; \
        if (unlikely(!(cb))) \
            __ret = -1; \
        __ret; \
    })

/**
 * VERIFY - Runtime assertion that doesn't get compiled out
 * Unlike ASSERT, this always runs even in release builds
 */
#define VERIFY(condition, context) \
    do { \
        if (unlikely(!(condition))) { \
            panicex("Verification failed: " #condition " in " context, \
                   PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/**
 * NOT_REACHED - Marks code paths that should never execute
 */
#define NOT_REACHED(context) \
    panicex("Reached unreachable code in " context, PANIC_KERNEL_BUG, \
           __FILE__, __LINE__, __func__)

// SUBSYSTEM-SPECIFIC MACROS

/**
 * FS_CHECK - Filesystem-specific checks
 */
#define FS_CHECK_PATH(path) CHECK_PTR(path, "filesystem path")
#define FS_CHECK_FD(fd) CHECK_FD(fd, 256, "filesystem")
#define FS_CHECK_OFFSET(offset, max) CHECK_RANGE(offset, 0, max, "filesystem offset")

/**
 * MEM_CHECK - Memory management checks (already in kmem.h, but aliased here)
 */
#define MEM_CHECK_PTR(ptr) CHECK_PTR(ptr, "memory allocation")
#define MEM_CHECK_SIZE(size) CHECK_SIZE(size, (size_t)-1, "memory size")
#define MEM_CHECK_ALIGN(ptr, align) CHECK_ALIGN(ptr, align, "memory alignment")

/**
 * SCHED_CHECK - Scheduler checks
 */
#define SCHED_CHECK_PID(pid, max) CHECK_RANGE(pid, 0, max, "scheduler PID")
#define SCHED_CHECK_PRIORITY(prio, min, max) CHECK_RANGE(prio, min, max, "scheduler priority")

/**
 * DRV_CHECK - Driver checks
 */
#define DRV_CHECK_IRQ(irq, max) CHECK_RANGE(irq, 0, max, "driver IRQ")
#define DRV_CHECK_PORT(port) CHECK_RANGE(port, 0, 0xFFFF, "driver I/O port")

// C++/RUST INTEROPERABILITY GUARDS

#ifdef __cplusplus
extern "C" {
#endif

// All functions remain C-callable even when included from C++

#ifdef __cplusplus
}

// C++-specific debug helpers
namespace ir0 {
namespace debug {
    
    // RAII-style scope guard for critical sections
    class ScopeGuard {
    public:
        explicit ScopeGuard(const char* name) : name_(name) {}
        ~ScopeGuard() {}
    private:
        const char* name_;
    };
    
} // namespace debug
} // namespace ir0

#endif // __cplusplus

// COMPATIBILITY NOTES

/*
 * These macros are designed to work with:
 * - Pure C code (current kernel)
 * - C++ kernel components (future)
 * - Rust FFI bindings (future)
 *
 * When calling from Rust:
 *   - All panicex calls are extern "C"
 *   - Use #[no_mangle] for FFI functions
 *   - panic_level_t maps to u32 in Rust
 */
