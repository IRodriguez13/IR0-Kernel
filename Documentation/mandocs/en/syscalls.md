# IR0 Syscall Layer

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | vfs, process, memory |
| Man page | IR0-syscalls (section 7) |
| Primary sources | `kernel/syscalls.c`, `kernel/syscalls/{fs_syscalls,socket_syscalls,syscall_dispatch}.c`, `arch/x86-64/asm/syscall_*.asm`, `includes/ir0/syscall*.h`, `includes/ir0/copy_user.c` |

## 1. Overview

User programs reach the kernel through Linux-numbered syscalls on x86-64.
Two entry mechanisms coexist: legacy **`int 0x80`** (historical lab ABI) and the
**`syscall` instruction** (musl ABI). Dispatch is centralized in
`syscall_dispatch()` with a table of `__NR_*` handlers. File I/O is split into
`fs_syscalls.c`; most other handlers remain in the monolithic `syscalls.c`.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `syscall_entry_64.asm` | int 0x80: args in rax, rbx, rcx, rdx, rsi, rdi |
| `syscall_insn_entry_64.asm` | Linux syscall insn: rdi, rsi, rdx, r10, r8, r9; 8 KiB kstack |
| `syscall_dispatch` | Bounds check, table lookup, invoke handler |
| `init_syscall_table` | Wires `__NR_*` → `wrap_sys_*` (incl. socket family) |
| `fs_syscalls.c` | open/read/write/stat path routing |
| `socket_syscalls.c` | socket/bind/listen/accept/connect/send/recv/socketpair |
| `copy_user.c` | User range checks + directory-aware copy |

**FD model (`process.h`):**

```text
  fd_table[MAX_FDS_PER_PROCESS=64]
    path[256], flags (IR0_O_*), vfs_file*, offset
    is_pipe, pipe_end, is_devfs, dev_device_id
  stdio 0–2 → /dev/stdin, /dev/stdout, /dev/stderr
```

## 3. Data flow

```text
  userspace syscall insn
        │
        ▼
  syscall_insn_entry_64.asm  (save frame, kstack)
        │
        ▼
  syscall_dispatch(nr, args...)
        │
        ├─ __NR_open ──► sys_open (fs_syscalls.c)
        │                    ├─ /proc → proc_open
        │                    ├─ /sys  → sysfs_open
        │                    ├─ /dev  → devfs path
        │                    └─ else  → vfs_open
        ├─ __NR_read/write ──► fd type branch (devfs/sysfs/pipe/vfs)
        ├─ __NR_mmap/brk ──► mm + paging
        └─ unknown nr ──► -ENOSYS
        │
        ▼
  return (negative errno convention)
        │
        ▼
  sysret / iretq
```

**`copy_from_user` / `copy_to_user`:**

Canonical contract: [`Documentation/uaccess.md`](../../uaccess.md).

1. If `KERNEL_MODE` process (dbgshell / embedded only): plain `memcpy` bypass.
2. Else validate with `mm_user_va_ok` (ISA window via `arch/*/arch_mm.c`).
3. Copy via `copy_*_region_in_directory(process_pgd(current), …)` (COW-safe).
4. Never `load_page_directory` + raw `memcpy` to a user VA — that classifies as
   `KERNEL_UACCESS_FAULT` at runtime and fails `arch-guard` for signals/syscalls P0.

## 4. Responsibilities

- Entry ASM: preserve ABI, capture syscall frame for fork/signal resume.
- Dispatch: reject `nr >= __NR_syscall_max` with `-ENOSYS`.
- Handlers: translate Linux flags before VFS; never pass raw Linux `O_*` to `vfs_open`.
- FS layer: route pseudo paths before VFS (see IR0-vfs).

## 5. Subsystem boundaries

- `kernel/syscalls.c` must not grow without split plan (`fs_syscalls.c`, future submodules).
- No `#include <drivers/...>` in syscall portable paths (architecture_guard).
- Socket syscalls are wired (`socket_syscalls.c`): **AF_UNIX** stream +
  `socketpair`, and **AF_INET** `SOCK_STREAM` onto the TCP wire path. Other
  domains/types still return `-ENOSYS` / `-EAFNOSUPPORT` as appropriate.
  See `IR0-net` and `IR0-ipc`.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| VFS | `vfs_*` for mounted paths only |
| Process | `current_process`, fd table, block/wake states |
| MM | mmap/brk/mprotect; copy_user uses process PML4 |
| Scheduler | block → `PROCESS_BLOCKED`; idle poll wakes |
| Console | dedicated `__NR_console_*`, keymap syscalls |

## 7. Visual maps

```text
  ring 3                    ring 0
  ┌──────────┐             ┌─────────────────┐
  │ musl     │  syscall    │ dispatch table  │
  │ __NR_*   │────────────►│ wrap_sys_*      │
  └──────────┘             └────────┬────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
               fs_syscalls      process/mm      console/...
```

## 8. Important invariants

1. Return negative errno to userspace; positive values are success payloads (e.g. fd, pid).
2. `MAX_FDS_PER_PROCESS = 64`; scan from fd 3 for new opens.
3. User copies never use global CR3 assumption for other tasks.
4. `poll`: max 32 fds, 16 concurrent waiters (`MAX_POLL_NFDS`, `MAX_POLL_WAITERS`).
5. `openat`: only `AT_FDCWD` supported in current subset.

## 9. Debugging tips

Tags: `[FASE50C][OPEN]`, `[EXEC_AUDIT]`, flag translation logs from `ir0_open_flags_log_translation`.

- `-ENOSYS`: check `init_syscall_table` wiring vs musl `__NR_*`.
- `-EFAULT`: copy_user range or unmapped user page.
- `-EINVAL` on open: Linux flags not translated to `IR0_O_*`.

Reference: Linux x86-64 syscall table, musl `arch/x86_64/syscall_arch.h`.

## 10. Future roadmap

- `sys_reboot`: POWER_OFF uses ACPI PM1a + DSDT `_S5_` SLP_TYP when found;
  `LINUX_REBOOT_CMD_KEXEC` → `REBOOT_KEXEC_LOADED` if `kexec_load` staged, else `REBOOT_KEXEC_STUB`;
  `LINUX_REBOOT_CMD_SW_SUSPEND` → `SYSTEM_S3_ENTER` + soft `SYSTEM_S3_RESUME_OK` (`_S3_` typ armed).
- `sys_kexec_load` (246): up to 4 segments into kernel RAM; magic `IR0KEXEC` payload for smoke.
- Split monolith: remaining glue in `syscalls.c`; process/mm/io/socket already split.
- Full `openat`/`*at` fd resolution.
- Broader socket options / datagram / Internet TCP depth (see `IR0-net` ceiling).
- ARM64: `syscall_entry_arm64` returns -1 (stub).
