# IR0 — Stable baseline (release 0.0.1)

> **Last verified:** 2026-07-30  
> **Source of truth:** `make release-0.0.1` / CTR gates, `Makefile` smoke targets,  
> hostshare-exec + F8 **honest MVP** (`smoke-f8-net`) + FAT secondary ship note,  
> runit PID1 + hybrid 9p payload (`runit_hostshare_payload_run`) for desktop X smoke,  
> merge `56a3f7b` (dev→master: kexec/S3, P1-storage, P1-T1), Future F2–F6,  
> `Documentation/releases/IR0_0.0.1_SCOPE.md`, [`BACKLOG_REMAINING.md`](BACKLOG_REMAINING.md),  
> [`releases/IR0_0.0.1_RC4.md`](releases/IR0_0.0.1_RC4.md), [`releases/NETWORKING_MATRIX.md`](releases/NETWORKING_MATRIX.md),  
> critical hybrid KTM battery ([`KTM.md`](KTM.md) § critical product battery),  
> **SEP-2** userspace split: product rootfs built by the **`IR0-userspace`** / **ISD** sibling
> (`IR0_USERSPACE_ROOT`, `make headers_install`) — see [`USERSPACE.md`](USERSPACE.md).
> In-kernel dbgshell / `debug_bins/` **removed** (2026-07-25).

This document is the **single checklist** for what is **stable enough to run and test in QEMU** (serial and GTK UI), what was formerly **in development** and is now closed for **0.0.1**, and what remains **future work** (see [`ROADMAP.md`](ROADMAP.md) P1+).

Maintainer sign-off (2026-06-26): treat the items below as **done for 0.0.1-rc1** after `make release-0.0.1` is green **3/3 consecutivas** (evidencia en `/tmp/release-run-{1,2,3}.log`).

**Honesty note (2026-07-10):** older STABLE/SCOPE text claimed fork COW while the kernel still
full-copied user pages. Real share-on-fork + write-fault break landed in `62cc512`; proof remains
`make smoke-mm-cow-lazy`. KTM is the canonical in-kernel test plane (`make ktm-run`,
`make ktm-userdev-run`); see [`ai_driven_dev/ktm.md`](ai_driven_dev/ktm.md).

### Tag prep vs release ship (2026-07-30)

| Artifact | Meaning |
|----------|---------|
| **`v0.0.1-rc4`** | **Last pre-release** before final. Version string `0.0.1-rc4`. Networking stress green (`NET_STRESS_PASS`). Details: [`releases/IR0_0.0.1_RC4.md`](releases/IR0_0.0.1_RC4.md). |
| **Post-rc4 work** | **Bugfixing + stabilization only** — no new 0.0.1 product surface. |
| **`v0.0.1` (final ship)** | Maintainer: **manual QEMU/VM** “listo” + SCOPE blockers cleared. |
| Earlier | `v0.0.1-rc2` / `rc1` / `pre.1` — historical tag prep only. |

Do **not** treat any `rc*` tag as “0.0.1 done”. Final git tag `v0.0.1` waits on ship criteria above.

---

## Merge → `master` — critical product gates (maintainer)

**Policy (2026-07-17):** before merging `dev` → `master`, the software that must **not** regress is
**TinyCC in-guest** (compile + run), **Doom T2 with real IWAD** (doomgeneric loads WAD + frames),
**BusyBox product applets** (KTM manifest), **runit as PID1**, and **broader userspace**
(`smoke-posix-depth` or `smoke-tier1`). CTR/`smoke-release-0.0.1` alone are not enough.

```bash
# Critical hybrid KTM battery (must PASS) — do not merge to master if any red
make -s ktm-userdev-busybox-manifest-run     # BUSY-2; alias: smoke-busybox-manifest
make -s IR0_LEGACY_SMOKE=1 ktm-userdev-doom-55d-run   # or smoke-fase55d-doomgeneric
make -s ktm-userdev-tcc-power-halt-run       # or smoke-tcc-power-halt; requires POWER_TCC_KTM_OK
make -s smoke-runit-boot                     # product PID1 (not userdev)

# Broader userspace + hygiene
make smoke-posix-depth                       # or: make smoke-tier1
make ctr
make smoke-tier1
make smoke-release-0.0.1
```

Default IWAD: `REAL_WAD_PATH` → `/home/ivanr013/Escritorio/universal-doom/DOOM1.WAD`
(file must exist on the merge host). Stub `smoke-fase55b-doom-stub` remains a fast regression
aid, **not** the merge blocker.

If TCC (`POWER_TCC_KTM_OK`), Doom+WAD (`KTM_DOOM_55D_OK` / frame loop), BusyBox manifest,
or `smoke-runit-boot` is red, **do not merge to `master`** even if release/CTR are green.
Status honesty: TCC may still hang at static link in some QEMU runs — treat a red TCC smoke as
a merge blocker, not as “optional WARN”.

---

## Release gate (0.0.1) — D1.20

```bash
make release-0.0.1          # kernel-text-budget + smoke-release-0.0.1
make smoke-release-0.0.1    # bundle determinista (sin health duplicado)
```

**`smoke-release-0.0.1` incluye (en orden):**

| Gate | Valida |
|------|--------|
| `roadmap-phase1-stability` | `kernel-x64.bin`, host tests, `kernel-tests`, memsafe, analyze, `build-matrix-min`, `arch-guard`, `smoke-mm-cow-lazy` |
| `linux-abi-audit` | contrato `brk` Linux↔IR0 + host/ktest |
| `smoke-runit-ash-interactive` | runit + BusyBox ash + TTY `echo hi` |
| `smoke-fat16-mount` | FAT16 read-only en `/dev/hdb` |

**`release-0.0.1` añade:** `kernel-text-budget`.

**Fuera del release gate D1.20 (pero sí bloquean merge a `master` — ver arriba):**

- `smoke-fase52-tcc` / `smoke-tcc-power-halt` — TinyCC in-guest (producto).
- `smoke-fase55*` Doom — T2 fullscreen path (producto).
- `IR0_LEGACY_SMOKE=1` fase53b — superseded por ktests + gate D1.20.

Inventario honesto: [`Documentation/releases/IR0_0.0.1_SCOPE.md`](releases/IR0_0.0.1_SCOPE.md).

**Debug release:** `CONFIG_DEBUG_D1_DIAG=n`, `CONFIG_KTM_MALLOC_FORENSICS=n`, `CONFIG_DEBUG_PAGE_FAULTS=n` en defconfig — boot sin tags `[D1.*]` / `[PF_AUDIT]`.

---

## Post-0.0.1 oleada (2026-07-11) — landed on `master`

| Slice | What users get | Proof |
|-------|----------------|-------|
| **Storage** | NVMe detect+read; FAT/EXT2/GPT/AHCI bundle | `make smoke-p1-storage` |
| **Power** | `kexec_load` + reboot loaded path; ACPI `_S3_` soft resume | `smoke-kexec-load`, `smoke-reboot-s3` |
| **POSIX** | PTY `TIOCSWINSZ` + WINCH queue; epoll/prlimit/robust smokes | `smoke-pty-winsz`, `smoke-epoll-basic`, … |
| **KTM** | Typed `PIPE_*` events; angular includes | `make ktm-run` (pass=16) |

AHCI NCQ (F2) and DSDT `_S5_` typed poweroff (F3) remain as previously landed Future items.

---

## Release 0.0.1 scope

| Area | Status | Primary proof |
|------|--------|---------------|
| **Hardening H1–H6** | **Closed** | [`HARDENING.md`](HARDENING.md); `make health` |
| **runit boot** | **Stable** | `make smoke-runit-boot` |
| **BusyBox ash + applets** | **Stable (product)** | Manifest `IR0-userspace/packages/busybox/required_applets.txt`; rootfs inject via `busybox_inject_manifest.sh`; ship smoke: `make ktm-userdev-busybox-manifest-run` (alias `smoke-busybox-manifest`) → `BUSYBOX_MANIFEST_OK` + `KTM_USERDEV_OK`. Extended probe: `smoke-fase58l-busybox-coreutils` |
| **TinyCC in-guest** | **Merge-critical** | `ktm-userdev-tcc-power-halt-run` / `smoke-tcc-power-halt` — requires `POWER_TCC_KTM_OK` — **blocker for `master`** |
| **COW fork** | **Stable** | `make smoke-mm-cow-lazy` (FASE40 A–F) |
| **Lazy allocation** | **Stable** | `CONFIG_LAZY_ANON_MMAP`, `CONFIG_LAZY_BRK_HEAP`; same smoke |
| **T1 POSIX slice** | **Stable** | tier1 + musl manifests; cred/pthread/setuid smokes |
| **Privilege model + doas** | **Stable** | set-id exec + `AT_SECURE` + `no_new_privs`; `smoke-setuid-exec`, `smoke-passwd`, `smoke-doas` (grant / env / **persist** / deny) |
| **Product profiles** | **Stable** | `/etc/ir0-profile`: `development` (root autologin + warning), `desktop` (`hostname login:` + `/etc/ir0-noroot`), `appliance` (`CONSOLE_NO_LOGIN`) |
| **T2 graphics / Doom** | **Merge-critical** | Real IWAD: `IR0_LEGACY_SMOKE=1 ktm-userdev-doom-55d-run` (alias `smoke-fase55d-doomgeneric`; requires `KTM_DOOM_55D_OK` **plus** `DOOMGENERIC_MOUSE_CAPS_OK` / `AUDIO_OK` / `AUDIO_WRITE_OK` + SB16) — **blocker for `master`**; stub 55b = fast aid only. See [`testing/DOOM_FASE55D.md`](testing/DOOM_FASE55D.md) |
| **Local net** | **Stable for test** | `AF_UNIX` (pathname + abstract + `socketpair` + `SCM_RIGHTS` multi-fd + real `getpeername` + SOCK flags/`accept4`/`MSG_PEEK`/`SO_REUSEADDR`) + **TCP loopback** — `smoke-stream-sock` / `smoke-scm-rights` / `smoke-unix-abstract` / `smoke-unix-harden` / `smoke-unix-flags` |
| **Host-share 9p** | **Dev aid / product-dev** | QEMU `-virtfs` → `/mnt/host`; exec: `smoke-hostshare-exec`; recipe: [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md). **Not** VirtualBox / virtiofs |
| **X11 mini (out of tree)** | **Stable for test** | Optional `make smoke-desk-xfbdev` (sibling [`IR0-desktop`](../../IR0-desktop/); **not** in `release-0.0.1`) → `IR0_XFBDEV_SMOKE_OK` |
| **DESK soft session (out of tree)** | **Stable for test** | Optional `smoke-desk-wm` / `classicube` / `play` — soft fb demos; **smoke ≠ ship ISO** ([`TREE_CONTRACT`](../../IR0-desktop/Documentation/TREE_CONTRACT.md)) |
| **DESK X session (out of tree)** | **Stable for test (flaky)** | Optional `smoke-desk-session` — often `IR0_DESK_SESSION_OK`; residual kernel panic — [`DESK_SESSION.md`](../../IR0-desktop/Documentation/DESK_SESSION.md) |
| **Tree separation (SEP-1)** | **Policy** | Kernel / desktop / third-party contract + [`ARCH_DEBT_SEP.md`](ARCH_DEBT_SEP.md); desktop rootfs skeleton fail-closed |
| **T3 desktop** | **Partial (soft demo)** | Soft fb session out of tree; TinyX X-WM / Mesa ClassiCube lab-blocked; no product desktop ISO yet |

### Version matrix (0.0.1 vs 0.0.2)

| Topic | **0.0.1** (ship) | **0.0.2** (next tag) | Later |
|-------|------------------|----------------------|-------|
| Gate D1.20 | `smoke-release-0.0.1` / `release-0.0.1` | keep green | — |
| Product | TCC + Doom+**IWAD** + posix/tier1 | same blockers | — |
| Network | AF_UNIX + TCP **loopback** | F8 honest MVP (`smoke-f8-net`: NIC L3 + guest/wire/listen) | full Internet TCP / congestion |
| Host share | virtio-**9p** MVP + exec (`smoke-hostshare-9p`, `smoke-hostshare-exec`) | subdirs / more FS ops | virtiofs + FUSE when ready |
| ARM | bring-up (F7*) — does not block x86 ship | continue port | — |
| X11 / WM | **out** (kernel prep OK) | userspace after usable net + T2 | never in-kernel T3; kernel: fb/MAP_SHARED + unix + SysV shm + memfd |
| CFS / SMP | **out** | **out** | much later; not with X11 |

Do **not** claim “virtiofs done” for the 9p path.

---

## Formerly in development — now stable (0.0.1)

These items were tracked as partial, WIP, or hardening backlog. They are **closed for this release** (code + smokes; re-run gates only on regression).

### Architecture hardening (H1–H6)

| Sprint | Deliverable | Evidence |
|--------|-------------|----------|
| **H1** ARCH-1 | `kernel/syscalls.c` **86 lines**; submodules under `kernel/syscalls/` | `wc -l kernel/syscalls.c` |
| **H2** FASE | FASE43–48 in `kernel/debug/fase_audit.c`; hooks on hot path | `process.c` ~3014 L |
| **H3** facades | 0 `#include <drivers/` in `includes/ir0/*.h`; `sched.h` ops facade | `make arch-guard` |
| **H4** host ABI | `test_musl_cred_abi`, `includes/ir0/abi/musl_cred_abi.h` | `make -C tests/host run` 12/12 |
| **H5** devfs read | `devfs_resolve_read_fd()` in `fs_syscalls.c` | `kernel-tests` |
| **H6** size budget | `make kernel-text-budget` in `make health` | ~815754 B / cap 850000 |

### Memory (COW + lazy)

| Item | Paths | Proof |
|------|-------|-------|
| PMM frame refcount | `mm/pmm.c` (`pmm_frame_get`/`put`) | FASE40 + ktests |
| Fork COW (share + WP break) | `mm/paging.c`, `arch/x86-64/sources/fault.c` | `smoke-mm-cow-lazy` (real since `62cc512`) |
| Lazy anon `mmap` | `CONFIG_LAZY_ANON_MMAP` | same smoke |
| Lazy `brk` | `CONFIG_LAZY_BRK_HEAP` | same smoke |
| ELF PT_LOAD VMA metadata | `kernel/elf_loader.c`, `process_user_vma_prot()` | exec + mmap ktests |

**Not required for 0.0.1:** 2 MiB huge-page COW, file-backed COW, optional stack COW (see ROADMAP P2/PERF-2).

Details: [`mandocs/en/mm.md`](mandocs/en/mm.md), [`MEMORY.md`](MEMORY.md).
### Userspace / init

| Item | Paths | Proof |
|------|-------|-------|
| Product userspace tree | `IR0-userspace/` (`fetch build rootfs image`), `IR0_USERSPACE_ROOT` | `check-userspace` fails loudly if the sibling is missing |
| runit PID1 | `IR0-userspace/services/`, `load-userspace-runit` | `smoke-runit-boot` |
| BusyBox minimal | `IR0-userspace/packages/busybox/fase58_busybox.config`, `build-busybox-fase50-min` | `smoke-tier1` |
| BusyBox extended applets | `build-busybox-fase58-full`, `smoke-fase58l-busybox-coreutils` | optional extended probe |
| BusyBox product manifest | **BUSY-1 / BUSY-2 Closed** | `required_applets.txt` + `ktm-userdev-busybox-manifest-run` (`BUSYBOX_MANIFEST_OK`) |
| Interactive ash on FB console | `includes/ir0/console.c`, TTY echo | [`fase58e-ash-interactive-console.md`](fase58e-ash-interactive-console.md) |
| musl static toolchain | `MUSL_CC`, `kernel-x64-userspace.iso` | tier1 smokes |
| Public UAPI export | `includes/uapi/`, `make headers_install DESTDIR=…` | `make -C IR0-userspace headers` |
| TinyCC | `setup/tcc/build-fase52.sh` | `build-tcc-fase52` |

### Networking (UDP + local streams)

| Item | Proof |
|------|-------|
| `socket` / `bind` / `sendto` / `recvfrom` / `connect` | tier1 manifest, `runtime-net-check` |
| UDP `accept` → `-EOPNOTSUPP` | documented in mandocs |
| AF_UNIX + TCP **loopback** `send`/`recv` | `smoke-stream-sock` (`STREAM_SENDRECV_OK`) — **in 0.0.1** |
| NIC L3 reach (rtl8139) | **F8-1 MVP** — `make smoke-nic-reach` requires ICMP/ARP proof (`NIC_PING_REPLY_OK` → `F8_NIC_REACH_OK`) |
| NIC L3 reach (virtio-net legacy) | **F8-1 MVP** — `make smoke-nic-reach-virtio` (`virt0`; QEMU `virtio-net-pci,disable-modern=on`) |
| E1000 NIC | **BLOQUEADO** — `CONFIG_DRV_NIC_E1000=n`; no driver sources |
| TCP guest→host wire | **F8-2 MVP** — `make smoke-tcp-guest` → **10.0.2.2:8889** hostfwd (not in-memory); `F8_TCP_GUEST_SENDRECV_OK` |
| TCP wire send+recv | **F8-3 MVP** — `make smoke-tcp-wire` peer evidence only (`F8_TCP_WIRE_SENDRECV_OK`); no synthetic CC as ship gate |
| TCP listen host→guest | **F8 listen MVP** — `make smoke-tcp-listen` (`F8_TCP_LISTEN_OK`); battery `make smoke-f8-net` |
| Full Internet TCP | **Not claimed** — no arbitrary Internet, no peer-driven congestion/SACK as product |

### Host share (dev aid)

| Item | Proof |
|------|-------|
| virtio-9p + VFS fstype `9p` → `/mnt/host` | `make smoke-hostshare-9p` (`HOSTSHARE_9P_OK`, host file visible) |
| 9p getattr + chunked read (ELF-sized) | `virtio_9p_stat_file` / `virtio_9p_read_file`; `hs_stat`/`hs_read` |
| Exec payload from share | `make smoke-hostshare-exec` — stub `init_hostshare_exec` mounts `ir0share`, `execve(/mnt/host/ir0_payload)` |
| Exec payload under **runit** PID1 | `IR0-userspace/services/runit_hostshare_payload_run.c` as supervised `sv/*/run` + `ktm_userdev_runner.py --disk` (prebuilt runit rootfs); reference: IR0-desktop Xfbdev smoke |
| virtiofs / FUSE | **Not implemented** — post-9p when FUSE exists |

### Storage (phase2 baseline)

| Item | Status | Notes |
|------|--------|-------|
| ATA + `/dev/hda` read | **Stable** | `block_hda_read_contract` ktest |
| MINIX root on `disk.img` | **Stable** | **Ship root FS** — default boot layout |
| VFS mount contracts | **Stable** | `runtime-mount-check`, mount ktests |
| FAT16 on `block_dev` | **Stable (secondary)** | Montable on `/dev/hdb`; `smoke-fat16-mount` in release gate; write audit `linux-abi-audit-vfs-write-fat`; **not** FAT-as-root in 0.0.1 |
| EXT2 read-only | **Stable for test** | `smoke-ext2-mount` |
| GPT probe | **Stable for test** | `smoke-gpt-partition` |
| AHCI detect/read/multi + NCQ | **Stable for test** | `smoke-ahci-read` (`AHCI_NCQ_OK` / `UNSUPPORTED`) |
| NVMe detect+read | **Stable for test** | `smoke-nvme-read` |
| NVMe advanced (MSI, multipath) | **Future** | backlog F7+ |

### Power / reboot (post-0.0.1)

| Item | Status | Proof |
|------|--------|-------|
| reboot/halt/poweroff + BusyBox applets | **Stable for test** | `smoke-runit-busybox-*` |
| ACPI FADT map + `_S5_` SLP_TYP | **Stable for test** | `ACPI_S5_OK` + `ACPI_PM1A_POWEROFF` |
| kexec stub (reboot, no load) | **OK** | `REBOOT_KEXEC_STUB` |
| kexec_load MVP | **OK** | `smoke-kexec-load` |
| S3 soft resume | **OK** | `smoke-reboot-s3` (`ACPI_S3_OK`) |
| SW_SUSPEND stub (return 0) | **Stub** | `SYSTEM_SUSPEND_ENTER` |

---

## Roadmap achievements — stable for QEMU test

Everything in this table was reached in at least one oleada and has a **runnable proof** (automated smoke and/or documented GUI target). Use it to **explore the system in QEMU with UI**.

| Tier | Capability | Automated smoke | QEMU GTK (manual) |
|------|------------|-----------------|-------------------|
| **T0** | Kernel contracts (ktest / linux-abi; no in-tree dbgshell) | `make kernel-tests` | `make run-console` / `run-pid1` (runit/getty/ash) |
| **T0** | pseudo-FS `/proc` `/dev` `/sys` | ktests, `runtime-mount-check` | explore from ash after tier1 boot |
| **T1** | runit + services | `make smoke-runit-boot` | `make run-fase58e-ash-gui` |
| **T1** | BusyBox ash interactive | `make smoke-tier1` | `make run-fase58e-ash-gui` |
| **T1** | multi-UID / permissions | `make smoke-multiuser-perms` | `su` / `id` in ash (rootfs) |
| **T1** | pthread / futex path | `make smoke-musl-pthread` | — |
| **T1** | setuid exec | `make smoke-setuid-exec` | — |
| **T1** | MM COW + lazy | `make smoke-mm-cow-lazy` | — |
| **T2** | framebuffer `/dev/fb0` | legacy `smoke-fase54a-fbdev`¹ | `make run-fase58c-fbdev-gui` |
| **T2** | input `/dev/events0` | legacy `smoke-fase54b-input`¹ | keyboard in GTK window |
| **T2** | Doom + real IWAD | `smoke-fase55d-doomgeneric`¹ | `make run-fase55d-doomgeneric-gui` |
| **Dev** | KTM boot + userdev | `make ktm-run`, `make ktm-userdev-run` | — |
| **Dev** | Host-share 9p (KTM artifacts + exec) | `make smoke-hostshare-9p` / `smoke-hostshare-exec` | — |
| **Dev** | KTM inventory | `make ktm-check` | — |
| **Dev** | arch + host contracts | `make arch-guard`, `make -C tests/host run` | — |

¹ Legacy phase smokes: `IR0_LEGACY_SMOKE=1 make smoke-fase54a-fbdev` (see `setup/make/legacy-smokes.mk`). Tier1 smokes above do **not** require legacy flag.

### Phase gates (batch confidence)

```bash
make roadmap-phase1-stability    # CTR + smoke-mm-cow-lazy
make roadmap-phase2-driver-expansion   # + runtime-net + runtime-mount
```

---

## QEMU — test the system with UI

**Prerequisites:** `make deptest` green; `musl-tools` for userspace ISO; QEMU with GTK (`-display gtk`).

### Quick path: runit + ash (recommended)

```bash
make defconfig
make run-fase58e-ash-gui
```

Builds its own temporary MINIX disk (runit + BusyBox). **No** `IR0_LEGACY_SMOKE=1` — target lives in main `Makefile`.

- **Display:** QEMU GTK window (keyboard focus required for typing).
- **Expect:** BusyBox ash prompt `#`, echo on Enter, basic applets (`ls`, `pwd`, `echo`).
- **Serial:** stdio in the same terminal (tags for bring-up).

Details: [`fase58e-ash-interactive-console.md`](fase58e-ash-interactive-console.md), [`SETUP.md`](../SETUP.md).

### Alternative GUI entry points

| Target | What you get |
|--------|----------------|
| `make run` | Product path: runit → getty/login → BusyBox ash |
| `make run-fase58e-ash-gui` | runit → BusyBox ash (recommended) |
| `make run-fase58c-fbdev-gui` | Framebuffer probe on `/dev/fb0` |
| `make run-fase55d-doomgeneric-gui` | doomgeneric stub (needs IWAD + inject — see SETUP) |

### Headless regression (local — no GitHub Actions gate)

Automated validation is **local** (the old GitHub `Tests` workflow was removed to
stop permanent red noise on `master`). Run before merge:

```bash
make smoke-tier1              # runit-boot + ash-interactive (automated)
make smoke-mm-cow-lazy
make kernel-tests
make -C tests/host run
make health                   # analyze + text budget + memsafe + ktests
```

---

## CTR gates (every merge)

```bash
make -s kernel-x64.bin
make -s kernel-x64-test.bin && make -s kernel-tests
make -s runtime-mount-check
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
make -s kernel-text-budget
```

After T1-sensitive changes: `make smoke-tier1` or at minimum `make smoke-runit-boot` ×3.

---

## Explicitly **not** stable (do not claim in 0.0.1)

| Item | Where tracked |
|------|----------------|
| TCP stream syscalls | ROADMAP P3 |
| AF_UNIX, X11, Wayland | ROADMAP P3 / T3 prep |
| WM + panel | T3 — out of kernel tree |
| SMP, CFS scheduler backend | ROADMAP P2 |
| Kernel module loader (MOD-*) | ROADMAP P2 |
| NVMe MVP | BACKLOG F6 |
| FACS waking-vector hard S3 | BACKLOG Future (soft resume landed) |
| `pthread_create` via musl libc only (no inline clone in smoke) | ROADMAP POSIX-1 |
| Full PTY / job control | ROADMAP P1-term |

---

## Related docs

| Doc | Topic |
|-----|--------|
| [`ROADMAP.md`](ROADMAP.md) | Full backlog P0–P3, evolution milestones |
| [`HARDENING.md`](HARDENING.md) | H1–H6 sprint detail |
| [`CHANGELOG.md`](CHANGELOG.md) | Iteration log |
| [`SETUP.md`](../SETUP.md) | Build, disk inject, Doom/TCC optional |
| [`TOOLING.md`](TOOLING.md) | Makefile targets taxonomy |
| [`esp/STABLE.md`](esp/STABLE.md) | Spanish mirror |
