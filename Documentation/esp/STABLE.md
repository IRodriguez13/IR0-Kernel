# IR0 — Baseline estable (release 0.0.1)

> **Última verificación:** 2026-07-30  
> **Fuente de verdad:** smokes/`Makefile`, merge `56a3f7b` (kexec/S3, P1-storage, P1-T1),  
> F8 MVP honesto (`smoke-f8-net`), Future F2–F6, híbrido runit+9p (`runit_hostshare_payload_run`),  
> [`../releases/IR0_0.0.1_RC4.md`](../releases/IR0_0.0.1_RC4.md), [`../HARDENING.md`](../HARDENING.md),  
> [`../ROADMAP.md`](../ROADMAP.md), gates CTR.

Checklist único de lo **estable para probar en QEMU** (serial y GTK), lo que **estaba en desarrollo** y quedó **cerrado en 0.0.1**, y lo que sigue siendo **trabajo futuro** ([`ROADMAP.md`](../ROADMAP.md) P1+).

Sign-off del mantenedor (2026-06-23): considerar **hecho para 0.0.1 final** salvo regresión en smokes.

**Nota de honestidad (2026-07-10):** textos STABLE anteriores afirmaban COW en fork mientras el
kernel aún hacía full-copy. El share-on-fork real + break en write fault está en `62cc512`
(`make smoke-mm-cow-lazy`). KTM es el plano de test canónico (`make ktm-run`,
`make ktm-userdev-run`); ver [`../ai_driven_dev/ktm.md`](../ai_driven_dev/ktm.md).

### Tag prep vs ship (2026-07-30)

- **`v0.0.1-rc4`**: **último pre-release** antes del final (`0.0.1-rc4`). Tras el tag: solo bugfixing/estabilización.
- **Ship `v0.0.1`**: VM manual del mantenedor + blockers de SCOPE.
- Detalle: [`../releases/IR0_0.0.1_RC4.md`](../releases/IR0_0.0.1_RC4.md).

---

## Merge → `master` — gates de producto (mantenedor)

**Política (2026-07-12):** antes de mergear `dev` → `master`, lo que **no puede romper** es
**TinyCC in-guest** (compilar y correr), **Doom T2 con IWAD real** (doomgeneric carga WAD + frames)
y **userspace amplio** (`smoke-posix-depth` o `smoke-tier1`). CTR solo no basta.

```bash
make smoke-tcc-power-halt
make IR0_LEGACY_SMOKE=1 smoke-fase55d-doomgeneric   # REAL_WAD_PATH por defecto en Makefile
make smoke-posix-depth
```

IWAD local por defecto: `/home/ivanr013/Escritorio/universal-doom/DOOM1.WAD`.
El stub `smoke-fase55b-doom-stub` es ayuda rápida, **no** bloquea merge.

Si TCC, Doom+WAD o el smoke userspace están en rojo → **no merge a `master`**.

---

## Alcance release 0.0.1

| Área | Estado | Prueba principal |
|------|--------|------------------|
| **Hardening H1–H6** | **Cerrado** | [`HARDENING.md`](HARDENING.md); `make health` |
| **runit boot** | **Estable** | `make smoke-runit-boot` |
| **BusyBox ash + applets** | **Estable (producto)** | Manifest `setup/busybox/required_applets.txt`; `make smoke-busybox-manifest` → `BUSYBOX_MANIFEST_OK` |
| **TinyCC in-guest** | **Crítico p/ merge** | `smoke-tcc-power-halt` — bloquea `master` |
| **COW fork** | **Estable** | `make smoke-mm-cow-lazy` |
| **Lazy alloc** | **Estable** | mismo smoke |
| **Slice POSIX T1** | **Estable** | manifests tier1/musl; smokes cred/pthread/setuid |
| **Gráficos T2 / Doom** | **Crítico p/ merge** | `IR0_LEGACY_SMOKE=1 smoke-fase55d-doomgeneric` (IWAD) + tags mouse/audio/`AUDIO_WRITE` + SB16 — bloquea `master`. Ver [`../testing/DOOM_FASE55D.md`](../testing/DOOM_FASE55D.md) |
| **Userspace amplio** | **Crítico p/ merge** | `smoke-posix-depth` o `smoke-tier1` |
| **Red local** | **Estable p/ prueba** | AF_UNIX + TCP loopback — `smoke-stream-sock` |
| **Host-share 9p** | **Ayuda de desarrollo** | `smoke-hostshare-9p` (no es virtiofs) |
| **Escritorio T3** | **Fuera de alcance** | WM fuera del árbol kernel |

### Matriz 0.0.1 vs 0.0.2

| Tema | **0.0.1** | **0.0.2** | Más tarde |
|------|-----------|-----------|-----------|
| Gate D1.20 | `release-0.0.1` | mantener verde | — |
| Producto | TCC + Doom+**IWAD** + posix | mismos blockers | — |
| Red | AF_UNIX + TCP loopback | F8 MVP honesto (`smoke-f8-net`) | TCP Internet completo |
| Host share | virtio-**9p** MVP | profundizar opcional | virtiofs+FUSE |
| X11 / WM | fuera | userspace post-red+T2 | T3 no in-kernel |
| CFS / SMP | fuera | fuera | mucho más tarde |

---

## Oleada post-0.0.1 (2026-07-11) — en `master`

| Slice | Qué ganás | Prueba |
|-------|-----------|--------|
| **Storage** | NVMe detect+read; bundle FAT/EXT2/GPT/AHCI | `make smoke-p1-storage` |
| **Power** | `kexec_load` + path loaded; `_S3_` soft resume | `smoke-kexec-load`, `smoke-reboot-s3` |
| **POSIX** | PTY winsz/WINCH; smokes epoll/prlimit/robust | `smoke-pty-winsz`, … |

---

## Antes en desarrollo — ahora estable (0.0.1)

### Hardening (H1–H6)

Ver tabla completa en [`../HARDENING.md`](../HARDENING.md). Resumen: split syscalls (86 L glue), FASE en `kernel/debug/fase_audit.c`, facades sin drivers en `includes/ir0/`, devfs read unificado, budget `.text` en `health`.

### Memoria (COW + lazy)

- Refcount PMM (`pmm_frame_get`/`put`), COW real en fork + break en `#PF` — **`make smoke-mm-cow-lazy`** (desde `62cc512`)
- Pendiente post-0.0.1: COW 2 MiB, file-backed, stack COW opcional
- Detalle: [`../mandocs/esp/mm.md`](../mandocs/esp/mm.md)
### Userspace

- runit, BusyBox (minimal + full opcional), ash interactivo en consola FB
- musl estático, TinyCC (`setup/tcc/`)

### Red

- UDP POSIX mínimo — **estable**
- AF_UNIX + TCP **loopback** — **en 0.0.1** (`smoke-stream-sock`)
- TCP Internet / NIC real — **0.0.2** (F8)

### Host-share (ayuda de desarrollo)

- virtio-9p (`-virtfs`) → `/mnt/host` — `make smoke-hostshare-9p`
- Exec stub PID1: `make smoke-hostshare-exec`
- Exec bajo **runit** PID1: `runit_hostshare_payload_run` + runner `--disk` (referencia: IR0-desktop Xfbdev)
- No confundir con virtiofs/FUSE (aún no)

### Storage

- ATA, MINIX root, lectura `/dev/hda` — **estable**
- FAT16 RO + write audit, EXT2 RO, GPT, AHCI(+NCQ) — **estable para prueba**; NVMe — Closed F6

---

## Logros del roadmap — probables en QEMU con UI

| Tier | Capacidad | Smoke automático | GTK manual |
|------|-----------|------------------|------------|
| T0 | ktests + pseudo-FS (sin dbgshell in-tree) | `make kernel-tests` | `make run-console` / `run-pid1` |
| T1 | runit + ash | `make smoke-tier1` | `make run-fase58e-ash-gui` |
| T1 | permisos multi-UID | `make smoke-multiuser-perms` | `su`/`id` en ash |
| T1 | COW + lazy | `make smoke-mm-cow-lazy` | — |
| T2 | fb0 / input | smokes legacy FASE54¹ | `run-fase58c-fbdev-gui`, Doom GUI |
| Dev | KTM + host | `make ktm-check`, `tests/host` | — |
| Dev | Host-share 9p | `make smoke-hostshare-9p` | — |

¹ `IR0_LEGACY_SMOKE=1` para smokes históricos FASE54/55.

---

## QEMU — probar con interfaz gráfica

```bash
make defconfig
make run-fase58e-ash-gui
```

Construye disco MINIX temporal (runit + BusyBox). **Sin** `IR0_LEGACY_SMOKE=1`.

- Ventana GTK; foco en teclado para escribir en el prompt `#`.
- Serial en la misma terminal.

Otros: `make run-fase58e-ash-gui`, `make run-fase55d-doomgeneric-gui`
(IWAD), `make run` (runit/getty/ash).

Regresión headless:

```bash
make smoke-tier1
make smoke-mm-cow-lazy
make health
```

Ver [`../SETUP.md`](../../SETUP.md) y [`fase58e-ash-interactive-console.md`](fase58e-ash-interactive-console.md).

---

## No estable / stub / Future

TCP Internet (F8 / **0.0.2**), X11/Wayland, WM, SMP/CFS, módulos kernel — ver [`../BACKLOG_REMAINING.md`](../BACKLOG_REMAINING.md). Host-share **9p** ya tiene smoke; virtiofs+FUSE sigue Future.

---

## Documentos relacionados

- [`../STABLE.md`](../STABLE.md) — inglés canónico  
- [`ROADMAP.md`](ROADMAP.md) — backlog completo  
- [`HARDENING.md`](HARDENING.md) — sprints H1–H6  
