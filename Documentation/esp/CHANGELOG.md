# IR0 Kernel — Changelog (español)

> **Última verificación:** 2026-07-30  
> **Fuente de verdad:** historial git, smokes del `Makefile`, [`../STABLE.md`](../STABLE.md), [`../KTM.md`](../KTM.md)

Versión en inglés (canónica): [`../CHANGELOG.md`](../CHANGELOG.md).

## [0.0.1-rc4] — 2026-07-30 (último pre-release)

Tag **`v0.0.1-rc4`**. Tras este tag: **solo bugfixing y estabilización** hasta `v0.0.1` final.  
Detalle: [`../releases/IR0_0.0.1_RC4.md`](../releases/IR0_0.0.1_RC4.md).

## [Sin publicar]

### Doom T2 audio/mouse + ESC + TinyCC `/lib/tcc` (2026-07-28)

- doomgeneric: mouse `EV_REL`+botones; PCM `/dev/audio` sin `-nosound`; smoke con
  tags mouse/audio/`AUDIO_WRITE` y SB16.
- ESC PS/2 → `0x1b` para BusyBox `vi`.
- TinyCC `--tccdir=/lib/tcc`; inject exige `libtcc1.a`/`libc.a`/`crt*`.
- Docs: [`../testing/DOOM_FASE55D.md`](../testing/DOOM_FASE55D.md).

### Captura BusyBox matrix (2026-07-28)

- Drain hasta EOF + needles streaming; ver [`../testing/BUSYBOX_MATRIX.md`](../testing/BUSYBOX_MATRIX.md).

### Límite kernel / userspace (2026-07-25)

- Hermano público **[IR0-userspace](https://github.com/IRodriguez13/IR0-userspace)**.
- Acoplamiento: [`USERSPACE.md`](USERSPACE.md) / [`../USERSPACE.md`](../USERSPACE.md).
- Eliminados del kernel: `debug_bins/`, `kernel/init.c` (dbgshell), Kconfig
  `KERNEL_DEBUG_SHELL` / `DEBUG_BINS*`. Boot solo `kexecve("/sbin/init")`.
- Detalle canónico en el CHANGELOG inglés.

### Banner portable / SB16 / Class B / desk (2026-07-23)

- `ir0_boot_serial_ready()` — mismo banner en toda ISA; ARM64 early con
  `IR0_FREESTANDING_BOOT`; SB16/Class B/desk como en canónico EN.

### Higiene klog / KTM (2026-07-21)

- Hub klog `[ts] [LEVEL] [COMP]`; `ASSERT_BATCH`; `KTM_SERIAL_VERBOSE` default n.
- Autokill: stderr QEMU → `*.qemu-stderr`.
- runit: `ir0_smoke_tag` + hostshare/pause.
- Regla `ir0-version-stamp.mdc` en `ai_driven_dev/rules/`.

---

## [0.0.1] — 2026-06-23 — baseline estable + hardening cerrado

### Documentación

- **`STABLE.md`** — checklist 0.0.1: lo que estaba en desarrollo ahora estable, prueba QEMU (serial + GTK), no-objetivos explícitos.
- **`ROADMAP.md`**, **`HARDENING.md`**, **`README.md`** — alineados con H1–H6 y alcance 0.0.1.
- **`make health`** — incluye `kernel-text-budget`.

### Hardening H1–H6

Split syscalls (86 L glue), FASE en `fase_audit.c`, facades sin drivers en `includes/ir0/`, devfs read unificado, budget `.text` ~815754 B.

### Baseline release (sign-off mantenedor)

Cerrado para 0.0.1: **runit**, **BusyBox**, **TinyCC**, **COW**, **lazy alloc**, smokes T1, path T2 fb/input/Doom GUI.

### Validación

29 ktests, 12 host tests, arch-guard, build-matrix-min, kernel-text-budget — OK.

Detalle completo en [`../CHANGELOG.md`](../CHANGELOG.md) sección `[0.0.1]`.

---

## [0.0.1-pre] — 2026-06-17 y oleadas previas

Ver changelog EN para wait4 NULL, KTM panic site, FAT16 MVP, ARCH-1 parcial, etc.
