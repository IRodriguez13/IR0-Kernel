# IR0 Kernel Changelog

> **Last verified:** 2026-07-30
> **Source of truth:** git history, `make ktm-check`, roadmap smokes in `Makefile`, [`HARDENING.md`](HARDENING.md), [`KTM.md`](KTM.md)

This file tracks user-visible and developer-facing changes per iteration.
For tier backlog see [`ROADMAP.md`](ROADMAP.md). For **what is stable in QEMU** see [`STABLE.md`](STABLE.md).

## [0.0.1-rc4] — 2026-07-30 (last pre-release)

Tag: **`v0.0.1-rc4`**. Version string: `0.0.1-rc4`.  
Policy: after this tag, **bugfixing and stabilization only** until final `v0.0.1`.  
Notes: [`releases/IR0_0.0.1_RC4.md`](releases/IR0_0.0.1_RC4.md), [`releases/NETWORKING_MATRIX.md`](releases/NETWORKING_MATRIX.md).

### Networking product path + stress (ISD BusyBox `ir0_full`)

- `ifconfig` / `route` mutate: `SIOCSIF*`, `SIOCADDRT` / `SIOCDELRT`; Linux `IFF_*`; RTL8139 link polarity.
- AF_UNIX `SOCK_DGRAM` for musl `if_nametoindex`; ICMP `IP_TTL` / `SO_BINDTODEVICE` / `IP_MULTICAST_IF`.
- TCP connect interruptible; SIGALRM → `-ETIMEDOUT` (safe `nc -w`); connect lock released on process exit.
- RTL8139 RX uses **CBR**; overflow drain (fixes RX wedge under load).
- Stress: `setup/pid1/net_command_stress.c` → `NET_STRESS_PASS` / `NC_ONLY_PASS`.

## [Unreleased]

### `/proc/stat` + BusyBox `top` + ATA odd-buffer bounce (2026-07-29)

- `/proc/stat` (`proc_stat_read`): Linux-shaped `cpu`/`cpu0` jiffy lines so
  BusyBox `top` can open `/proc/stat` after `chdir("/proc")`.
- `proc_readdir("/proc")` lists digit PID dirs **before** static registry names
  so `GETDENTS_BATCH_MAX` (24) does not hide all processes from `top`/`ps`.
- ATA/MINIX: bounce path for odd userspace buffers; gate MINIX fast-path when
  dst/src is odd (stops `ATA_BUFFER_ALIGNMENT_SUSPECT` storms).
- MINIX `--format-large`: wipe inode table + rebuild imap (stale inodes from a
  prior pack broke `/etc` inject → `ROOTFS_VERIFY_FAIL … missing component 'etc'`);
  large images use 2048 inodes.
- Smokes: `scripts/smoke_proc_stat_top.py`; `smoke_proc_coherence.py` checks
  `/proc/stat`. Docs: [`VIRTUAL_FILESYSTEMS.md`](VIRTUAL_FILESYSTEMS.md),
  [`PROCESSES.md`](PROCESSES.md) (reparent-to-init note).

### Doom T2 audio/mouse + ESC + TinyCC `/lib/tcc` (2026-07-28)

- doomgeneric IR0: `EV_REL`/buttons → `ev_mouse`; PCM to `/dev/audio` (no `-nosound`);
  smoke requires `MOUSE_CAPS` / `AUDIO_OK` / `AUDIO_WRITE_OK` with QEMU SB16.
- PS/2 set-1 ESC (`0x01`) emits ASCII `0x1b` for BusyBox `vi`.
- TinyCC staging uses absolute `--tccdir=/lib/tcc`; `inject_devtools_minix.sh`
  verifies `libtcc1.a` / `libc.a` / `crt*` on disk.
- Docs: [`testing/DOOM_FASE55D.md`](testing/DOOM_FASE55D.md), [`USERSPACE.md`](USERSPACE.md),
  [`STABLE.md`](STABLE.md).

### BusyBox matrix capture (2026-07-28)

- Drain-to-EOF + streaming needles; host `test_matrix_capture`; QEMU `-m 1024M`.
  See [`testing/BUSYBOX_MATRIX.md`](testing/BUSYBOX_MATRIX.md).

### Kernel / userspace tree boundary (2026-07-25)

- Sibling **[IR0-userspace](https://github.com/IRodriguez13/IR0-userspace)** is the
  public Unix userland (runit, BusyBox, login/doas, rootfs profiles).
- Kernel coupling: [`USERSPACE.md`](USERSPACE.md), `userspace/README.md`,
  `IR0_USERSPACE_ROOT` + `make check-userspace` / `headers_install`.
- **Removed** from IR0: `debug_bins/` (dbgshell + cmd_*), `kernel/init.c`
  (`start_init_process`), and Kconfig `KERNEL_DEBUG_SHELL` / `DEBUG_BINS*`.
- Product boot is only `kexecve("/sbin/init")`. `make run-dbgshell` is retired.
- Mandoc `debug-bins` chapter kept as a **removed** historical note.
- TTY/CSI/termios support + desktop smokes (`smoke_desktop_cmd_matrix`,
  `smoke_desktop_nano_mnt`) land with the same boundary PR.

### Login shell PS1 + BusyBox ash builtins (2026-07-24)

- BusyBox product configs enable `CONFIG_ASH_EXPAND_PRMT` (dynamic `$PWD` in
  prompts) and `CONFIG_ASH_TEST` (`[`/`test` builtins for POSIX scripts).
- Console login execs ash as a login shell (`argv[0] = "-sh"`); `/etc/profile`
  owns `PS1` (`# ` for root, `user@$HOSTNAME:$PWD$ ` otherwise). Password
  reads silence tty echo via `tcsetattr`.
- New gate: `make smoke-runit-login-nonroot` (crypt(3) as `ivan`, uid 1001,
  prompt `ivan@unix:/home/ivan$`). Docs: mandocs en/esp `userspace.md`.
- **MINIX:** `minix_fs_free_inode` wrote the inode bitmap to block 1 (superblock)
  instead of block 2; remount then panicked in `kmalloc(s_zmap_blocks)`. Fixed
  block/index/polarity to match `minix_alloc_inode`; init rejects absurd geometry.

### Structured klog event core (2026-07-24)

- Every klog record carries sequence, boot phase, clock state, raw early ticks,
  severity, component, event id, CPU id, and a bounded message.
- Presentation profiles: quiet/normal/debug/trace via `LOG_PROFILE_*` and
  Multiboot `ir0.loglevel=` / `ir0.trace=open_abi,vfs` (`kernel/cmdline.c`).
- `OPEN_ABI` and MINIX `stat('/') OK` are TRACE-gated; driver ceremony is DEBUG
  with concise INFO one-liners; `klog_smoke` always emits.
- Early x86-64 IDT after TSS; full IDT still installed at `irq_init()`.
- Boot messages: core complete → KTM validation → ready for userspace.
- Login: crypt(3) shadows, setuid session, `ivan` account, Welcome to IR0/Unix.
- Early static ring promotes to a 1024-record heap ring; timestamps remain
  `?.???` until the monotonic clock is online.
- Serial, boot console, `/proc/kmsg`, `/dev/kmsg`, hostshare, and optional KTM
  mirror consume the same event stream. `smoke-klog-ktm-off` proves klog without KTM.
- Product profiles and daily `make run*` use runit/getty/ash.
  (Later: `debug_bins` removed entirely — see wave 2026-07-25 above.)
- Driver probe results emit `KLOG_EVENT_DRIVER_PROBE_RESULT` per registration;
  summary uses `KLOG_EVENT_DRIVER_SUMMARY`.
- First bare-metal boot: sentinel `/etc/ir0-baremetal-booted` + smoke tags
  `FIRST_BAREMETAL_BOOT` / `_SKIP` / `_FAIL` (skipped under hypervisor).
- Docs: [`KLOG.md`](KLOG.md), updated boot/VFS/KTM/BACKLOG.
- ARM64: freestanding `hello_aarch64` when musl CRT is incomplete; early boot
  stubs for `sched_context_switch_to` / `klog_info`. Proof: `smoke-arm64-boot`.

### runit-only + Unix login + stage1 helpers (2026-07-24)

- **PID1** — `irinit` removed; product/tests use runit only (fail-fast retired Make targets).
- **Boot identity** — `IR0 kernel <stamp>` + `ARCH` / `HYPERVISOR` / `PLATFORM` + honest `SMP: UP`.
- **uname** — `IR0` / `unix` / stamp / `IR0/Unix` / machine; `/proc/version` aligned.
- **Login** — runit console service is getty/login (`unix login:` / `Password:`), welcome block, tags `GETTY_READY` / `LOGIN_OK`.
- **Stage1** — `/sbin/fsck.ir0` (MINIX magic or `FSCK_SKIPPED`) + `/sbin/ir0-firstboot` defaults when passwd missing.
- **Drivers** — NOTICE summary `ready/absent/deferred/unsupported/failed` + `DRIVER_SUMMARY_OK`.
- **Smokes** — `smoke-runit-boot`, `smoke-runit-login`; ash interactive logs in first.

### Portable boot logging (2026-07-23)

- **`ir0_boot_serial_ready()`** (`includes/ir0/boot_log.h`, `arch/common/boot_log.c`) —
  same framed BOOT banner on every ISA; freestanding ARM64 early boot uses
  `IR0_FREESTANDING_BOOT` path; product kernels route through klog.
- ARM64 early tags (`ARM64_*`) emit as COMP `SMOKE` (still greppable for autokill).

### Boot banner, SB16, Class B, desk (2026-07-23)

- **Banner-first serial** — `klog_boot_hold` until after `serial_init`; first
  framed line identifies `IR0 kernel <version>` (`kernel/main.c`, `ktm/klog.c`).
- **SB16 QEMU** — audiodev wiring for QEMU 8+; `SB16_DSP_OK` smoke tag;
  `make smoke-sb16-probe` (`scripts/make/boot-audio.mk`). Adlib may stay ABSENT (not a gate fail).
- **Blocked syscall yield** — `kernel_idle_poll_nosched` in clock_wait loops; no nested
  `sched_schedule_next` from poll/stdin wakes during sleep.
- **Class B** — host invariant + KTM inject/repair gates (`scripts/make/class-b.mk`,
  `smoke-class-b-mitigated` / `smoke-class-b-repro`); `skip_prev_save` on IRQ preempt.
- **AF_UNIX stream** — `fd_refs` + `sock_stream_is_slot` guard (close must not
  `sock_udp_release` static stream slots) — desk session path.
- **Docs** — `KTM.md`, `SCHEDULING.md`, `DRIVERS.md`, `mandocs/en/boot.md`, desk
  `DESK_SESSION.md`.

### Logging / KTM hygiene (2026-07-21)

- **klog hub** — human serial format `[ts] [LEVEL] [COMP] msg` in `ktm/klog.c`; facade `<ir0/ktm/klog.h>` (legacy `includes/ir0/klog.c` removed).
- **ASSERT_BATCH** — collapse happy-path soft asserts (`process.wait_drain` / `reclaim_exit`) into one `KTM|…|ASSERT_BATCH|…` line.
- **`CONFIG_KTM_SERIAL_VERBOSE`** — default **n**; product/desk boots skip noisy CHECKPOINT/PROBE mirrors.
- **Autokill** — QEMU host stderr → sibling `*.qemu-stderr` (guest serial log stays clean).
- **runit** — `ir0_smoke_tag.h` + `runit_hostshare_payload_run` / `runit_pause_run` for hybrid 9p desk/KTM smokes.
- **Docs** — `KTM.md` logging layers; `ai_driven_dev/rules/ir0-version-stamp.mdc` (lockstep `version.h` ↔ upstream tags).

---

## [0.0.1] — 2026-06-23 — stable baseline + hardening closed

### Documentation

- **`Documentation/STABLE.md`** (+ `esp/STABLE.md`) — release 0.0.1 checklist: formerly in-dev items closed, roadmap achievements testable in QEMU (serial + GTK), explicit non-goals.
- **`ROADMAP.md`**, **`HARDENING.md`**, **`README.md`** — aligned with H1–H6 done and 0.0.1 scope.
- **`make health`** — includes `kernel-text-budget`.

### Hardening (H1–H6 closed)

- **H1** — `kernel/syscalls.c` 86 lines; submodules under `kernel/syscalls/`.
- **H2** — FASE43–48 in `kernel/debug/fase_audit.c`; `process.c` ~3014 L; `process_reap_zombie_child()` production path.
- **H3** — 0 `#include <drivers/` in `includes/ir0/` (arch-guard rule 14).
- **H4** — `test_musl_cred_abi`, `includes/ir0/abi/musl_cred_abi.h`.
- **H5** — `devfs_resolve_read_fd()`; dead block_dev stubs removed.
- **H6** — `make kernel-text-budget` (~815754 B / cap 850000); USB describe fix.

### Release baseline (maintainer sign-off)

Closed for 0.0.1 without re-smoke unless regression: **runit**, **BusyBox ash/applets**, **TinyCC**, **COW fork**, **lazy alloc**, tier1 POSIX smokes, T2 fb/input/Doom GUI targets.

### Permissions & lifecycle (included in 0.0.1)

- **`ir0_access_from_stat_groups()`** — supplementary group checks.
- **ARCH-3** — devfs FD release; fork rollback FD cleanup.
- **ARCH-4** — FASE50-gated exit/destroy serial noise.

### ktest / block / devfs

- **ktest open ABI** — `KTEST_O_*` Linux flags.
- **ATA sector count** — IDENTIFY + `ata_get_size()`.
- **devfs read** — per-FD offset; unified `sys_read` path.

### Validation

| Check | Result |
|-------|--------|
| `make kernel-tests` | 29/29 |
| `make -C tests/host run` | 12/12 |
| `make arch-guard` | OK |
| `make build-matrix-min` | OK |
| `make kernel-text-budget` | OK |
| `make smoke-tier1` | prior green |

---

## [0.0.1-pre] — 2026-06-23 hardening oleada (pre-doc consolidation)

### Permissions & lifecycle (T0/T1)

- **`ir0_access_from_stat_groups()`** — supplementary group checks for path permission facades (`fs/permissions.c`, `kernel/credentials.c`, `includes/ir0/path_routed.c`, `kernel/syscalls.c`).
- **ARCH-3** — `process_release_fds()` closes devfs nodes; `fork_rollback()` releases child FD table on failure.
- **ARCH-4** — exit/destroy/exec-fail serial storms gated with `CONFIG_DEBUG_FASE50`.

### ktest / block / devfs fixes

- **ktest open ABI** — `KTEST_O_*` Linux flags in `kernel/test/ktest_harness.h`; fixes mount tmpfs/multi-fs/longest-prefix contracts (see [`HARDENING.md`](HARDENING.md)).
- **ATA sector count** — `ata_devices[].size` populated on IDENTIFY; `ata_get_size()` fallback from `capacity_bytes`.
- **devfs** — `sys_read` honors per-FD offset for bound devfs files.
- **`sys_mount`** — suppress misleading stderr on `-EBUSY`.

### Structural hardening (H2/H5/H6)

- **`process_t`** — FASE44/46 audit fields removed; side table in `kernel/debug/fase_audit.c`.
- **`sys_read`** — single devfs path via `devfs_resolve_read_fd()`.
- **`make kernel-text-budget`** — `.text` cap 850000 B (current ~822202 B).

- **`kernel/syscalls.c`** — 86 lines (glue only); logic in `kernel/syscalls/*` (`fs_path_syscalls`, `validate_user`, extended mm/process/io).
- **H3 partial** — `includes/ir0/partition.h`, `block_dev.h` decoupled from `<drivers/...>`.
- **H4** — `test_musl_cred_abi` + `includes/ir0/abi/musl_cred_abi.h`; host 12/12.
- **H5/H6** — dead block_dev stubs removed; USB describe fix; FASE46 gated.

### Validation (post-ARCH-1)

- `make kernel-tests` — 29/29; `runtime-mount-check`; arch-guard; build-matrix-min; host 12/12.
- `make smoke-runit-boot` — 3/3 green.

---

## [0.0.0-pre] — 2026-06-17 iteration

### Stability & POSIX (T1)

- **wait4(pid, NULL, …)** — Fixed hang in FASE40 / `smoke-mm-cow-lazy`: arch context switch now resumes blocked waiters when `wait_status_ptr` is NULL but `irq_frame_saved` is set; wake path aligned in `process_exit` and `syscall_wake_blocked_on_child`.
- **init_fork_mem_smoke** — `touch_mmap_pages()` marked `noinline` to avoid `-Os` codegen breaking lazy COW touch.

### Build / CI

- **`CONFIG_TICK_RATE_HZ`** — `drivers/timer/clock_system.c` includes `config.h`; `Makefile` always passes `-DCONFIG_TICK_RATE_HZ` (default 1000). Fixes `kernel-x64-test.bin` build without `.config`.

### ARCH-1 syscall split

- **`kernel/syscalls/time_syscalls.c`** — `sys_gettimeofday`, `sys_clock_gettime` extracted from `kernel/syscalls.c`.
- **`kernel/syscalls/syscall_dispatch.c`** — Linux x86-64 syscall table + `syscall_dispatch()` (~400 lines moved out of monolith).

### Documentation (stable contracts)

- **`Documentation/CHANGELOG.md`** — iteration log (EN + `esp/` mirror).
- **`Documentation/ai_driven_dev/ktm.md`** — KTM panic site, macros, inventory gate.
- **`PROCESSES.md`**, **`MEMORY.md`**, **`TOOLING.md`**, **`VIRTUAL_FILESYSTEMS.md`**, mandocs `process`/`interrupts` — aligned with wait4 NULL, COW, `/dev/hda`, panic path.

### Storage / devfs

- **`fs/devfs.c`** — `dev_disk_read` fix for QEMU IDENTIFY.
- **`fs/fat16_disk.c`** — read-only FAT16 on `hda` / `hda1`…; `fat16_fs.c` routes virtual `fat0` vs block devices.
- ktest `fat16_bpb_probe`.

### KTM (Kernel Trace Module)

- **`panic()` → macro** — `includes/ir0/oops.h` maps legacy `panic(msg)` to `panicex` with **callsite** `__FILE__` / `__LINE__` / `__func__` (removed wrapper in `oops.c` that always reported `oops.c`).
- **`PANIC_HW` / `PANIC_MEM`** — Level-specific panic macros for hardware and OOM paths.
- **Fault path** — `arch/x86-64/sources/fault.c`, HPET, arch divide-by-zero use `PANIC_HW` where appropriate.
- **`[KTM][PANIC_SITE]`** — `ktm_panic_site_emit()` logs file, line, caller, and message on every `panicex`.
- **`ktm_classify_kernel_panic_ex()`** — Classification uses panic level first, then message heuristics (`KERNEL_HW_FAULT`, `KERNEL_OOM`, etc.).
- **`scripts/ktm_panic_inventory.py`** — CI gate in `make ktm-check`; host test `test_ktm_panic_inventory_contract`.

### Validation (this iteration)

| Check | Result |
|-------|--------|
| `make kernel-x64.bin` | OK |
| `make ktm-check` | OK |
| `kernel-tests` (32/32) | prior green |
| `smoke-mm-cow-lazy`, tier1 smokes | prior green |
| `tests/host` | 10 tests incl. panic inventory |
| `syscalls.c` line count | ~3536 (was ~3950; dispatch extracted) |

### Known gaps (historical snapshot — see [0.0.1] / [`STABLE.md`](STABLE.md) for current)

- ~~P1-storage: FAT16 on `block_dev`~~ — MVP read-only in 0.0.1
- ~~ARCH-1: further `syscalls.c` split~~ — done H1 (86 L)
- POSIX-1: futex robust / musl pthread edge cases — still open
- Audit noise on some lazy-touch paths — gated `IR0_DEBUG_PROC`

---

## Format

New entries go under `[Unreleased]` at the top. On a tagged release, rename the section to the version/date and open a fresh `[Unreleased]`.
