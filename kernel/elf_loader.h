/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: elf_loader.h
 * Description: IR0 kernel source/header file
 */

// kernel/elf_loader.h - Header para ELF Loader básico

#pragma once

#include <stdint.h>
#include <stddef.h>

// Forward declaration
struct process;

// ===============================================================================
// FUNCIONES PRINCIPALES
// ===============================================================================

/**
 * Cargar programa ELF en memoria de proceso
 * @param path Ruta del archivo ELF
 * @param process Proceso donde cargar el programa
 * @return 0 en éxito, -1 en error
 */
int load_elf_program(const char *path, struct process *process);

/**
 * kexecve - Kernel-level ELF binary loader and executor
 * @path: Path to ELF executable file
 * @argv: Array of command line arguments (NULL-terminated)
 * @envp: Array of environment variables (NULL-terminated)
 *
 * Main function to load and execute ELF binaries. Creates a process,
 * loads segments, and schedules for execution. This is the kernel-level
 * equivalent of the execve() syscall, used for loading user programs.
 *
 * Algorithm:
 * 1. Read ELF file from filesystem via VFS
 * 2. Validate ELF header (magic, architecture, type)
 * 3. Create process structure with proper page directory
 * 4. Load ELF segments into memory at virtual addresses
 * 5. Set up entry point, stack with argc/argv/envp, and registers
 * 6. Add process to scheduler for execution
 *
 * Returns: Process PID on success, -1 on error
 */
int kexecve(const char *path, char *const argv[], char *const envp[]);

/**
 * exec_replace_current - Reload ELF into current user process (same PID).
 * Does not return on success.
 *
 * Ownership: on the success path the callee releases @argv and @envp with
 * kfree() before entering the new image, because the caller never regains
 * control to do it. They must therefore be kmalloc'd, NULL-terminated
 * vectors. On failure the vectors are untouched and the caller frees them.
 */
int exec_replace_current(const char *path, char *const argv[], char *const envp[]);

/* Backwards-compatible alias - deprecated, use kexecve() instead */
static inline int elf_load_and_execute(const char *path) __attribute__((deprecated));
static inline int elf_load_and_execute(const char *path)
{
    return kexecve(path, NULL, NULL);
}

// ===============================================================================
// FUNCIONES DE DEBUG
// ===============================================================================

/**
 * Mostrar información del header ELF
 * @param header Puntero al header ELF
 */
void debug_elf_header(const void *header);

/**
 * Mostrar información de program header
 * @param phdr Puntero al program header
 * @param index Índice del program header
 */
void debug_program_header(const void *phdr, int index);

// ===============================================================================
// CONSTANTES ELF
// ===============================================================================

#define ELF_MAGIC 0x464C457F // "\x7fELF"
#define PT_LOAD 1            // Segmento cargable
#define PF_R 4               // Readable
#define PF_W 2               // Writable
#define PF_X 1               // Executable

// ===============================================================================
// ESTRUCTURAS ELF (para uso externo)
// ===============================================================================

// ELF Header (64-bit)
typedef struct
{
    unsigned char e_ident[16]; // Magic number y info
    uint16_t e_type;           // Tipo de archivo
    uint16_t e_machine;        // Arquitectura
    uint32_t e_version;        // Versión ELF
    uint64_t e_entry;          // Entry point
    uint64_t e_phoff;          // Offset de program headers
    uint64_t e_shoff;          // Offset de section headers
    uint32_t e_flags;          // Flags
    uint16_t e_ehsize;         // Tamaño del header
    uint16_t e_phentsize;      // Tamaño de program header
    uint16_t e_phnum;          // Número de program headers
    uint16_t e_shentsize;      // Tamaño de section header
    uint16_t e_shnum;          // Número de section headers
    uint16_t e_shstrndx;       // Índice de string table
} elf64_header_t;

// Program Header (64-bit)
typedef struct
{
    uint32_t p_type;   // Tipo de segmento
    uint32_t p_flags;  // Flags
    uint64_t p_offset; // Offset en archivo
    uint64_t p_vaddr;  // Dirección virtual
    uint64_t p_paddr;  // Dirección física
    uint64_t p_filesz; // Tamaño en archivo
    uint64_t p_memsz;  // Tamaño en memoria
    uint64_t p_align;  // Alineación
} elf64_phdr_t;
