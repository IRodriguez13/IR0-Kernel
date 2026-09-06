# Capa de syscalls de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | vfs, process, memory |
| Página man | IR0-syscalls (sección 7) |
| Fuentes principales | `kernel/syscalls.c`, `kernel/syscalls/{fs_syscalls,socket_syscalls,syscall_dispatch}.c`, `arch/x86-64/asm/syscall_*.asm`, `includes/ir0/syscall*.h`, `includes/ir0/copy_user.c` |

## 1. Visión general

Los programas de usuario alcanzan el kernel mediante syscalls numeradas al estilo
Linux en x86-64. Coexisten dos mecanismos de entrada: legacy **`int 0x80`**
(ABI de laboratorio histórica) y la instrucción **`syscall`** (ABI musl). El despacho está
centralizado en `syscall_dispatch()` con una tabla de handlers `__NR_*`. La I/O
de ficheros está separada en `fs_syscalls.c`; la mayoría de handlers restantes
permanecen en el monolítico `syscalls.c`.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `syscall_entry_64.asm` | int 0x80: args en rax, rbx, rcx, rdx, rsi, rdi |
| `syscall_insn_entry_64.asm` | insn syscall Linux: rdi, rsi, rdx, r10, r8, r9; kstack 8 KiB |
| `syscall_dispatch` | Chequeo de límites, lookup en tabla, invocar handler |
| `init_syscall_table` | Conecta `__NR_*` → `wrap_sys_*` (incluye familia socket) |
| `fs_syscalls.c` | Enrutamiento open/read/write/stat |
| `socket_syscalls.c` | socket/bind/listen/accept/connect/send/recv/socketpair |
| `copy_user.c` | Chequeos de rango user + copia consciente de directorio |

**Modelo de FD (`process.h`):**

```text
  fd_table[MAX_FDS_PER_PROCESS=64]
    path[256], flags (IR0_O_*), vfs_file*, offset
    is_pipe, pipe_end, is_devfs, dev_device_id
  stdio 0–2 → /dev/stdin, /dev/stdout, /dev/stderr
```

## 3. Flujo de datos

```text
  userspace insn syscall
        │
        ▼
  syscall_insn_entry_64.asm  (guardar frame, kstack)
        │
        ▼
  syscall_dispatch(nr, args...)
        │
        ├─ __NR_open ──► sys_open (fs_syscalls.c)
        │                    ├─ /proc → proc_open
        │                    ├─ /sys  → sysfs_open
        │                    ├─ /dev  → ruta devfs
        │                    └─ else  → vfs_open
        ├─ __NR_read/write ──► rama por tipo fd (devfs/sysfs/pipe/vfs)
        ├─ __NR_mmap/brk ──► mm + paging
        └─ nr desconocido ──► -ENOSYS
        │
        ▼
  retorno (convención errno negativo)
        │
        ▼
  sysret / iretq
```

**`copy_from_user` / `copy_to_user`:**

Contrato canónico: [`Documentation/esp/uaccess.md`](../../esp/uaccess.md)
(inglés: [`uaccess.md`](../../uaccess.md)).

1. Si proceso `KERNEL_MODE` (solo dbgshell / embebido): bypass `memcpy`.
2. Si no, validar con `mm_user_va_ok` (ventana ISA en `arch/*/arch_mm.c`).
3. Copiar vía `copy_*_region_in_directory(process_pgd(current), …)` (COW-safe).
4. Nunca `load_page_directory` + `memcpy` crudo a VA user — clasifica como
   `KERNEL_UACCESS_FAULT` y falla `arch-guard` en signals/syscalls P0.

## 4. Responsabilidades

- ASM de entrada: preservar ABI, capturar frame syscall para resume fork/signal.
- Dispatch: rechazar `nr >= __NR_syscall_max` con `-ENOSYS`.
- Handlers: traducir flags Linux antes de VFS; nunca pasar `O_*` Linux crudos a `vfs_open`.
- Capa FS: enrutar rutas pseudo antes de VFS (ver IR0-vfs).

## 5. Límites del subsistema

- `kernel/syscalls.c` no debe crecer sin plan de split (`fs_syscalls.c`, submódulos futuros).
- Sin `#include <drivers/...>` en rutas syscall portables (architecture_guard).
- Syscalls de socket cableadas (`socket_syscalls.c`): stream **AF_UNIX** +
  `socketpair`, y **AF_INET** `SOCK_STREAM` al camino TCP wire. Otros
  dominios/tipos siguen con `-ENOSYS` / `-EAFNOSUPPORT`. Ver `IR0-net` e
  `IR0-ipc`.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| VFS | `vfs_*` solo para rutas montadas |
| Process | `current_process`, tabla fd, estados block/wake |
| MM | mmap/brk/mprotect; copy_user usa PML4 del proceso |
| Scheduler | block → `PROCESS_BLOCKED`; idle poll despierta |
| Console | `__NR_console_*` dedicados, syscalls keymap |

## 7. Mapas visuales

```text
  ring 3                    ring 0
  ┌──────────┐             ┌─────────────────┐
  │ musl     │  syscall    │ tabla dispatch  │
  │ __NR_*   │────────────►│ wrap_sys_*      │
  └──────────┘             └────────┬────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
               fs_syscalls      process/mm      console/...
```

## 8. Invariantes importantes

1. Retornar errno negativo a userspace; valores positivos son payloads de éxito (p. ej. fd, pid).
2. `MAX_FDS_PER_PROCESS = 64`; escaneo desde fd 3 para nuevas aperturas.
3. Copias user nunca usan suposición de CR3 global para otras tareas.
4. `poll`: máx. 32 fds, 16 waiters concurrentes (`MAX_POLL_NFDS`, `MAX_POLL_WAITERS`).
5. `openat`: solo `AT_FDCWD` soportado en el subconjunto actual.

## 9. Consejos de depuración

Tags: `[FASE50C][OPEN]`, `[EXEC_AUDIT]`, logs de traducción de flags desde `ir0_open_flags_log_translation`.

- `-ENOSYS`: comprobar cableado `init_syscall_table` vs musl `__NR_*`.
- `-EFAULT`: rango copy_user o página user no mapeada.
- `-EINVAL` en open: flags Linux no traducidos a `IR0_O_*`.

Referencia: tabla syscalls x86-64 Linux, musl `arch/x86_64/syscall_arch.h`.

## 10. Hoja de ruta futura

- `sys_reboot`: POWER_OFF usa ACPI PM1a + DSDT `_S5_` SLP_TYP si existe;
  `LINUX_REBOOT_CMD_KEXEC` → `REBOOT_KEXEC_LOADED` si hay `kexec_load`, si no `REBOOT_KEXEC_STUB`;
  `LINUX_REBOOT_CMD_SW_SUSPEND` → `SYSTEM_S3_ENTER` + soft `SYSTEM_S3_RESUME_OK`.
- `sys_kexec_load` (246): hasta 4 segmentos en RAM kernel; payload mágico `IR0KEXEC` para smoke.
- Split del monolito: glue restante en `syscalls.c`; process/mm/io/socket ya partidos.
- Resolución fd completa `openat`/`*at`.
- Opciones socket / datagram / profundidad TCP Internet (ver techo en `IR0-net`).
- ARM64: `syscall_entry_arm64` retorna -1 (stub).
