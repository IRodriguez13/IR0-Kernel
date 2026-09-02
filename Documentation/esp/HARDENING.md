# IR0 — Backlog de hardening arquitectónico

> **Última verificación:** 2026-09-01  
> **Fuente de verdad:** [`Documentation/HARDENING.md`](../HARDENING.md) (inglés, canónico)

Espejo breve del plan de sanitización post-hito. Detalle completo, tablas de archivos y gates en el documento EN.

## Oleada cerrada (2026-09-02) — pipes + stack Linux-strict **CERRADO**

- Write atómico ≤ `PIPE_BUF`; `sys_write` blocking completa el count; SIGPIPE
- `pipe_wait(write_need)`; IRQ signals con `signal_pick_handler_sp`
- Pack ISD: `/tmp` 1777
- Gates: `smoke-pipeline-stress`, `smoke-mm-cow-lazy`, host pipes
- Detalle: [`../HARDENING.md`](../HARDENING.md)

## Oleada cerrada (2026-09-01) — audio, MM, encapsulación **CERRADO**

- SB16: buffers ping-pong, DMA antes PLAY, IRQ 5; `smoke-sb16-probe`
- `fd_can_write_for` sin mutar `current_process`
- PF: sin soft-grow heap/stack; `pf_debug_*` en arch x86
- `ir0_spinlock` en pipes; `process_saved_context_*` + arch-guard
- Gates: `kernel-x64.bin`, arch-guard, host 43/43

Pendiente fuera de oleada: smoke Doom IWAD al disparar; pipe stress 5/6.

## Oleada cerrada (2026-06-23) — hardening estructural **CERRADO**

- **ARCH-1:** `kernel/syscalls.c` **86 líneas**; submódulos en `kernel/syscalls/`
- **H2:** FASE43–48 → `kernel/debug/fase_audit.c`; `process.c` ~2980 L; hooks `fase_audit_note_*`
- **H3:** facades — 0 `#include <drivers/` en `includes/ir0`
- **H5:** `devfs_resolve_read_fd()` unifica `sys_read` devfs
- **H6:** `make kernel-text-budget` + `make health`; cap 850 KiB `.text`
- Gates: ktests, host, arch-guard, build-matrix-min

## Sprints

| ID | Estado | Notas |
|----|--------|-------|
| **H1** | **Hecho** | Glue 86 L |
| **H2** | **Hecho** | Módulo `fase_audit` completo |
| **H3** | **Hecho** | arch-guard regla 14 |
| **H4** | Hecho (musl) | ktm inventory vía `make ktm-check` |
| **H5** | **Hecho** | devfs read unificado |
| **H6** | **Hecho** | budget `.text` en `health` |

## Baseline release 0.0.1 (cerrada por mantenedor)

runit, tcc, BusyBox+, COW, lazy alloc — objetivos finales 0.0.1; no revalidar salvo regresión.

**Siguiente:** escalar tier/features con hardening H1–H6 verde. **Prueba QEMU:** [`STABLE.md`](STABLE.md).

Gates por sprint: ver [`HARDENING.md`](HARDENING.md) y [`STABLE.md`](STABLE.md).
