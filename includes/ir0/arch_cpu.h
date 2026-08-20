/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_cpu.h
 * Description: Portable CPU/TLS/MM activate facades (x86 FS / ARM TPIDR, CR3/TTBR).
 */

// ===============================================================================
// IR0 KERNEL PORTABLE ARCHITECTURE INTERFACE
// ===============================================================================
// This file provides a portable interface for all supported architectures
// All architecture-specific code should go through this interface

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/arch_config.h>

typedef uintptr_t arch_addr_t;
typedef uintptr_t arch_size_t;
typedef uint32_t arch_irq_t;
typedef uint32_t arch_flags_t;


/**
 * Initialize architecture-specific memory management (legacy hook; unused).
 */
void isa_memory_init(void);

/**
 * Get the start address of available physical memory (legacy hook; unused).
 */
arch_addr_t isa_get_memory_start(void);

/**
 * Get the end address of available physical memory (legacy hook; unused).
 */
arch_addr_t isa_get_memory_end(void);

/**
 * Allocate a physical page (legacy hook; unused — prefer PMM).
 */
arch_addr_t isa_alloc_phys_page(void);

/**
 * Free a physical page (legacy hook; unused — prefer PMM).
 */
void isa_free_phys_page(arch_addr_t addr);

/**
 * Map virtual address to physical address (legacy hook; unused — prefer mm/paging map_page).
 */
int isa_map_page(arch_addr_t virt, arch_addr_t phys, arch_flags_t flags);

/**
 * Unmap a virtual page (legacy hook; unused — prefer mm/paging unmap_page).
 */
int isa_unmap_page(arch_addr_t virt);

/**
 * Get page size for current architecture
 */
size_t get_page_size(void);

/**
 * Initialize architecture-specific interrupt system
 */
void interrupt_init(void);

/**
 * Compatibility alias for architecture IRQ initialization.
 */
void irq_init(void);

/**
 * Unmask architecture-specific IRQ lines needed for boot devices.
 */
void boot_irq_unmask(void);

/**
 * Enable interrupts globally
 */
void enable_interrupts(void);

/**
 * Disable interrupts globally
 */
void disable_interrupts(void);

/**
 * Register an interrupt handler
 */
int register_irq(arch_irq_t irq, void (*handler)(void));

/**
 * Unregister an interrupt handler
 */
int unregister_irq(arch_irq_t irq);

/**
 * Send end-of-interrupt signal
 */
void irq_eoi(arch_irq_t irq);

/**
 * Read from I/O port (x86) or MMIO address (ARM)
 */
uint8_t mmio_read8(arch_addr_t addr);

/**
 * Write to I/O port (x86) or MMIO address (ARM)
 */
void mmio_write8(arch_addr_t addr, uint8_t value);

/**
 * Read 16-bit from I/O port or MMIO address
 */
uint16_t mmio_read16(arch_addr_t addr);

/**
 * Write 16-bit to I/O port or MMIO address
 */
void mmio_write16(arch_addr_t addr, uint16_t value);

/**
 * Read 32-bit from I/O port or MMIO address
 */
uint32_t mmio_read32(arch_addr_t addr);

/**
 * Write 32-bit to I/O port or MMIO address
 */
void mmio_write32(arch_addr_t addr, uint32_t value);


/**
 * Halt the CPU (low power mode)
 */
void cpu_halt(void);

/**
 * Enter architecture-specific idle wait instruction.
 */
void cpu_idle(void);

/**
 * Platform final actions after coordinated shutdown (do not return).
 * Distinct from cpu_idle(): these stop the machine / CPU permanently.
 */
void system_halt(void) __attribute__((noreturn));
void system_reboot(void) __attribute__((noreturn));
void system_poweroff(void) __attribute__((noreturn));

/**
 * Get CPU ID (APIC ID)
 */
uint32_t get_cpu_id(void);

/**
 * Get number of CPUs
 */
uint32_t get_cpu_count(void);

/**
 * Get CPU vendor string
 * @vendor_buf: Buffer to store vendor string (must be at least 13 bytes)
 * Returns: 0 on success, -1 on failure
 */
int get_cpu_vendor(char *vendor_buf);

/**
 * Get CPU signature (family, model, stepping)
 * @family: Output for CPU family
 * @model: Output for CPU model
 * @stepping: Output for CPU stepping
 * Returns: 0 on success, -1 on failure
 */
int get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping);

/**
 * Get CPUID maximum leaf (EAX from CPUID.0)
 */
int get_cpuid_max_leaf(uint32_t *max_leaf);

/**
 * Get CPU brand string from silicon (CPUID 0x80000002-0x80000004)
 * @buf: Buffer, at least 49 bytes
 * @size: Buffer size
 */
int get_cpu_brand_string(char *buf, size_t size);

/**
 * Get CPU feature bits from CPUID.1 (EDX, ECX)
 */
int get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx);

/**
 * Hypervisor present (x86: CPUID.1 ECX bit 31). Bare metal → false.
 */
int arch_hypervisor_present(void);

/**
 * Copy hypervisor vendor from CPUID 0x40000000 (12 chars + NUL).
 * Returns 0 if present with vendor; -1 if bare metal / unavailable.
 */
int arch_hypervisor_vendor(char *buf, size_t n);

/**
 * Get CLFLUSH line size in bytes (from CPUID.1)
 */
uint32_t get_cpu_clflush_size(void);

/**
 * Switch to user mode (if supported)
 */
void switch_to_user(arch_addr_t entry, arch_addr_t stack);

/*
 * switch_to_user_task - Enter ring 3 with full task register state.
 * Linux ret_from_fork analogue for syscall-block resume paths.
 */
struct task;
void switch_to_user_task(const struct task *task);

/*
 * first_switch_to - First transfer from idle/boot into @next.
 * Does not return on success (iretq / EL drop / cooperative switch).
 * ISA details live in arch backends; portable sched must not embed iretq.
 */
struct process;
void first_switch_to(struct process *next);

/*
 * set_fs_base - Install the running thread's TLS base.
 * x86-64: IA32_FS_BASE. ARM64: TPIDR_EL0. Prefer set_tls() from portable code.
 */
void set_fs_base(uint64_t base);

/* Re-apply current_process TLS before returning to userspace. */
void arch_restore_user_fs_base(void);

/*
 * set_tls - Portable TLS base install (x86: FS base; ARM64: TPIDR_EL0).
 */
static inline void set_tls(uint64_t base)
{
	set_fs_base(base);
}

/*
 * tls_invalidate - Drop cached TLS view after task switch prep (no-op MSR).
 */
static inline void tls_invalidate(void)
{
}

/*
 * W10 multi-arch note (2026-07):
 * - TLS: use set_tls / tls_invalidate from portable paths (done).
 * - MM activate / TLB / irq save: use mm_activate / tlb_* / irq_save
 *   (F8-facade-mm). VA indices + PTE decode: mm_va_indices / mm_pte_*
 *   (Pack C). Table alloc/create path still mostly in mm/paging.c.
 * - Trap/context: x86-64 IDT + switch_x64.asm stay arch-local; portable code
 *   uses simple facades (switch_to, irq_save, …) — no CPUID in kernel/syscalls.
 */

/**
 * Activate address-space root (x86: CR3; ARM64: TTBR0). Neutral name — no ISA in callers.
 */
void mm_activate(uintptr_t root);

/** Read current address-space root from hardware. */
uintptr_t mm_current_root(void);

/** Invalidate one VA in the local TLB. */
void tlb_invalidate_page(uintptr_t va);

/** Invalidate all non-global TLB entries (local CPU). */
void tlb_invalidate_all(void);

/**
 * Save IRQ state and disable IRQs. Restore with irq_restore.
 * Portable replacement for pushfq/cli copies in kernel/sched.
 */
unsigned long irq_save(void);

/** Restore IRQ state from irq_save. */
void irq_restore(unsigned long flags);

/**
 * MM control registers behind a neutral API (W10b partial).
 * ctrl0: x86 CR0; ARM64 SCTLR_EL1 (bit0 M ≈ paging enabled).
 * ctrl1: x86 CR4; ARM64 reserved/0 until a portable need exists.
 */
uint64_t mm_read_ctrl0(void);
void mm_write_ctrl0(uint64_t value);
uint64_t mm_read_ctrl1(void);

/**
 * 4-level VA indices (9 bits each) for 4 KiB granules — x86-64 and aarch64.
 * idx[0]=L0/PML4, idx[1]=L1/PDPT, idx[2]=L2/PD, idx[3]=L3/PT.
 */
void mm_va_indices(uintptr_t va, size_t idx[4]);

/** PTE/table descriptor decode (ISA-local present / large / phys). */
int mm_pte_present(uint64_t e);
int mm_pte_large(uint64_t e);
uintptr_t mm_pte_phys(uint64_t e);

/**
 * Build non-leaf table PTE (present+RW[+user]; never NX on table levels).
 * Leaf PTE: present + low 12 flag bits; NX when exec==0 (x86).
 */
uint64_t mm_make_table_pte(uintptr_t phys, int user);
uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec);
/** OR user-accessible bit onto an existing table descriptor. */
void mm_pte_set_user(uint64_t *e);

/*
 * get_fs_base - Read the running thread's TLS base (x86 FS / ARM TPIDR_EL0).
 */
uint64_t get_fs_base(void);

/**
 * Get current CPU mode
 */
uint32_t get_cpu_mode(void);

/**
 * Initialize architecture-specific timer
 */
void timer_init(void);

/**
 * Initialize architecture-specific syscall entry plumbing.
 */
void syscall_init(void);

/**
 * Get current timer value
 */
uint64_t timer_read(void);

/**
 * Set timer frequency
 */
void timer_set_frequency(uint32_t hz);

/**
 * Get timer frequency
 */
uint32_t timer_get_frequency(void);

/**
 * Get fault address (for page faults, etc.)
 */
arch_addr_t get_fault_address(void);

/**
 * Get fault type
 */
uint32_t get_fault_type(void);

/**
 * Get fault error code
 */
uint32_t get_fault_error(void);

/**
 * Dump CPU registers
 */
void dump_cpu_registers(void);

/**
 * Early architecture initialization
 * Sets up core architecture features (GDT/TSS for x86, etc.)
 */
void early_init(void);

/**
 * Initialize architecture-specific interrupt system
 * Sets up interrupt tables and controllers (IDT/PIC for x86, GIC for ARM, etc.)
 */
void interrupt_init(void);

/**
 * Late architecture initialization
 */
void late_init(void);

/**
 * Get boot parameters
 */
void *get_boot_params(void);

/**
 * Set boot parameters from architecture-specific entry code.
 */
void set_boot_params(void *params);

/**
 * Get command line arguments
 */
const char *get_cmdline(void);

/**
 * Get architecture name
 */
const char *get_arch_name(void);

/**
 * uname(2) machine field (x86_64 / aarch64 / …).
 */
const char *get_arch_uname_machine(void);

typedef enum
{
	ARCH_CLOCK_UNAVAILABLE = 0,
	ARCH_CLOCK_RAW = 1,
	ARCH_CLOCK_CALIBRATED = 2,
	ARCH_CLOCK_MONOTONIC = 3
} arch_clock_quality_t;

/*
 * Earliest architecture counter. RAW means ordered ticks only: callers must
 * not render duration until the common monotonic clock is calibrated.
 */
int arch_early_clock_available(void);
uint64_t arch_early_clock_read(void);
arch_clock_quality_t arch_early_clock_quality(void);

/**
 * Get architecture bits (32/64)
 */
uint32_t get_arch_bits(void);

/**
 * Check if architecture supports specific feature
 */
int supports_feature(const char *feature);

/**
 * Get architecture-specific compiler flags
 */
const char *get_arch_cflags(void);

/**
 * Get architecture-specific linker flags
 */
const char *get_arch_ldflags(void);
