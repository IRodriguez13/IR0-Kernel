/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: elf_loader.c
 * Description: ELF binary loader for user programs with segment loading and process creation
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process.h"
#include <ir0/arch_task.h>
#include <ir0/arch_elf.h>
#include <ir0/task_ops.h>
#include <ir0/sched.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ir0/kmem.h>
#include <fs/vfs.h>
#include <ir0/ktm/klog.h>
#include <kernel/process.h>
#include <mm/paging.h>
#include <mm/pmm.h>
#include <ir0/copy_user.h>
#include <ir0/debug_trap.h>
#include <ir0/oops.h>
#include <ir0/arch_port.h>
#include <ir0/signals.h>
#include <ir0/ktm/user_canary.h>
#include <ir0/console.h>
#include <ir0/paging.h>
#include <ir0/tls.h>
#include <ir0/context.h>
#include <ir0/chmod.h>
#include <ir0/credentials.h>
#include <ir0/permissions.h>
#include <config.h>
#include <ir0/ktm/fault.h>
#include <ir0/vdso.h>
#include <ir0/errno.h>
#include <errno.h>

#define EXEC_SHEBANG_MAX_DEPTH 4
#define EXEC_SHEBANG_LINE_MAX  127

/* Compiler optimization hints */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Use external strstr from string.h */

/* Minimal ELF structures */
typedef struct
{
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_header_t;

typedef struct
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

/* ELF constants */
#define ELF_MAGIC_0 0x7f
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELFCLASS64 2
#define ET_EXEC 2
#define ET_DYN  3
#define PT_LOAD 1

/* Maximum program headers processed per executable (bounds cost and table size) */
#define ELF_MAX_PHNUM 64

/* Cap argv/envp enumeration so hostile or buggy user stacks cannot unbounded-scan */
#define ELF_ARG_MAX 256

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_BASE   7
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_FLAGS  24
#define AT_RANDOM 25
#define AT_SYSINFO_EHDR 33
#define PT_PHDR   6

#define ELF_AUXV_PAIRS 16
#define ELF_AT_RANDOM_BYTES 16
#define AT_CLKTCK_VALUE 100

static void elf_trace_argv_contract(process_t *proc, const char *image_path,
                                    const char *stage)
{
    (void)proc;
    (void)image_path;
    (void)stage;
    return;
}

static void elf_trace_entry_stack_layout(process_t *proc, const elf64_header_t *header,
                                         uint64_t at_phdr, uint64_t at_base,
                                         const char *stage)
{
    (void)proc;
    (void)header;
    (void)at_phdr;
    (void)at_base;
    (void)stage;
    return;
}

static int validate_elf_header(const elf64_header_t *header)
{
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3)
        return 0;
    if (header->e_ident[4] != ELFCLASS64 ||
        !elf_machine_supported(header->e_machine))
        return 0;
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN)
        return 0;
    return 1;
}

static int exec_buffer_is_elf(const void *data, size_t size)
{
    if (!data || size < 4)
        return 0;
    {
        const uint8_t *b = (const uint8_t *)data;

        return b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F';
    }
}

/*
 * Parse #! interpreter line. Returns 1 on success, 0 if not a shebang,
 * -ENOEXEC on malformed script header.
 */
static int exec_parse_shebang_line(const void *data, size_t size,
                                   char *interp, size_t interp_sz,
                                   char *iarg, size_t iarg_sz)
{
    const char *p = (const char *)data;
    const char *end = p + size;
    const char *line_end;
    const char *path_end;
    size_t path_len;
    size_t arg_len;

    if (!data || size < 2 || p[0] != '#' || p[1] != '!')
        return 0;

    if (interp_sz == 0 || iarg_sz == 0)
        return -ENOEXEC;

    interp[0] = '\0';
    iarg[0] = '\0';

    p += 2;
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;

    line_end = p;
    while (line_end < end && *line_end != '\n' && *line_end != '\r' && *line_end != '\0')
        line_end++;

    if (p >= line_end)
        return -ENOEXEC;

    path_end = line_end;
    while (path_end > p && (path_end[-1] == ' ' || path_end[-1] == '\t'))
        path_end--;

    {
        const char *sp = p;

        while (sp < path_end && *sp != ' ' && *sp != '\t')
            sp++;
        path_len = (size_t)(sp - p);
        if (path_len == 0 || path_len >= interp_sz)
            return -ENOEXEC;
        memcpy(interp, p, path_len);
        interp[path_len] = '\0';

        if (sp < path_end)
        {
            while (sp < path_end && (*sp == ' ' || *sp == '\t'))
                sp++;
            arg_len = (size_t)(path_end - sp);
            if (arg_len >= iarg_sz)
                return -ENOEXEC;
            if (arg_len > 0)
            {
                memcpy(iarg, sp, arg_len);
                iarg[arg_len] = '\0';
            }
        }
    }

    return 1;
}

static char *exec_strdup_kern(const char *s)
{
    size_t n;
    char *d;

    if (!s)
        return NULL;
    n = strlen(s) + 1;
    d = kmalloc(n);
    if (!d)
        return NULL;
    memcpy(d, s, n);
    return d;
}

static void exec_release_string_vector(char *const vec[])
{
	int i;

	if (!vec)
		return;

	for (i = 0; i < 256 && vec[i]; i++)
		kfree((void *)(uintptr_t)vec[i]);
}

static char **exec_build_shebang_argv(const char *interp, const char *iarg,
                                      const char *script_path,
                                      char *const orig_argv[])
{
    int origc = 0;
    int idx = 0;
    int i;
    char **nv;

    if (orig_argv)
    {
        while (orig_argv[origc])
            origc++;
    }

    nv = kmalloc((size_t)(origc + 4) * sizeof(char *));
    if (!nv)
        return NULL;

    nv[idx++] = exec_strdup_kern(interp);
    if (!nv[0])
        goto fail;
    if (iarg && iarg[0])
    {
        nv[idx++] = exec_strdup_kern(iarg);
        if (!nv[idx - 1])
            goto fail;
    }
    nv[idx++] = exec_strdup_kern(script_path);
    if (!nv[idx - 1])
        goto fail;
    for (i = 1; i < origc; i++)
    {
        nv[idx++] = exec_strdup_kern(orig_argv[i]);
        if (!nv[idx - 1])
            goto fail;
    }
    nv[idx] = NULL;
    return nv;

fail:
    exec_release_string_vector(nv);
    kfree(nv);
    return NULL;
}

static uint64_t elf_compute_load_base(const elf64_header_t *header, const uint8_t *file_data)
{
    uint16_t phnum = header->e_phnum;
    uint64_t base = (uint64_t)-1;
    elf64_phdr_t *phdr;
    int i;

    if (phnum > ELF_MAX_PHNUM)
        phnum = ELF_MAX_PHNUM;

    if (header->e_phoff > (uint64_t)-1 || !file_data)
        return 0;

    phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    for (i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        if (phdr[i].p_vaddr < base)
            base = phdr[i].p_vaddr;
    }

    return (base == (uint64_t)-1) ? 0 : base;
}

/* Page-aligned end of last PT_LOAD — Linux-style initial program break. */
static uint64_t elf_compute_initial_brk(const elf64_header_t *header, const uint8_t *file_data)
{
	uint16_t phnum = header->e_phnum;
	elf64_phdr_t *phdr;
	uint64_t brk = 0;
	int i;

	if (phnum > ELF_MAX_PHNUM)
		phnum = ELF_MAX_PHNUM;
	if (!file_data)
		return 0;
	phdr = (elf64_phdr_t *)(file_data + header->e_phoff);
	for (i = 0; i < (int)phnum; i++)
	{
		uint64_t end;

		if (phdr[i].p_type != PT_LOAD)
			continue;
		end = phdr[i].p_vaddr + phdr[i].p_memsz;
		end = (end + 0xFFFULL) & ~0xFFFULL;
		if (end > brk)
			brk = end;
	}
	/*
	 * Initial break is the first page after the last PT_LOAD (Linux).
	 * Do not jump to USER_HEAP_BASE: that leaves a multi‑hundred‑MiB hole
	 * between &_end and the break which musl/BusyBox may touch as heap
	 * without a brk/mmap (present=1 SEGV on supervisor identity, e.g.
	 * addr=0x1020000 in hexdump|head). Growth through the identity window
	 * is handled by map_page split_huge + COW (see page_fault).
	 */
	return brk;
}

static uint64_t elf_compute_at_phdr(const elf64_header_t *header, const uint8_t *file_data)
{
    uint16_t phnum = header->e_phnum;
    elf64_phdr_t *phdr;
    int i;

    if (phnum > ELF_MAX_PHNUM)
        phnum = ELF_MAX_PHNUM;

    if (header->e_phoff > (uint64_t)-1 || !file_data)
        return 0;

    phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    for (i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type == PT_PHDR)
            return phdr[i].p_vaddr;
    }

    for (i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        if (header->e_phoff >= phdr[i].p_offset &&
            header->e_phoff < phdr[i].p_offset + phdr[i].p_filesz)
            return phdr[i].p_vaddr + (header->e_phoff - phdr[i].p_offset);
    }

    return 0;
}

static void elf_fill_random_bytes(uint8_t *buf, size_t len)
{
    uint64_t tsc;
    size_t i;

    tsc = timer_read();
    if (tsc == 0)
        tsc = 0x5851f42d4c957f2dULL;

    for (i = 0; i < len; i++)
    {
        tsc = tsc * 6364136223846793005ULL + 1ULL;
        buf[i] = (uint8_t)(tsc >> 33);
    }
}

/* Load ELF segments into memory at correct virtual addresses */
static int elf_load_segments(elf64_header_t *header, uint8_t *file_data, size_t file_size,
                             process_t *process)
{
    uint16_t phnum = header->e_phnum;
    if (phnum > ELF_MAX_PHNUM)
        phnum = ELF_MAX_PHNUM;

    if (header->e_phoff > file_size)
    {
        klog_debug("ELF", "SERIAL: ELF: Program header table offset out of bounds\n");
        return -1;
    }

    {
        uint64_t ph_bytes = (uint64_t)phnum * sizeof(elf64_phdr_t);
        if (ph_bytes > (uint64_t)file_size - header->e_phoff)
        {
            klog_debug("ELF", "SERIAL: ELF: Program header table extends past file\n");
            return -1;
        }
    }

    elf64_phdr_t *phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    klog_debug_fmt("ELF", "SERIAL: ELF: Loading %x program segments\n", (unsigned)(phnum));

    /* Get process page directory */
    uint64_t *pml4 = process_pgd(process);
    if (!pml4)
    {
        klog_debug("ELF", "SERIAL: ELF: Process has no page directory\n");
        return -1;
    }

    /*
     * Phase 1 — map all PT_LOAD regions under kernel CR3.  Page-table
     * allocation uses the kernel heap and must not run with child CR3 active.
     */
    for (int i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        if (phdr[i].p_memsz < phdr[i].p_filesz)
        {
            klog_debug("ELF", "SERIAL: ELF: PT_LOAD p_memsz < p_filesz\n");
            return -1;
        }

        if (phdr[i].p_filesz > 0)
        {
            if (phdr[i].p_offset > file_size ||
                phdr[i].p_filesz > (uint64_t)file_size - phdr[i].p_offset)
            {
                klog_debug("ELF", "SERIAL: ELF: PT_LOAD segment file range out of bounds\n");
                return -1;
            }
        }

        klog_debug_fmt("ELF", "SERIAL: ELF: Mapping segment %x at vaddr 0x%x size 0x%x", (unsigned)(i), (unsigned)((uint32_t)phdr[i].p_vaddr), (unsigned)((uint32_t)phdr[i].p_memsz));

        {
            uint64_t memsz = phdr[i].p_memsz;
            uintptr_t vaddr = phdr[i].p_vaddr;
            uintptr_t vaddr_aligned = vaddr & ~0xFFF;
            size_t size_aligned = ((vaddr + memsz + 0xFFF) & ~0xFFF) - vaddr_aligned;
            uint64_t flags = PAGE_USER;

            if (phdr[i].p_flags & 2)
                flags |= PAGE_RW;
            if (phdr[i].p_flags & 1)
                flags |= PAGE_EXEC;

            if (map_user_region_in_directory(pml4, vaddr_aligned, size_aligned, flags) != 0)
            {
                klog_debug("ELF", "SERIAL: ELF: Failed to map user memory region\n");
                return -1;
            }
        }
    }

    /*
     * Phase 2 — copy segment bytes via physical frames (kernel CR3).
     */
    for (int i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        {
            uintptr_t vaddr = phdr[i].p_vaddr;

            if (phdr[i].p_filesz > 0)
            {
                if (copy_to_user_region_in_directory(pml4, vaddr,
                        file_data + phdr[i].p_offset,
                        (size_t)phdr[i].p_filesz) != 0)
                {
                    klog_debug("ELF", "SERIAL: ELF: Failed to copy segment data\n");
                    return -1;
                }
                klog_debug_fmt("ELF", "SERIAL: ELF: Copied %x bytes from file to vaddr 0x%x", (unsigned)((uint32_t)phdr[i].p_filesz), (unsigned)((uint32_t)vaddr));
            }

            if (phdr[i].p_memsz > phdr[i].p_filesz)
            {
                if (zero_user_region_in_directory(pml4,
                        vaddr + phdr[i].p_filesz,
                        (size_t)(phdr[i].p_memsz - phdr[i].p_filesz)) != 0)
                {
                    klog_debug("ELF", "SERIAL: ELF: Failed to zero BSS\n");
                    return -1;
                }
                klog_debug("ELF", "SERIAL: ELF: Zeroed BSS section\n");
            }
        }
    }

    return 0;
}

/* Dummy entry: spawn() needs a function pointer; ELF overwrites the IP. */
static void elf_dummy_entry(void)
{
    /* This should never be called as we override the task IP */
    panic("ELF dummy entry should never be called :)\n");
}

/* Create a new process for the ELF program */
static process_t *elf_create_process(elf64_header_t *header, const char *path)
{
    const char *full_path = path;

    klog_debug_fmt("ELF", "SERIAL: ELF: Creating process for %s with entry point 0x%x", path, (unsigned)((uint32_t)header->e_entry));

    /* Extract basename for process name */
    const char *basename = path;
    const char *last_slash = path;
    while (*path) {
        if (*path == '/') last_slash = path + 1;
        path++;
    }
    basename = last_slash;

    /* Create user process using spawn (creates isolated page directory) */
    /* Use dummy entry that will be overridden with ELF entry point */
    /* ELF binaries always run in USER_MODE */
    pid_t pid = spawn_user(elf_dummy_entry, basename);
    if (pid < 0)
    {
        klog_debug("ELF", "SERIAL: ELF: Failed to create process\n");
        return NULL;
    }

    /* Find the created process */
    process_t *process = process_find_by_pid(pid);
    if (process)
    {
        /* spawn_user only carries the basename; /proc/<pid>/exe needs the path. */
        strncpy(process->exe_path, full_path, sizeof(process->exe_path) - 1);
        process->exe_path[sizeof(process->exe_path) - 1] = '\0';
    }
    if (!process)
    {
        klog_debug("ELF", "SERIAL: ELF: Failed to find created process\n");
        return NULL;
    }

    /*
     * spawn() enqueues the task; keep it off the run queue until PT_LOAD
     * segments and stack are fully initialized (timer IRQ may preempt kmain).
     */
    sched_remove_process(process);

    /* Set up user mode execution */
    /* spawn() already set up the process structure correctly */
    /* We just need to set the entry point (will be done after loading segments) */
    
    /* Set entry point (will be adjusted after segments are loaded) */
    task_set_ip(&process->task, header->e_entry);
    task_set_user_segments(&process->task);

    /* Stack window is USER_STACK_TOP (spawn_user / process_set_stack_layout). */

    /* Enable interrupts in user mode */
    task_set_flags(&process->task, task_initial_user_status());

    klog_debug_fmt("ELF", "SERIAL: ELF: Process created with PID %x", (unsigned)(process->task.pid));
    klog_debug_fmt("ELF", "SERIAL: ELF: Entry point: 0x%x", (unsigned)((uint32_t)task_get_ip(&process->task)));
    klog_debug_fmt("ELF", "SERIAL: ELF: Stack: 0x%x", (unsigned)((uint32_t)task_get_sp(&process->task)));

    return process;
}

/**
 * elf_setup_stack - argc/argv/envp + auxv (SysV ABI stack; ISA-neutral).
 * @process: Process to set up stack for
 * @argv: Command line arguments (NULL-terminated)
 * @envp: Environment variables (NULL-terminated)
 *
 * Stack: argc, argv[], envp[], auxv, strings.
 * Entry registers (task_set_arg0/1/2): argc, argv, envp.
 */
static int elf_setup_stack(process_t *process, char *const argv[], char *const envp[],
                           const elf64_header_t *header, uint64_t at_phdr,
                           uint64_t at_base, const char *builder_tag,
                           const char *image_path)
{
    int vdso_ok;

    if (!process || process->mode != USER_MODE)
        return -1;

    vdso_ok = (vdso_map(process) == 0);
    if (!vdso_ok)
    {
        klog_debug("ELF", "SERIAL: ELF: vDSO map failed (continuing without AT_SYSINFO_EHDR)\n");
    }

    (void)builder_tag;

    /* Count arguments (cap to ELF_ARG_MAX) */
    int argc = 0;
    if (argv)
    {
        while (argc < ELF_ARG_MAX && argv[argc])
            argc++;
    }

    /* Count environment variables (cap to ELF_ARG_MAX) */
    int envc = 0;
    if (envp)
    {
        while (envc < ELF_ARG_MAX && envp[envc])
            envc++;
    }
    
    /* Calculate total stack size needed */
    size_t strings_size = 0;
    
    /* Calculate argv strings size */
    for (int i = 0; i < argc; i++)
    {
        if (argv[i])
            strings_size += strlen(argv[i]) + 1;
    }
    
    /* Calculate envp strings size */
    for (int i = 0; i < envc; i++)
    {
        if (envp[i])
            strings_size += strlen(envp[i]) + 1;
    }
    
    /* Total size: argc slot + argv[] + envp[] + auxv[] + strings + alignment */
    size_t stack_size = sizeof(uint64_t) +
                       (argc + 1) * sizeof(uint64_t) +
                       (envc + 1) * sizeof(uint64_t) +
                       ELF_AUXV_PAIRS * 2 * sizeof(uint64_t) +
                       ELF_AT_RANDOM_BYTES +
                       strings_size +
                       16;
    
    /* Leave 256 bytes margin for safety */
    size_t stack_margin = 256;
    if (stack_size > (process_stack_size(process) - stack_margin))
    {
        klog_debug_fmt("ELF", "SERIAL: ELF: ERROR - Stack too small for arguments (need %x bytes, have %x)\n", (unsigned)((uint32_t)stack_size), (unsigned)((uint32_t)process_stack_size(process)));
        return -ENOMEM;
    }
    
    /* Switch to process page directory temporarily (after kernel heap allocs) */
    uint64_t *argv_ptrs = (uint64_t *)kmalloc((argc + 1) * sizeof(uint64_t));
    uint64_t *envp_ptrs = (uint64_t *)kmalloc((envc + 1) * sizeof(uint64_t));

    if (!argv_ptrs || !envp_ptrs)
    {
        if (argv_ptrs)
            kfree(argv_ptrs);
        if (envp_ptrs)
            kfree(envp_ptrs);
        return -1;
    }

    /*
     * A NULL entry inside argv/envp leaves its slot untouched by the copy
     * loops below, and kmalloc does not zero: without this the user-visible
     * argv[] would carry a stale kernel-heap qword as a string pointer.
     */
    memset(argv_ptrs, 0, (size_t)(argc + 1) * sizeof(uint64_t));
    memset(envp_ptrs, 0, (size_t)(envc + 1) * sizeof(uint64_t));

    uint64_t stack_top = process_stack_start(process) + process_stack_size(process);
    uint64_t stack_base = stack_top - stack_size;
    uint64_t argc_slot;
    uint64_t argv_array;
    uint64_t envp_array;
    uint64_t auxv_base;
    uint64_t random_base;
    uint64_t strings_base;
    uint64_t current_string_ptr;
    uint64_t *pml4 = process_pgd(process);

    stack_base &= ~0xFULL;

    argc_slot = stack_base;
    argv_array = argc_slot + sizeof(uint64_t);
    envp_array = argv_array + (argc + 1) * sizeof(uint64_t);
    auxv_base = envp_array + (envc + 1) * sizeof(uint64_t);
    random_base = auxv_base + ELF_AUXV_PAIRS * 2 * sizeof(uint64_t);
    strings_base = random_base + ELF_AT_RANDOM_BYTES;

    /* Copy argv/env strings into userspace */
    current_string_ptr = strings_base;

    for (int i = 0; i < argc; i++)
    {
        if (argv[i])
        {
            size_t len = strlen(argv[i]) + 1;

            if (copy_to_user_region_in_directory(pml4, current_string_ptr,
                                                 argv[i], len) != 0)
            {
                kfree(argv_ptrs);
                kfree(envp_ptrs);
                return -1;
            }
            argv_ptrs[i] = current_string_ptr;
            current_string_ptr += len;
        }
    }

    for (int i = 0; i < envc; i++)
    {
        if (envp[i])
        {
            size_t len = strlen(envp[i]) + 1;

            if (copy_to_user_region_in_directory(pml4, current_string_ptr,
                                                 envp[i], len) != 0)
            {
                kfree(argv_ptrs);
                kfree(envp_ptrs);
                return -1;
            }
            envp_ptrs[i] = current_string_ptr;
            current_string_ptr += len;
        }
    }

    /* argc at [RSP+0] per SysV ABI process entry stack contract. */
    {
        uint64_t argc_q = (uint64_t)argc;
        if (copy_to_user_region_in_directory(pml4, argc_slot, &argc_q, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* argv[] */
    for (int i = 0; i < argc; i++)
    {
        uint64_t ptr = argv_ptrs[i];

        if (copy_to_user_region_in_directory(pml4,
                argv_array + (size_t)i * sizeof(uint64_t),
                &ptr, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    {
        uint64_t zero = 0;

        if (copy_to_user_region_in_directory(pml4,
                argv_array + (size_t)argc * sizeof(uint64_t),
                &zero, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* envp[] */
    for (int i = 0; i < envc; i++)
    {
        uint64_t ptr = envp_ptrs[i];

        if (copy_to_user_region_in_directory(pml4,
                envp_array + (size_t)i * sizeof(uint64_t),
                &ptr, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    {
        uint64_t zero = 0;

        if (copy_to_user_region_in_directory(pml4,
                envp_array + (size_t)envc * sizeof(uint64_t),
                &zero, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* 16-byte AT_RANDOM seed on stack */
    {
        uint8_t random_seed[ELF_AT_RANDOM_BYTES];

        elf_fill_random_bytes(random_seed, sizeof(random_seed));
        if (copy_to_user_region_in_directory(pml4, random_base, random_seed,
                                             sizeof(random_seed)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* auxv[] — musl walks past envp NULL to find these */
    {
        uint64_t at_secure = process ? process->at_secure : 0;
        uint64_t at_vdso = vdso_ok ? vdso_ehdr() : 0;

        struct
        {
            uint64_t a_type;
            uint64_t a_val;
        } auxv[ELF_AUXV_PAIRS] = {
            { AT_PHDR, at_phdr },
            { AT_PHENT, header ? header->e_phentsize : 0 },
            { AT_PHNUM, header ? header->e_phnum : 0 },
            { AT_UID, process->uid },
            { AT_EUID, process->euid },
            { AT_GID, process->gid },
            { AT_EGID, process->egid },
            { AT_CLKTCK, AT_CLKTCK_VALUE },
            { AT_SECURE, at_secure },
            { AT_FLAGS, 0 },
            { AT_RANDOM, random_base },
            { AT_PAGESZ, 4096 },
            { AT_BASE, at_base },
            { AT_ENTRY, task_get_ip(&process->task) },
            { AT_SYSINFO_EHDR, at_vdso },
            { AT_NULL, 0 },
        };
        size_t i;

        for (i = 0; i < ELF_AUXV_PAIRS; i++)
        {
            uint64_t off = i * 2 * sizeof(uint64_t);

            if (copy_to_user_region_in_directory(pml4, auxv_base + off,
                    &auxv[i].a_type, sizeof(uint64_t)) != 0 ||
                copy_to_user_region_in_directory(pml4,
                    auxv_base + off + sizeof(uint64_t),
                    &auxv[i].a_val, sizeof(uint64_t)) != 0)
            {
                kfree(argv_ptrs);
                kfree(envp_ptrs);
                return -1;
            }
        }
    }

    /*
     * The image is laid out below stack_top with 16 bytes of slack left over
     * (the +16 in stack_size, plus whatever the 16-byte align-down of
     * stack_base adds). Stamp it so an overwrite is caught where it happens
     * instead of surfacing later as a bad length or a stale pointer.
     */
    ktm_user_canary_install(pml4, stack_top, (uint32_t)process->task.pid);

    task_set_sp(&process->task, argc_slot);
    task_set_frame_pointer(&process->task, argc_slot);

    /* SysV entry: arg0=argc, arg1=argv, arg2=envp (x86 rdi/rsi/rdx, ARM x0/x1/x2). */
    task_set_arg0(&process->task, (uint64_t)argc);
    task_set_arg1(&process->task, argv_array);
    task_set_arg2(&process->task, envp_array);

    kfree(argv_ptrs);
    kfree(envp_ptrs);
    
    klog_debug_fmt("ELF", "SERIAL: ELF: Stack initialized: argc=%x, argv=%x, envp=%x", (unsigned)(argc), (unsigned)((uint32_t)argv_array), (unsigned)((uint32_t)envp_array));
    elf_trace_argv_contract(process, image_path, "stack-builder-final");
    elf_trace_entry_stack_layout(process, header, at_phdr, at_base, "elf_setup_stack-final");

    {
        int env_rc = process_saved_environ_set(process, envp);

        if (env_rc < 0)
            return env_rc;
    }

    return 0;
}

/**
 * kexecve - Load and execute ELF binary (kernel-level exec)
 * @path: Path to ELF executable file
 * @argv: Command line arguments (NULL-terminated, can be NULL)
 * @envp: Environment variables (NULL-terminated, can be NULL)
 *
 * This function loads an ELF binary from the filesystem, creates a process,
 * maps the segments into memory, and schedules the process for execution.
 * This is the kernel-level equivalent of execve() syscall.
 *
 * Algorithm:
 * 1. Read ELF file from filesystem via VFS
 * 2. Validate ELF header (magic, architecture, type)
 * 3. Create process structure with proper page directory
 * 4. Load ELF segments into memory at virtual addresses
 * 5. Set up entry point, stack with argc/argv/envp, and registers
 * 6. Free the in-kernel ELF buffer and enqueue the process for execution
 *
 * Returns: Process PID on success, -1 on error
 *
 * Thread safety: NOT thread-safe - should be called from process context
 */
static int kexecve_depth(const char *path, char *const argv[], char *const envp[],
                         int shebang_depth)
{
    /* Step 1: Read the ELF file from filesystem */
    void *file_data = NULL;
    size_t file_size = 0;
    int result;
    process_t *process;
    elf64_header_t *header;
    uint64_t at_phdr;
    uint64_t at_base;
    result = vfs_read_file(path, &file_data, &file_size);
    if (result != 0 || !file_data)
    {
        klog_debug("ELF", "SERIAL: ELF: ERROR - Failed to read file from filesystem\n");
        return -1;
    }

    klog_debug_fmt("ELF", "SERIAL: ELF: File loaded successfully, size: %x bytes\n", (unsigned)(file_size));

    if (!exec_buffer_is_elf(file_data, file_size))
    {
        char interp[256];
        char iarg[128];
        int sh_rc;

        sh_rc = exec_parse_shebang_line(file_data, file_size, interp,
                                        sizeof(interp), iarg, sizeof(iarg));
        kfree(file_data);
        file_data = NULL;

        if (sh_rc == 0)
            return -ENOEXEC;
        if (sh_rc < 0)
            return sh_rc;
        if (shebang_depth >= EXEC_SHEBANG_MAX_DEPTH)
            return -ENOEXEC;
        {
            char **new_argv = exec_build_shebang_argv(interp,
                                                      iarg[0] ? iarg : NULL,
                                                      path, argv);
            int rc;

            if (!new_argv)
                return -ENOMEM;
            rc = kexecve_depth(interp, new_argv, envp, shebang_depth + 1);
            exec_release_string_vector(new_argv);
            kfree(new_argv);
            return rc;
        }
    }

    /* Step 2: Validate ELF header */
    if (!validate_elf_header((elf64_header_t *)file_data))
    {
        klog_debug("ELF", "SERIAL: ELF: ERROR - Invalid ELF header\n");
        kfree(file_data);
        return -ENOEXEC;
    }

    header = (elf64_header_t *)file_data;
    klog_debug("ELF", "SERIAL: ELF: Header validation passed\n");

    /* Step 3: Create process first */
    process = elf_create_process(header, path);
    if (!process)
    {
        klog_debug("ELF", "SERIAL: ELF: ERROR - Failed to create process\n");
        kfree(file_data);
        return -1;
    }

    /* Step 4: Load segments into memory */
    if (elf_load_segments(header, (uint8_t *)file_data, file_size, process) != 0)
    {
        klog_debug("ELF", "SERIAL: ELF: ERROR - Failed to load segments\n");
        /*
         * spawn_user() already queued this process; drop it from the scheduler
         * and free the struct so we do not run a half-loaded image.
         */
        sched_remove_process(process);
        (void)process_remove_from_list(process);
        process_destroy(process);
        kfree(process);
        kfree(file_data);
        return -1;
    }

    /* Step 5: Set up stack with argc/argv/envp */
    at_phdr = elf_compute_at_phdr(header, (uint8_t *)file_data);
    at_base = elf_compute_load_base(header, (uint8_t *)file_data);
    if (elf_setup_stack(process, argv, envp, header, at_phdr, at_base,
                        "kexecve", path) != 0)
    {
        klog_debug("ELF", "SERIAL: ELF: WARNING - Failed to set up stack arguments, continuing anyway\n");
        /* Continue even if stack setup fails - some binaries don't need args */
    }

    sched_add_process(process);

    /* Step 6: Clean up file data */
    kfree(file_data);

    klog_debug_fmt("ELF", "SERIAL: ELF: SUCCESS - Program loaded and scheduled for execution\nSERIAL: ELF: PID: %x Entry: 0x%x", (unsigned)(process->task.pid), (unsigned)((uint32_t)task_get_ip(&process->task)));
    klog_debug("ELF", "SERIAL: ELF: ========================================\n");

    return process->task.pid;
}

int kexecve(const char *path, char *const argv[], char *const envp[])
{
    return kexecve_depth(path, argv, envp, 0);
}

/*
 * exec_commit_ctx - per-exec_replace_current commit audit (single-threaded kernel).
 */
static struct
{
	uint64_t mm_entry;
	uint64_t task_cr3_entry;
	uint64_t active_cr3_entry;
	uint64_t entry_rip;
	uint64_t task_rip_final;
	uint64_t task_rsp_final;
	int unmapped;
	int segments_loaded;
	int stack_ready;
	const char *fail_point;
} exec_commit_ctx;

static void exec_commit_emit(const char *point, int64_t errno_val,
                             process_t *proc, const char *classify)
{
	klog_debug_fmt("EXEC",
		       "[EXEC_COMMIT] point=%s classify=%s errno=%llx pid=%x "
		       "mm_entry=%llx mm_now=%llx mm_same=%s task_cr3_entry=%llx "
		       "task_cr3_now=%llx active_cr3=%llx cr3_activate=%s "
		       "entry_rip=%llx task_rip=%llx task_rsp=%llx unmapped=%s "
		       "loaded=%s stack=%s",
		       point ? point : "(null)", classify ? classify : "(null)",
		       (unsigned long long)((uint64_t)errno_val),
		       (unsigned)(proc ? (uint32_t)proc->task.pid : 0),
		       (unsigned long long)(exec_commit_ctx.mm_entry),
		       (unsigned long long)(proc ? (uint64_t)(uintptr_t)process_pgd(proc) : 0),
		       (proc && (uint64_t)(uintptr_t)process_pgd(proc) ==
				exec_commit_ctx.mm_entry)
			   ? "1"
			   : "0",
		       (unsigned long long)(exec_commit_ctx.task_cr3_entry),
		       (unsigned long long)(proc ? process_mm_root(proc) : 0),
		       (unsigned long long)(get_current_page_directory()),
		       (point && strcmp(point, "before-userswitch") == 0)
			   ? "switch_to_user_asm"
			   : "not_yet",
		       (unsigned long long)(exec_commit_ctx.entry_rip),
		       (unsigned long long)(proc ? task_get_ip(&proc->task)
						 : exec_commit_ctx.task_rip_final),
		       (unsigned long long)(proc ? task_get_sp(&proc->task)
						 : exec_commit_ctx.task_rsp_final),
		       exec_commit_ctx.unmapped ? "1" : "0",
		       exec_commit_ctx.segments_loaded ? "1" : "0",
		       exec_commit_ctx.stack_ready ? "1" : "0");
	klog_debug_fmt("EXEC", "CLASSIFY %s",
		       classify ? classify : "(null)");
}

static const char *exec_audit_classify_vfs(int vfs_ret, size_t file_size,
                                           void *file_data, const char *path)
{
	if (!path)
		return "EXEC_PATH_COPY_BAD";

	if (vfs_ret == -EFAULT)
		return "EXEC_PATH_COPY_BAD";

	if (vfs_ret == -ENOENT || vfs_ret == -ENOSYS)
	{
		if (strcmp(path, "/bin/busybox") == 0)
			return "EXEC_BUSYBOX_FILE_MISSING_OR_TRUNCATED";
		return "EXEC_LOOKUP_FAIL";
	}

	if (vfs_ret == -EINVAL)
		return "EXEC_OPEN_FAIL";

	if (vfs_ret != 0)
		return "EXEC_VFS_READ_ERR";

	if (file_size == 0 || !file_data)
	{
		if (path && strcmp(path, "/bin/busybox") == 0)
			return "EXEC_BUSYBOX_FILE_MISSING_OR_TRUNCATED";
		return "EXEC_VFS_READ_ZERO";
	}

	return NULL;
}

static const char *exec_audit_classify_elf(const elf64_header_t *header,
                                           size_t file_size)
{
	if (!header || file_size < sizeof(elf64_header_t))
		return "EXEC_VFS_READ_SHORT";

	if (header->e_ident[0] != ELF_MAGIC_0 ||
	    header->e_ident[1] != ELF_MAGIC_1 ||
	    header->e_ident[2] != ELF_MAGIC_2 ||
	    header->e_ident[3] != ELF_MAGIC_3)
		return "EXEC_ELF_MAGIC_BAD";

	return NULL;
}

static void exec_audit_emit_elf_header(const uint8_t *file_data, size_t file_size)
{
	size_t i;
	const elf64_header_t *header;

	if (!file_data || file_size < 4)
	{
		klog_debug("EXEC", "[EXEC_AUDIT][ELF] stage=header_read bytes=0 magic=(none)\n");
		return;
	}

	header = (const elf64_header_t *)file_data;
	klog_debug_fmt("EXEC", "[EXEC_AUDIT][ELF] stage=header_read expect=%llx got=%llx magic=", (unsigned long long)((uint64_t)sizeof(elf64_header_t)), (unsigned long long)((uint64_t)file_size));
	for (i = 0; i < 4; i++)
	{
		if (i > 0)
			klog_debug_fmt("KERN", " %llx", (unsigned long long)((uint64_t)file_data[i]));
	}
	klog_debug_fmt("KERN", " e_type=%llx e_machine=%llx", (unsigned long long)((uint64_t)header->e_type), (unsigned long long)((uint64_t)header->e_machine));
}

/**
 * exec_replace_current - Replace the current user process image in-place.
 * @path: Path to ELF executable
 * @argv: Command line arguments (NULL-terminated, may be NULL)
 * @envp: Environment variables (NULL-terminated, may be NULL)
 *
 * Unmaps the current user address space, reloads ELF segments into the same
 * process (same PID), sets up a fresh stack/auxv, and jumps to user mode.
 * Does not return on success.
 */
/*
 * Set-user-ID / set-group-ID on exec (Linux execve(2)).
 *
 * The decision is taken from the on-disk inode before the image is replaced,
 * and only committed once the old image is gone: from that point every failure
 * kills the task (exec_fail_kill), so a failed exec can never return to the
 * caller with raised credentials.
 */
struct exec_setid
{
	int raise_uid;
	int raise_gid;
	uint32_t new_euid;
	uint32_t new_egid;
};

/*
 * DAC on exec. vfs_read_file() enforces nothing, so without this an
 * unprivileged task could load (and with set-user-ID bits, gain the identity
 * of) a binary it has no execute permission on. Root keeps the bypass it has
 * everywhere else in ir0_access_from_stat().
 */
static int exec_permission_denied(const process_t *proc, const char *path)
{
	stat_t st;

	if (!proc || proc->euid == ROOT_UID)
		return 0;

	/*
	 * Linux resolves the path before judging permission (do_open_execat):
	 * a missing file is -ENOENT, and -EACCES is reserved for one that
	 * exists without the exec bit. Answering "denied" for both turned every
	 * miss of a shell PATH walk into a permission refusal — a plain
	 * `mount` typed by a non-root user logged EACCES for
	 * /home/labuser/mount before reaching the real binary.
	 */
	if (vfs_stat(path, &st) != 0)
		return 0;

	return ir0_check_file_access(path, ACCESS_EXEC) ? 0 : 1;
}

static void exec_setid_collect(const process_t *proc, const char *path,
			       struct exec_setid *out)
{
	stat_t st;

	memset(out, 0, sizeof(*out));

	if (!proc || !path)
		return;
	if (vfs_stat(path, &st) != 0)
		return;
	if (!S_ISREG(st.st_mode))
		return;
	if (!(st.st_mode & (S_ISUID | S_ISGID)))
		return;

	if (proc->no_new_privs)
	{
		klog_notice_fmt("EXEC",
				"no_new_privs: ignoring set-id bits on %s", path);
		return;
	}

	if (!vfs_path_allows_setid(path))
	{
		klog_notice_fmt("EXEC",
				"nosuid mount: ignoring set-id bits on %s", path);
		return;
	}

	if (st.st_mode & S_ISUID)
	{
		out->raise_uid = 1;
		out->new_euid = (uint32_t)st.st_uid;
	}
	if (st.st_mode & S_ISGID)
	{
		out->raise_gid = 1;
		out->new_egid = (uint32_t)st.st_gid;
	}
}

static void exec_setid_commit(process_t *proc, const struct exec_setid *setid)
{
	if (!proc || !setid)
		return;

	if (setid->raise_uid)
		proc->euid = setid->new_euid;
	if (setid->raise_gid)
		proc->egid = setid->new_egid;

	/* POSIX: the saved IDs track the effective IDs of the new image. */
	proc->suid = proc->euid;
	proc->sgid = proc->egid;
	proc->at_secure = (proc->uid != proc->euid || proc->gid != proc->egid);

	if (setid->raise_uid || setid->raise_gid)
		klog_notice_fmt("EXEC",
				"set-id exec: pid=%u uid=%u euid=%u gid=%u egid=%u",
				(unsigned)proc->task.pid, (unsigned)proc->uid,
				(unsigned)proc->euid, (unsigned)proc->gid,
				(unsigned)proc->egid);
}

static void exec_fail_kill(process_t *proc, int code, const char *point)
{
	exec_commit_ctx.fail_point = point;
	exec_commit_emit(point ? point : "exec_fail_kill", (int64_t)code, proc,
	                 "EXEC_LOADER_FAIL");
	process_fase43_proc_audit("exec-fail-kill");
	paging_fase43_oom_audit("exec-fail-kill");
	process_exit(code);
}

static int exec_replace_current_depth(const char *path, char *const argv[],
                                      char *const envp[], int shebang_depth);

int exec_replace_current(const char *path, char *const argv[], char *const envp[])
{
    return exec_replace_current_depth(path, argv, envp, 0);
}

static int exec_replace_current_depth(const char *path, char *const argv[],
                                      char *const envp[], int shebang_depth)
{
    process_t *proc = current_process;
    void *file_data = NULL;
    size_t file_size = 0;
    elf64_header_t *header;
    uint64_t at_phdr;
    uint64_t at_base;
    const char *basename;
    const char *last_slash;
    const char *walk;
    size_t total_frames_before = 0;
    size_t used_frames_before = 0;
    size_t total_frames_after = 0;
    size_t used_frames_after = 0;
    struct exec_setid setid;

    if (!proc || proc->mode != USER_MODE || !path)
    {
        exec_commit_emit("return-entry-invalid", -1, proc,
                         "EXEC_ABORT_BEFORE_COMMIT");
        return -1;
    }

    memset(&exec_commit_ctx, 0, sizeof(exec_commit_ctx));
    exec_commit_ctx.mm_entry = (uint64_t)(uintptr_t)process_pgd(proc);
    exec_commit_ctx.task_cr3_entry = process_mm_root(proc);
    exec_commit_ctx.active_cr3_entry = get_current_page_directory();

    paging_ir0_mm_checkpoint("exec-before", (int32_t)proc->task.pid);
    process_fase44_list_checkpoint("exec-before");
    pmm_stats(&total_frames_before, &used_frames_before, NULL);

    klog_debug_fmt("ELF", "SERIAL: ELF: exec_replace_current: %s", path);

    if (argv)
    {
        int ai;

        klog_debug_fmt("EXEC", "[EXEC_AUDIT][LOADER] path=%s argv=", path ? path : "(null)");
        for (ai = 0; ai < 4 && argv[ai]; ai++)
        {
            if (ai > 0)
                klog_debug_fmt("KERN", ",%s", argv[ai]);
            else
                klog_debug_fmt("KERN", "%s", argv[ai]);
        }
    }

    if (exec_permission_denied(proc, path))
    {
        exec_commit_emit("return-eacces", -EACCES, proc,
                         "EXEC_ABORT_BEFORE_COMMIT");
        return -EACCES;
    }

    /*
     * Image read is the largest single kernel allocation in the system (the
     * whole binary; ~1.8 MiB for busybox-full), so it is the first thing to
     * fail when the heap fragments. That failure mode shipped undetected
     * once already — exec started returning -ENOMEM after a few hundred
     * spawns — because nothing exercised this branch.
     */
    if (KTM_FAULT_HIT("exec.read_file"))
    {
        exec_commit_emit("return-read-fault", -ENOMEM, proc,
                         "EXEC_ABORT_BEFORE_COMMIT");
        return -ENOMEM;
    }

    vfs_exec_audit_begin(path);
    {
        int vfs_ret = vfs_read_file(path, &file_data, &file_size);
        const char *vfs_class;

        vfs_exec_audit_end();
        vfs_class = exec_audit_classify_vfs(vfs_ret, file_size, file_data, path);
        if (vfs_ret != 0 || !file_data)
        {
            klog_debug_fmt("EXEC", "CLASSIFY %s vfs_ret=%llx", vfs_class ? vfs_class : "EXEC_VFS_READ_ERR", (unsigned long long)((uint64_t)(int64_t)vfs_ret));
            klog_debug_fmt("EXEC", "[EXEC_ONLY][LOADER] vfs_read_fail path=%s errno=%llx", path ? path : "(null)", (unsigned long long)((uint64_t)(int64_t)vfs_ret));
            exec_commit_emit("return-vfs_read_fail", (int64_t)vfs_ret, proc,
                             vfs_class ? vfs_class : "EXEC_ABORT_BEFORE_COMMIT");
            return vfs_ret < 0 ? vfs_ret : -ENOENT;
        }
    }

    exec_audit_emit_elf_header((const uint8_t *)file_data, file_size);
    if (!exec_buffer_is_elf(file_data, file_size))
    {
        char interp[256];
        char iarg[128];
        int sh_rc;

        sh_rc = exec_parse_shebang_line(file_data, file_size, interp,
                                        sizeof(interp), iarg, sizeof(iarg));
        kfree(file_data);
        file_data = NULL;

        if (sh_rc == 0)
        {
            exec_commit_emit("return-validate_elf_fail", -ENOEXEC, proc,
                             "EXEC_ENOEXEC");
            return -ENOEXEC;
        }
        if (sh_rc < 0)
        {
            exec_commit_emit("return-validate_elf_fail", sh_rc, proc,
                             "EXEC_SHEBANG_BAD");
            return sh_rc;
        }
        if (shebang_depth >= EXEC_SHEBANG_MAX_DEPTH)
        {
            exec_commit_emit("return-validate_elf_fail", -ENOEXEC, proc,
                             "EXEC_SHEBANG_DEPTH");
            return -ENOEXEC;
        }
        if (exec_permission_denied(proc, interp))
        {
            exec_commit_emit("return-eacces", -EACCES, proc,
                             "EXEC_ABORT_BEFORE_COMMIT");
            return -EACCES;
        }
        {
            char **new_argv = exec_build_shebang_argv(interp,
                                                      iarg[0] ? iarg : NULL,
                                                      path, argv);
            int rc;

            if (!new_argv)
            {
                exec_commit_emit("return-read-fault", -ENOMEM, proc,
                                 "EXEC_ABORT_BEFORE_COMMIT");
                return -ENOMEM;
            }
            rc = exec_replace_current_depth(interp, new_argv, envp,
                                            shebang_depth + 1);
            exec_release_string_vector(new_argv);
            kfree(new_argv);
            return rc;
        }
    }
    {
        const char *elf_class =
            exec_audit_classify_elf((elf64_header_t *)file_data, file_size);

        if (elf_class)
        {
            klog_debug_fmt("EXEC", "CLASSIFY %s", elf_class);
            kfree(file_data);
            exec_commit_emit("return-validate_elf_fail", -ENOEXEC, proc,
                             elf_class);
            return -ENOEXEC;
        }
    }

    if (!validate_elf_header((elf64_header_t *)file_data))
    {
        kfree(file_data);
        exec_commit_emit("return-validate_elf_fail", -ENOEXEC, proc,
                         "EXEC_ELF_MAGIC_BAD");
        return -ENOEXEC;
    }

    header = (elf64_header_t *)file_data;
    at_phdr = elf_compute_at_phdr(header, (uint8_t *)file_data);
    at_base = elf_compute_load_base(header, (uint8_t *)file_data);

    /* Decided on the old image; committed below, past the point of no return. */
    exec_setid_collect(proc, path, &setid);

    process_exec_close_cloexec(proc);

    {
        size_t used_after_unmap = 0;

        process_unmap_user_address_space(proc);
        pmm_stats(NULL, &used_after_unmap, NULL);
        exec_commit_ctx.unmapped = 1;
    }

    {
        unsigned freed = 0;

        while (process_mmap_list(proc))
        {
            struct mmap_region *next = process_mmap_list(proc)->next;

            kfree(process_mmap_list(proc));
            process_mm_set_mmap_list(proc, next);
            freed++;
        }
        klog_debug_fmt("EXEC", "vma teardown pid=%x freed=%x\n",
                       (unsigned)((uint32_t)proc->task.pid), freed);
    }

    process_set_heap_start(proc, 0);
    process_set_heap_end(proc, 0);
    /*
     * Linux clears TLS across execve. Stale tls_base from the pre-exec image
     * (or fork parent) must not be restored on the next context switch —
     * that leaves TLS pointing at unmapped VA while glibc/musl still expect
     * to install a new base during CRT startup (x86: arch_prctl; ARM: TPIDR).
     */
    process_tls_set(proc, 0);
    /*
     * Hardware TLS is per-CPU. Only clobber it when this image is the one
     * currently running — otherwise a concurrent parent keeps a live FS/TPIDR
     * while the child's process_t already says 0.
     */
    if (proc == current_process)
	    set_tls(0);
    process_set_stack_layout(proc, USER_STACK_TOP - USER_STACK_SIZE,
			     USER_STACK_SIZE);

    if (map_user_region_in_directory(process_pgd(proc), process_stack_start(proc),
                                     process_stack_size(proc), PAGE_RW) != 0)
    {
        kfree(file_data);
        exec_fail_kill(proc, 127, "map_stack_fail");
    }

    if (elf_load_segments(header, (uint8_t *)file_data, file_size, proc) != 0)
    {
        kfree(file_data);
        exec_fail_kill(proc, 127, "elf_load_segments_fail");
    }

    {
	uint64_t brk0 = elf_compute_initial_brk(header, (uint8_t *)file_data);

	if (brk0 != 0)
	{
		process_set_heap_start(proc, brk0);
		process_set_heap_end(proc, brk0);
	}
    }
    exec_commit_ctx.segments_loaded = 1;

    /* Old image is gone: raise credentials before auxv (AT_SECURE/AT_EUID). */
    exec_setid_commit(proc, &setid);

    basename = path;
    last_slash = path;
    walk = path;
    while (*walk)
    {
        if (*walk == '/')
            last_slash = walk + 1;
        walk++;
    }
    basename = last_slash;
    strncpy(proc->comm, basename, sizeof(proc->comm) - 1);
    proc->comm[sizeof(proc->comm) - 1] = '\0';
    /* @path is already absolute here (sys_exec resolved it). */
    strncpy(proc->exe_path, path, sizeof(proc->exe_path) - 1);
    proc->exe_path[sizeof(proc->exe_path) - 1] = '\0';

    task_set_ip(&proc->task, header->e_entry);
    exec_commit_ctx.entry_rip = header->e_entry;
    task_set_user_segments(&proc->task);
    task_set_flags(&proc->task, task_initial_user_status());

    /*
     * Past the point of no return: the old image is already gone, so this
     * exercises the kill path rather than a rollback. What must hold is that
     * the dead process leaves no frames, no VMAs and no fd behind.
     */
    if (KTM_FAULT_HIT("exec.setup_stack") ||
        elf_setup_stack(proc, argv, envp, header, at_phdr, at_base,
                        "exec_replace_current", path) != 0)
    {
        kfree(file_data);
        exec_fail_kill(proc, 127, "elf_setup_stack_fail");
    }
    exec_commit_ctx.stack_ready = 1;
    exec_commit_ctx.task_rip_final = task_get_ip(&proc->task);
    exec_commit_ctx.task_rsp_final = task_get_sp(&proc->task);
    elf_trace_entry_stack_layout(proc, header, at_phdr, at_base, "after-setup-stack");

    kfree(file_data);
    pmm_stats(&total_frames_after, &used_frames_after, NULL);
    paging_ir0_mm_checkpoint("exec-after", (int32_t)proc->task.pid);
    process_fase44_list_checkpoint("exec-after");

    klog_debug_fmt("ELF", "SERIAL: ELF: exec_replace success PID %x", (unsigned)((uint32_t)proc->task.pid));
    klog_debug_fmt("ELF", "SERIAL: ELF: exec CR3 active=%llx task_cr3=%llx mm_cr3=%llx", (unsigned long long)(get_current_page_directory()), (unsigned long long)(process_mm_root(proc)), (unsigned long long)((uint64_t)(uintptr_t)process_pgd(proc)));
    /*
     * Linux clears pending catchable signals across execve. IR0 had
     * signals_reset_on_exec() but never called it — fork PF leftovers
     * (SIGSEGV pending) then made accept/poll return -EINTR immediately.
     */
    signals_reset_on_exec(proc);
    /*
     * Password read may leave want_kernel_ret set; clear before iretq into
     * the new image. Cooked+echo is restored from userspace before execve.
     */
    proc->want_kernel_ret = 0;
    proc->irq_frame_saved = 0;
    ir0_console_reset_cooked_echo();
    elf_trace_argv_contract(proc, path, "before-iret");
    elf_trace_entry_stack_layout(proc, header, at_phdr, at_base, "before-userswitch");

    /*
     * Point of no return. switch_to_user() never comes back, so the caller's
     * own cleanup after exec_replace_current() only runs when exec fails: on
     * success every argv/envp copy leaked. Each was a few hundred bytes, five
     * or so per execve, scattered across the heap — after ~277 execs the
     * 24 MiB heap was 90% free but its largest hole was under 2 MiB and exec
     * itself started failing with -ENOMEM. The strings have already been
     * written to the new user stack here. Linux frees the bprm the same way
     * before entering the image (fs/exec.c).
     */
    exec_release_string_vector(argv);
    exec_release_string_vector(envp);

    exec_commit_emit("before-userswitch", 0, proc, "EXEC_COMMIT_OK");
    switch_to_user((arch_addr_t)task_get_ip(&proc->task), (arch_addr_t)task_get_sp(&proc->task));
    exec_commit_emit("return-after-userswitch", -1, proc, "EXEC_COMMIT_RETURNED");
    return -1;
}
