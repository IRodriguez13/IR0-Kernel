# IR0 — Post-0.0.1 backlog (honest remaining work)

> **Last verified:** 2026-07-24
> **Source of truth:** `Documentation/ROADMAP.md`, code under `fs/`, `drivers/storage/`,
> `scripts/linux_abi/`, Makefile gates. Prefer this file for **what is still open**;
> ROADMAP holds history and tier %.

## Closed (do not re-open without regression)

| Item | Evidence |
|------|----------|
| Linux ABI cola 0.0.1 | `IR0_0.0.1_ABI_BOARD.md` |
| `/proc`/`/sys` → `fd_table` + dup | `linux-abi-audit-dup` |
| `/heart` MVP + src expand | `IR0_LEGACY_SMOKE=1 smoke-heart` (`HEART_SRC_EXPAND_OK`, cmdline/osrelease) |
| `/proc/cmdline` + `/sys/kernel/osrelease` | `smoke-heart` mirrors |
| ARCH-3 virtual fds / `FD_SYS`/`FD_DEV` | `agent-fast` + tier1 smokes |
| Process ownership split + backend facades | `kernel-x64.bin` + `arch-guard` |
| FAT16 RO + QEMU smoke | `smoke-fat16-mount` |
| Host `ir0_block_*` contracts | `tests/host/test_blockdev_facade.c` |
| System power (sync + reboot/halt/poweroff + runit) | `smoke-runit-power`, `smoke-runit-busybox-halt`, `smoke-runit-busybox-poweroff`, `smoke-runit-busybox-reboot` |
| ACPI PM1a QEMU poweroff + kexec/suspend ABI stubs | FADT via on-demand map + `ACPI_PM1A_POWEROFF`; stubs `smoke-reboot-kexec-enosys`, `smoke-reboot-suspend-stub` |
| F2 AHCI NCQ (FPDMA + multi-slot) | `smoke-ahci-read` → `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED` |
| F3 DSDT `_S5_` SLP_TYP | `ACPI_S5_OK` + PM1a `(typ<<10)\|SLP_EN` |
| F4 kexec stub reboot | `REBOOT_KEXEC_STUB` via `smoke-reboot-kexec-enosys` |
| F5 suspend stub success | superseded by S3 soft resume (`smoke-reboot-s3`) |
| kexec_load MVP | `smoke-kexec-load` → `KEXEC_LOAD_OK` + `REBOOT_KEXEC_LOADED` + `KEXEC_PAYLOAD_OK` |
| S3 soft resume MVP | `ACPI_S3_OK` + `SYSTEM_S3_ENTER` + `SYSTEM_S3_RESUME_OK` (`smoke-reboot-s3`) |
| F6 NVMe detect+read | `smoke-nvme-read` → `NVME_DETECT_OK` + `NVME_READ_OK` |
| P1-storage bundle | `make smoke-p1-storage` (FAT/EXT2/GPT/AHCI/NVMe) |
| Kernel relative includes hygiene | `<kernel/…>` in syscalls/process; arch-guard `[kernel-include]` |
| KTM typed `PIPE_*` events | `KTM_EVENT_PIPE_{CREATE,EOF,EPIPE,WAKE}` in `pipe.c` |
| POSIX-2 setsid/setpgid + SIGHUP/TTY | `smoke-posix-setsid`, `smoke-posix-sighup-tty` |
| Real fork COW A–F (HOST) | `make smoke-mm-cow-lazy` (FASE40 A–F) — verified 2026-07-11 |
| GPT / EXT2 RO / AHCI detect+read + multi | `smoke-gpt-partition`, `smoke-ext2-mount`, `smoke-ahci-read`, `smoke-ahci-multi` |
| FAT16 write + vfs-write audit | `linux-abi-audit-vfs-write-fat` VERIFIED |
| PTY multi + `TIOCSWINSZ` | `smoke-pty-winsz` |
| musl pthread libc | `smoke-musl-pthread-libc` |
| TLS facade (`set_tls`) | `arch_portable.h`; W10b PTE deferred |
| ARM64 boot stub + `kernel-arm64.bin` link | `smoke-arm64-boot`; `make ARCH=arm64 kernel-arm64.bin` |
| ARM64 early MMU identity map (F7.1) | `smoke-arm64-mmu` → `ARM64_MMU_OK` (TTBR0 idmap DRAM+UART) |
| ARM64 VBAR + EL1 SVC (F7.2) | `smoke-arm64-vbar` → `ARM64_VBAR_OK` + `ARM64_SVC_RET_OK` |
| ARM64 EL0 drop + SVC + PSCI off (F7.3) | `smoke-arm64-el0` / `make smoke-arm64` |
| AF_UNIX + TCP loopback + `send`/`recv` | `smoke-stream-sock` (`STREAM_SENDRECV_OK`) |
| AF_UNIX `socketpair` stream | `smoke-socketpair` (`SOCKETPAIR_OK` / `KTM_SOCKETPAIR_OK`) |
| AF_UNIX abstract + sock poll | `smoke-unix-abstract` (`UNIX_ABSTRACT_OK` / `SOCK_POLL_OK`) |
| `SCM_RIGHTS` sendmsg/recvmsg | `smoke-scm-rights` (`SCM_RIGHTS_OK`) |
| SysV shm (MIT-SHM prep) | `smoke-sysv-shm` (`SYSV_SHM_OK`) |
| `memfd_create` + MAP_SHARED | `smoke-memfd-shared` (`MEMFD_SHARED_OK`) |
| `getpeername` + multi-fd `SCM_RIGHTS` | `smoke-unix-harden` (`GETPEERNAME_OK` / `SCM_MULTI_OK`) |
| SOCK flags / `accept4` / `MSG_PEEK` / `SO_REUSEADDR` | `smoke-unix-flags` |
| `eventfd2` + `timerfd` | `smoke-event-fds` |
| POSIX `/dev/shm` (`shm_open` path) | `smoke-posix-shm` |
| `mmap` MAP_SHARED `/dev/fb0` | `smoke-fb-map-shared` (`FB_MAP_SHARED_OK`) |
| Host-share virtio-9p (QEMU `-virtfs`) | `smoke-hostshare-9p` (`HOSTSHARE_9P_OK` / `KTM_HOSTSHARE_OK`) |
| Host-share exec (stub + `ir0_payload`) | `smoke-hostshare-exec` (`HOSTSHARE_EXEC_MOUNT_OK` + case done tag) |
| Host-share exec under runit PID1 | `runit_hostshare_payload_run` + runner `--disk`; IR0-desktop `run-xfbdev-smoke.sh` |
| `isa-debug-exit` + CAD/RESTART2 tags | `smoke-isa-debug-exit` |
| ARM64 `platform_ops` virt + RPi stub | `arch/arm64/sources/platform.c` + `board.c` (`ARM64_BOARD=qemu-virt\|rpi4\|rpi5`) |
| KTM boot suite | `make ktm-run` (pass=16 incl. `process.reclaim_exit`) |
| KTM userdev | `ktm-userdev-run`, `ktm-userdev-cow-run` |
| Kernel `[FASE` serial retired | arch-guard `ktm-no-fase` |
| PERF-1 `sys_gettid` | no per-call GETTID spam |
| FASE→KTM Open residual | 41/42/44 fork+exec_drain+reap_drain+**init_exit_drain** SUB; 52/55/58 HOST+KTM; **57 GUI** HOST |

## Closed this wave (2026-07-25) — tree boundary: no in-kernel userspace shell

| Item | Proof |
|------|-------|
| Publish **IR0-userspace** sibling | https://github.com/IRodriguez13/IR0-userspace |
| Coupling docs | [`USERSPACE.md`](USERSPACE.md), `userspace/README.md` |
| Remove `debug_bins/` + `kernel/init.c` (dbgshell PID1) | PR #28; `make run-dbgshell` retired |
| Drop `CONFIG_KERNEL_DEBUG_SHELL` / `CONFIG_DEBUG_BINS*` | `setup/Kconfig`, `setup/defconfig` |
| Gates still green | `kernel-x64.bin`, `arch-guard`, `build-matrix-min`, `tests/host` 22/22 |

## Closed this wave (2026-07-24) — klog event core + product without dbgshell

| Item | Proof |
|------|-------|
| Structured `klog_record` + phases + early clock | [`KLOG.md`](KLOG.md); serial `[#seq] [phase]` |
| Sinks: serial/console/kmsg/hostshare; KTM optional | `smoke-klog-ktm-off`, `smoke-boot-log-hostshare` |
| Product `make run*` → runit/ash | dbgshell/`debug_bins` removed; always `kexecve("/sbin/init")` |
| Driver probe events + summary | `DRIVER_PROBE_RESULT` + `DRIVER_SUMMARY_OK` |
| First bare-metal boot sentinel | `/etc/ir0-baremetal-booted` + `FIRST_BAREMETAL_BOOT*` (no HV) |
| ARM64 hello without musl CRT | freestanding fallback in `musl-aarch64-hello` + early stubs; `smoke-arm64-boot` PASS |

## Closed this wave (2026-07-24) — runit login + stage1 + driver summary

| Item | Proof |
|------|-------|
| getty/login + welcome | `make smoke-runit-login` (`GETTY_READY` / `LOGIN_OK`); GUI: `run-fase58e-ash-gui` |
| First-boot account defaults | `ir0-firstboot` → `FIRSTBOOT_OK` / `FIRSTBOOT_SKIP`; rootfs `IR0-userspace/rootfs/etc/` |
| `fsck.ir0` stage1 | `FSCK_OK` / `FSCK_SKIPPED` / `FSCK_FAIL` (honest) |
| Driver summary NOTICE | `DRIVER_SUMMARY_OK` + structured `DRIVER_SUMMARY` event |
| SMP honesty | `[SMP] UP (1 CPU online)` |

## Open

| Item | Blocks | Next proof |
|------|--------|------------|
| Maintainer manual VM (**mantenedor only**) | **0.0.1 ship** | Interactive QEMU GTK / serial — **not agent backlog** |
| Interactive first-boot wizard (TTY Q&A) | UX polish | Today: non-interactive defaults via `ir0-firstboot` when `/etc/passwd` missing |
| `crypt(3)` shadow hashes | Login hardening | Plain/`empty` passwords for bring-up; BusyBox login/PAM later |
| Kernel root RO → fsck → remount RW | Storage path | `fsck.ir0` tags honest SKIP/OK/FAIL; remount pipeline TBD |
| Interactive sendkey password path | TTY | Username echoes; password stage stalls in QEMU — lab uses `/etc/ir0-autologin`; GUI: `IR0_NO_AUTOLOGIN=1` |
| `smoke-runit-ash-interactive` echo OK | Flaky post-login | LOGIN/welcome OK; `ASH_COMMAND_ECHO_OK` after sendkey still pending |
| Full musl aarch64 CRT restore | Optional toolchain hygiene | Freestanding `hello_aarch64` + weak `sched_context_switch_to`/`klog_info` in `rr_early_stubs.c` unblock `smoke-arm64-boot`; re-extract musl.cc for libc programs |
| virtiofs + FUSE | Future host-share | Guest FUSE; 9p remains ship path |
| Migrate remaining drains/storms stub PID1 → runit+9p | Lab depth | **Mostly closed** — `smoke-ktm-drains-runit`; `init-exit-drain` stays stub (PID1 SUT) |

## Closed this wave (2026-07-18) — KTM drains runit + RTL8139 OWN/cursor

| Item | Proof |
|------|-------|
| Wrapper `--inject` / `--host-file` | [`scripts/ktm_userdev_runit_run.sh`](../scripts/ktm_userdev_runit_run.sh); runner applies inject on `--disk` |
| Drains/storms → runit | `make smoke-ktm-drains-runit` (fork/exec/reap/pseudofs/input + init-exit stub residual) |
| RTL8139 OWN + ring cursor | Host-owns polarity (QEMU `TxHostOwns`); TX only on `currTxDesc` cursor; `smoke-f8-net` with **0** `Failed to send IP` / **0** `TX path recovered` |

## Closed this wave (2026-07-18) — KTM runit T3 + RTL8139 TX

| Item | Proof |
|------|-------|
| Wrapper runit+9p | `scripts/ktm_userdev_runit_run.sh` → `ktm_prepare_runit_hostshare_disk.sh` + `ktm_userdev_runner.py --disk` |
| T3 prep aliases → runit | `make smoke-t3-prep` PASS (`RUNIT_STAGE2_OK` + done tags); stub residual `ktm-userdev-socketpair-run` OK |
| RTL8139 TX recover hygiene | Rate-limit full recover (~100ms); force-release → `LOG_DEBUG`; `make smoke-f8-net` PASS; ≤1 `TX path recovered` WARN per TCP smoke (no storm) |

## Closed this wave (2026-07-18) — kill/#UD + KTM runit piloto

| Item | Proof |
|------|-------|
| `kill(2)` default-fatal without `#UD` | `send_signal`: `kill(pid,0)` existence-only; fatal → zombie without READY race; self → `process_exit`; `make smoke-kill-sigterm` |
| X harness teardown via `kill` | IR0-desktop `ir0_xfbdev_harness.c` + `run-xfbdev-smoke.sh` → `IR0_XFBDEV_SMOKE_OK` |
| Runit KTM piloto | `ktm_prepare_runit_hostshare_disk.sh`; `smoke-stream-sock` + `smoke-hostshare-exec` under runit PID1 |

## Closed this wave (2026-07-18) — X under runit PID1

| Item | Proof |
|------|-------|
| Runit + 9p payload service | `IR0-userspace/services/runit_hostshare_payload_run.c`; `inject-smoke-service.sh --run-only` |
| Desktop X smoke | IR0-desktop `smoke/run-xfbdev-smoke.sh` → `IR0_XFBDEV_SMOKE_OK` (autokill PASS) |
| Autokill mid-tag flush | `scripts/smoke_autokill.py` flat success match + pending line buffer |
| Runner `--disk` payload copy | `ktm_userdev_runner.py` always copies `--init` → share `ir0_payload` when fsdev on |

## Closed this wave (2026-07-18) — ARCH residual + F8 honest MVP

| Item | Proof |
|------|-------|
| **ARCH-4** log hygiene | Ungated serial in `io_syscalls` / `process_syscalls` / `mm_syscalls` gated (`CONFIG_DEBUG_FASE50` / `DEBUG_MMAP_AUDIT`) |
| **procfs** legacy maps | Dead `PROC_*_MAP` virtual-fd offsets removed; userspace uses `fd_table.is_pseudo` |
| **FD helpers** | `includes/ir0/fd_dispatch.h` + use in `fs_syscalls.c` (no one-file-per-syscall) |
| **F8-1** honest NIC | `smoke-nic-reach` requires L3 proof (`NIC_PING_REPLY_OK`) before `F8_NIC_REACH_OK` |
| **F8-2** guest wire | `smoke-tcp-guest` → hostfwd **10.0.2.2:8889** (not in-memory loopback PASS) |
| **F8-3** peer-only wire | `smoke-tcp-wire` send+recv; synthetic DUPACK/SACK/RENO selftest not a Makefile gate |
| **Client RX** | Outbound `g_out` RX buffer; wire/guest echo |
| **Listen battery** | `smoke-tcp-listen` + `make smoke-f8-net` |

**Not done:** full Internet TCP, E1000 NIC (BLOQUEADO — no driver MVP), arbitrary Internet, ARM64 `ALL_OBJS`.

## Closed this wave (2026-07-18) — F8 virtio-net MVP (O3)

| Item | Proof |
|------|-------|
| Legacy virtio-net-pci | [`drivers/net/virtio_net.c`](../drivers/net/virtio_net.c); probe before RTL8139; `virt0`; QEMU `disable-modern=on`; queue size up to **256** (QEMU default) |
| `smoke-nic-reach-virtio` | `make smoke-nic-reach-virtio` → `F8_NIC_REACH_OK` + `NIC_PING_REPLY_OK` (no rtl8139 device) |
| E1000 | **BLOQUEADO** — `CONFIG_DRV_NIC_E1000=n`; no `e1000` sources; not enabled in defconfig |

## Closed this wave (2026-07-18) — F8 peer-CC (LINUX-LIKE)

| Item | Proof |
|------|-------|
| Loss probe + rexmit without synthetic DUPACK/SACK | Port **8890**; `drop_next_tx` once; `make smoke-tcp-peer-cc` → `F8_TCP_PEER_REXMIT_OK` + `F8_TCP_PEER_CC_OK` |
| Wire path keeps lab synthetic CC | `smoke-tcp-wire` still PASS (DUPACK/SACK tags allowed there) |

State: **LINUX-LIKE** (smoke), not ABI VERIFIED.

## Closed this wave (2026-07-12) — F8-3 wire TCP (superseded honesty above)

| Item | Proof |
|------|-------|
| **F8-3** minimal wire TCP | `net/tcp.c` SYN/SYN-ACK/ACK + PSH; `sock_stream` wire path; `make smoke-tcp-wire` → `F8_TCP_WIRE_OK` + host listener `WIRETCP` |

Slice: connect+send/recv to QEMU gateway **10.0.2.2:8888** (MVP, not full stack). Not 0.0.1 ship gate.

## Closed this wave (2026-07-12) — Host-share exec + F8 harden + FAT ship note

| Item | Proof |
|------|-------|
| 9p ELF-sized I/O | `virtio_9p_stat_file` / chunked `virtio_9p_read_file`; `hs_stat`/`hs_read` |
| Stub + KTM share-payload | `init_hostshare_exec`; runner default; `make smoke-hostshare-exec` |
| F8 harden | NET RX `LOG_DEBUG`; FIN\|ACK + bounded poll; `smoke-nic-reach` / `smoke-tcp-guest` / `smoke-tcp-wire` green |
| FAT + MINIX ship | MINIX = root; FAT16 = secondary (`smoke-fat16-mount`); **no** FAT-as-root |

## Closed this wave (2026-07-12) — BUSY-1/2

| Item | Proof |
|------|-------|
| **BUSY-1** product applet manifest | `IR0-userspace/packages/busybox/required_applets.txt` + `IR0-userspace/scripts/busybox_inject_manifest.sh` on runit disk (`load-userspace-runit`) |
| **BUSY-2** applet smoke | `make smoke-busybox-manifest` → `BUSYBOX_MANIFEST_OK` (also `smoke-fase58l-busybox-coreutils`) |

virtiofs/FUSE remains **Future** (no guest FUSE). Host-share remains virtio-**9p**.

Tag `v0.0.1-rc4` = **last pre-release** (feature freeze for 0.0.1 surface); post-rc4 = bugfix /
stabilization only. **Ship** `v0.0.1` still needs Maintainer manual VM above. See
[`releases/IR0_0.0.1_RC4.md`](releases/IR0_0.0.1_RC4.md).

## ARM64 — honest status (2026-07-12)

**What works (freestanding QEMU virt only):**

```text
UART → identity MMU → VBAR → EL1 SVC → EL0 drop → EL0 SVC → PSCI OFF
Proof: make smoke-arm64
```

**What does not exist yet (real port):**

| Gap | Today |
|-----|--------|
| Full kernel link | `kernel-arm64.bin` = `ARCH_OBJS` stubs, not `ALL_OBJS` |
| Syscall dispatch / VFS / MM | x86-only production paths |
| Context switch | `switch_arm64.c` empty stub |
| musl / BusyBox / init | no ARM rootfs |
| GIC / timer / virtio / storage | not wired in boot image |
| Board beyond QEMU virt | **virt + rpi4 UART QEMU** (`arm64_board`, PL011 `0xfe201000`); **rpi5 stub** (`uart=none`, `ARM64_BOARD_RPI5_STUB`); GIC/SD/DT/RP1 still future |

**Rough maturity:** early CPU privilege bring-up (**not** “ARM port ~done”). Treat F7 as
closed for that slice; track real port as **F7b** above.

**F7b.1 (2026-07-12):** curated `ARM64_SLICE_OBJS` (`slice_hello`) linked post-MMU →
`ARM64_SLICE_OK`; `make arm64-slice-compile` also probes `includes/string.c` (compile-only,
not linked — still pulls oops headers). Not `ALL_OBJS`.

**F7b pack (2026-07-12):** PL011 TU + `serial_io_arm64` + early paging API verify +
GIC Device map (no IRQ program) + arch timer CNTFRQ/CNTPCT.
Proof: `make smoke-arm64-port` → `ARM64_PL011_OK` + `ARM64_PAGING_OK` + `ARM64_GIC_MAP_OK`
+ `ARM64_TIMER_OK`. Meta `make smoke-arm64` includes port smoke.

**F7b GIC/IRQ + serial + portable (2026-07-12):** GICv2 Dist/CPU + CNTP PPI30 one-shot
→ `ARM64_GIC_OK` / `ARM64_TIMER_IRQ_OK` (`smoke-arm64-gic`, QEMU `gic-version=2`); remask
before EL0. COM1 `serial.o` excluded when `ARCH=arm64`; `serial.c` x86-guarded.
`make arm64-portable-compile` grows curated objs (`gic_v2`/`timer`/`portable_string`).

**BLOCKED — ALL_OBJS / musl aarch64:** Pack E cleared the **INTERRUPT_OBJS** wall
(`INTERRUPT_OBJS_ARM64` = `irq_portable_stubs.o`). `make arm64-all-objs-probe` compiles
`MEMORY_OBJS` + sample **`kernel/main.c`** / **`open_flags.c`** OK; **next divergence** =
full KERNEL_OBJS / drivers (still not linked as `kernel-arm64.bin = ALL_OBJS`).
**musl aarch64** still **BLOCKED** (SETUP has x86 musl only).

**F8-facade-mm (2026-07-12):** `mm_activate` / `mm_current_root` /
`tlb_invalidate_*` / `irq_save|restore` in `arch_portable`; x86 + ARM64
`mm_ops.c`. Portable callers (`mm/paging` wrappers, `mm_syscalls`, `rr_sched`,
`process_irq_*`) no longer embed `%%cr3`/`invlpg`/`pushfq`. PTE walker still x86
in `mm/paging.c`. **ALL_OBJS/musl aarch64 still BLOCKED.**

**F8b irq/MM residual sweep (2026-07-12):** Mechanical migration — `pushfq`/`cli`/`popfq`
→ `irq_save`/`irq_restore` in `kernel/{ipc,input_events,sock_udp}.c`,
`net/{udp,dns,dhcp,icmp,arp}.c`, `includes/ir0/{named_fifo,console}.c`,
`drivers/net/rtl8139.c`; `fault.c` → `tlb_invalidate_page`; `acpi_pm.c` →
`mm_current_root`; `idt_arch_x64` → `mm_activate`. Portable trees have no
real `__asm__` pushfq/cr3/invlpg (oops diagnostic + walker + process ABI deferred).
Proof: `kernel-x64.bin` + `arch-guard` + `build-matrix-min` + `tests/host` +
`smoke-stream-sock` + `smoke-arm64-syscall`. Remaining debt: PTE walker / `%%cr0`/`%%cr4`
in `mm/paging.c`; `task.cr3` / fork frames / `switch_x64.asm`.

**F7c (2026-07-12):** EL0 Linux-ish SVC ABI (`x8` nr) — getpid/write/exit + one 4 KiB EL0-RW
page @ `0x42000000`. Proof: `make smoke-arm64-syscall` → `ARM64_EL0_PAGE_OK` +
`ARM64_WRITE_OK` + `ARM64_SYSCALL_OK`. Not `process_t` / fork / TTBR-per-task.

**F7d nanosleep + Pack A (2026-07-12):** EL0 `nanosleep` (Linux nr 101) busy-wait on CNTPCT;
tag `ARM64_NANOSLEEP_OK`. Boot IRQ demo + freestanding stubs use `arch_irq_*` (`mm_ops`
linked in freestanding image). ARCH-5: `tests/host` `arch_irq_facade_nested`. Proof:
`smoke-arm64-syscall` (+ NANOSLEEP), `tests/host` 20/20, `smoke-mm-cow-lazy`. Remaining:
walker / process ABI / ALL_OBJS/musl; TCC gate separate (see stab notes).

**Pack B / W10b partial + F7e (2026-07-12):** `mm_read_ctrl0` / `write_ctrl0` / `read_ctrl1`
(x86 CR0/CR4; ARM SCTLR_EL1 / 0) — `mm/paging.c` no longer embeds `%%cr0`/`%%cr4`. F7e
`clock_gettime(CLOCK_MONOTONIC)` → `ARM64_CLOCK_GETTIME_OK`. arch-guard
`[portable-no-isa-asm]` bans pushfq/`%%cr3`/invlpg in portable trees (allowlist `oops.c`).
Product gates (no master merge this wave): `smoke-tcc-power-halt`,
`IR0_LEGACY_SMOKE=1 smoke-fase55b-doom-stub`, `smoke-posix-depth` + CTR/cow/arm64. Remaining:
PTE walker algorithm still x86 in `mm/paging.c`; process ABI; ALL_OBJS/musl.

**Pack C / walker indices + F7f (2026-07-12):** `mm_va_indices` + `mm_pte_present` /
`large` / `phys`; walk paths in `paging_get_pte` / `is_page_mapped` / `map_page_in_directory` /
`unmap_page_in_directory` use facade (no hardcoded `>>39` in those paths). F7f
`gettimeofday` → `ARM64_GETTIMEOFDAY_OK`. arch-guard also bans `%%cr0`/`%%cr4` asm in portable.
Proof: cow + arm64-syscall + stream-sock + CTR. Remaining: `get_or_create_table` / full map
ISA split; process ABI; ALL_OBJS/musl.

**Pack D / PTE encode + F7g (2026-07-12):** `mm_make_table_pte` / `make_leaf_pte` /
`pte_set_user` used in `get_or_create_table` + leaf `map_page_in_directory`. F7g
`clock_nanosleep` (nr 115) → `ARM64_CLOCK_NANOSLEEP_OK`. ARCH-5 host:
`test_arch_mm_pte_facade`. Proof: CTR + `smoke-mm-cow-lazy` + `smoke-arm64-syscall` +
`smoke-stream-sock`. Remaining: process ABI (`task.cr3` / fork); full ARM walker;
ALL_OBJS/musl **BLOCKED**. No master merge this wave.

**Pack E / process MM-root + INTERRUPT wall + probe (2026-07-12):**
`task_mm_root` / `process_mm_root` accessors (field `cr3` @ +0xB0 unchanged for asm).
`mm/paging.c` residual present-checks → `mm_pte_present`. Makefile
`INTERRUPT_OBJS_ARM64` = `irq_portable_stubs.o` (no `lidt` / `isr_stubs_64`).
`make arm64-all-objs-probe`: MEMORY_OBJS compile OK under ARM64_BOOT_CFLAGS;
**next divergence** = KERNEL_OBJS / drivers (not linked into `kernel-arm64.bin`).
**musl aarch64** still **BLOCKED** (no cross toolchain in SETUP; x86 musl only).

---

| # | Item | Next proof |
|---|------|------------|
| F0 | ~~Host-share virtio-9p~~ | **DONE** 2026-07-12 — `smoke-hostshare-9p` (guest `/mnt/host` → host dir; **not** virtiofs/FUSE) |
| F1 | ~~ACPI FADT map seguro~~ | **DONE** 2026-07-11 — `ACPI_FADT_MAPPED` + `ACPI_PM1A_POWEROFF` |
| F2 | ~~AHCI NCQ~~ | **DONE** 2026-07-11 — `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED` |
| F3 | ~~AML `_S5` SLP_TYP~~ | **DONE** 2026-07-11 — `ACPI_S5_OK` + typed PM1a |
| F4 | ~~kexec mínimo~~ | **DONE** 2026-07-11 — stub + `kexec_load` MVP (`smoke-kexec-load`) |
| F5 | ~~Suspend / S3~~ | **DONE** 2026-07-11 — `_S3_` + soft resume (`smoke-reboot-s3`; FACS wake deferred) |
| F6 | ~~NVMe MVP~~ | **DONE** 2026-07-11 — `smoke-nvme-read` (`NVME_READ_OK`) |
| F7 | ARM64 early bring-up (F7.1–F7.3) | **DONE** `make smoke-arm64` — **not** full port |
| F7b | ARM64 real port | F7c–F7g + Pack B–E; KERNEL_OBJS link + musl aarch64 **BLOCKED** |
| F8 | TCP Internet / real NIC (**0.0.2**) | **MVP 0.0.2 honest closed** (`smoke-f8-net`); virtio-net L3 `smoke-nic-reach-virtio`; remaining = peer-driven CC depth, **E1000**, arbitrary Internet |
| F9 | SMP / CFS | **much later** — not coupled to UI/X11 |
| F10 | Rust/C++ driver ABI | DRV-* |
| F11 | T3 WM / X11 userspace | Mini X **PASS**; TinyX guest **BLOCKED** ([TINYX_LAB](../../IR0-desktop/Documentation/TINYX_LAB.md)); WM product waits on TinyX |
| F12 | TCC/Doom “stable” | STABLE.md — merge master solo con bundle verde (Doom=**55d IWAD**) |
| F13 | BusyBox product applets (**BUSY-1/2**) | **DONE** 2026-07-12 — `smoke-busybox-manifest` (`BUSYBOX_MANIFEST_OK`); ship still needs maintainer VM |
| F14 | **HAB** — AC remoto / domótica (userspace) | See [HAB arc](#hab--home-automation--ac-remoto) — not started |
| F15 | **DESK** — desktop chiquito → ClassiCube | **DESK-0…4 DONE** (soft fb); DESK-5 JVM BLOCKED; ship ISO = SEP rootfs (not yet); does not replace Doom 55d |
| F16 | **AST** — Astral-class desktop north star | See [AST arc](#ast--astral-class-desktop-north-star) — aspirational post-DESK; not 0.0.1 |

## Closed this wave (2026-07-18) — HAB/DESK roadmap desglose (docs only)

| Item | Proof |
|------|-------|
| HAB-* / DESK-* / AST-* IDs | Tables above; Future **F14** / **F15** / **F16** |
| Defaults locked | VPN remote; ESP IR first; ClassiCube before JVM; Astral-class = post-DESK north star |
| Code | **None** — daemon/IR/ClassiCube/AST await explicit “dale HAB” / “dale DESK” / “dale AST” |

## Closed this wave (2026-07-18) — DESK-1 mini X gate

| Item | Proof |
|------|-------|
| `make smoke-desk-xfbdev` | PASS `IR0_XFBDEV_SMOKE_OK` + `XSERVER_SELECT mini` |
| Harness default | TinyX only with `/mnt/host/force_tinyx` (no silent fallback) |
| DESK-0 / DESK-1 | **DONE** in tables below |

## Closed this wave (2026-07-18) — DESK-2…4 soft fb path

| Item | Proof |
|------|-------|
| `make smoke-desk-wm` | PASS `IR0_DESK_WM_SMOKE_OK` (fb panel + client rect; not TinyX WM) |
| `make smoke-desk-classicube` | PASS `CLASSICUBE_OK` (`ir0_classicube_soft` soft_fb0) |
| `make smoke-desk-play` | PASS `DESK_PLAY_OK` (frames + optional `/dev/events0`) |
| TinyX X-WM / upstream ClassiCube+GL | **BLOCKED** (lab) — no Mesa/GL; TinyX panic/hang on IR0 |

## Closed this wave (2026-07-18) — SEP-1 tree separation

| Item | Proof |
|------|-------|
| Tree contract | Sibling [`IR0-desktop/Documentation/TREE_CONTRACT.md`](../../IR0-desktop/Documentation/TREE_CONTRACT.md) |
| ARCH debt inventory | [`ARCH_DEBT_SEP.md`](ARCH_DEBT_SEP.md) P0/P1 |
| `smoke-desk-*` vs release | Optional only; **not** in `smoke-release-0.0.1` / `release-0.0.1` |
| Desktop rootfs skeleton | `IR0-desktop/scripts/build-desktop-rootfs.sh` fail-closed (`SHIP_DESKTOP_ROOTFS≠1` → exit 1) |

## Closed this wave (2026-07-18) — Campaña 1 ARCH-DEBT

| Item | Proof |
|------|-------|
| C1-A pseudo-fd | `pseudo_fs.h` policy; `sys_close` fd_table-first; host acquire test |
| C1-B log hygiene | `SIGNAL_DELIVER_LOG` default 0; wait/exec already gated |
| C1-C lifecycle | Audited pipe install rollback — no fix |
| C1-D hostshare/F8 | [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md); E1000/VBox honesty in mandoc/STABLE |

## Closed this wave (2026-07-18) — Campaña 2 TINYX-LAB

| Item | Proof |
|------|-------|
| force_tinyx repro | `XSERVER_SELECT tinyx` then `XFBDEV_BOOT_FAIL not_running` |
| Root cause class | `#PF`/`#GP` + `CONTEXT_LIFETIME_BROKEN`; RIP `0x401670` → `abort` in Xfbdev |
| Docs | [`IR0-desktop/Documentation/TINYX_LAB.md`](../../IR0-desktop/Documentation/TINYX_LAB.md) |
| Manual checklist | Deferred until TinyX green |
| Product WM | **Not started** — waits on TinyX PASS |

## HAB — Home Automation / AC remoto

Same kernel; **userspace** daemon + IR. No “AC driver” in kernel. Remoto afuera de casa = **VPN/WireGuard (o Tailscale)** al LAN — never naked port-forward to IR0. IR path: **ESP blaster** first; native GPIO IR after F7b GPIO.

| ID | Milestone | Depends | Proof |
|----|-----------|---------|-------|
| **HAB-0** | REST contract (`POST /ac/power`, temp, mode) + token auth + threat model (VPN-only) | F8 MVP | Doc in `Documentation/` when implemented (not claimed done) |
| **HAB-1** | runit daemon; LAN `curl` → stub log `IR_CMD …` | HAB-0, `smoke-f8-net` | `smoke-hab-ac-api-lan` |
| **HAB-2** | Proxy to IR blaster (ESP/Tasmota/HTTP); AC brand/protocol in config | HAB-1 | Smoke with mock host or lab HW |
| **HAB-3** | Real remote: WireGuard/Tailscale; phone → API without exposing IR0 | HAB-1 | Ops checklist in STABLE/BACKLOG (no mandatory CI smoke) |
| **HAB-4** | GPIO + IR timing on IR0 (ARM or x86 lab); optional drop ESP | F7b GPIO + HAB-2 | QEMU or board smoke |
| **HAB-5** | Minimal mobile page/app against API | HAB-3 | Manual |

**Not done / not claimed:** any AC control, IR TX, or public Internet API.

## DESK — Desktop chiquito → classic

Aligns with F11/T3; WM **out of kernel tree** (sibling `IR0-desktop`). First playable game = **ClassiCube** (FOSS classic-style). Mojang jar / JVM = **DESK-5**, BLOCKED until toolchain.

| ID | Milestone | Depends | Proof |
|----|-----------|---------|-------|
| **DESK-0** | Declare target: mini desktop out-of-tree + ClassiCube first game | T3 prep OK, mini X PASS | **DONE** — this table + ROADMAP Future product arcs |
| **DESK-1** | Mini X / Xfbdev **default** smoke stable (TinyX still lab/`force_tinyx`) | F11 | **DONE** — `make smoke-desk-xfbdev` → `IR0_XFBDEV_SMOKE_OK` (mini only unless `force_tinyx`) |
| **DESK-2** | Minimal WM + panel (out-of-tree) | DESK-1, AF_UNIX/shm | **DONE (fb session)** — `make smoke-desk-wm` → `IR0_DESK_WM_SMOKE_OK`; TinyX X-WM **lab-blocked** (panic/hang) |
| **DESK-X** | Mini X session: WM layout + panel + client (wire X11) | DESK-1/2 soft | **LINUX-LIKE** — `make smoke-desk-session` → `IR0_DESK_SESSION_OK` (2026-07-23 after stream `fd_refs` + clock_wait nosched); TinyX remains lab — see [`IR0-desktop/Documentation/DESK_SESSION.md`](../../IR0-desktop/Documentation/DESK_SESSION.md) |
| **DESK-3** | ClassiCube (or fork) on IR0 (fullscreen or X window) | DESK-1/2, LAN, musl/glibc enough | **DONE (soft_fb0)** — `make smoke-desk-classicube` → `CLASSICUBE_OK`; upstream ClassiCube+GL **BLOCKED** |
| **DESK-4** | Playable perf/input (mouse; audio optional) | DESK-3 | **DONE (soft)** — `make smoke-desk-play` → `DESK_PLAY_OK` (evdev optional) |
| **DESK-5** | Optional: JVM / Mojang classic client | musl/Java port | **BLOCKED** — no done without binary |

Doom **55d** remains the T2 graphics gate; ClassiCube is a **T3 product demo**, not a Doom replacement.

## AST — Astral-class desktop (north star)

Long-term vision after DESK soft path: CDE-like multi-window desktop with concurrent real apps (reference: Astral OS–style hobby demos — Motif/CDE look, panel + workspaces, terminal, IRC, audio, GL, ported editors, classic Minecraft). Detail + order: [`ROADMAP.md`](ROADMAP.md) (*North star — Astral-class desktop*). **WM/apps out of kernel** (`IR0-desktop`).

| ID | Milestone | Depends | Proof |
|----|-----------|---------|-------|
| **AST-0** | Declare north star (this table + ROADMAP) | DESK-0…4 soft | **DONE** — docs only (2026-07-18) |
| **AST-1** | Product panel + launcher (CDE-like), not soft-fb smoke only | Product WM / TinyX PASS | Smoke or ship checklist TBD |
| **AST-2** | Virtual workspaces (multi-desktop switcher) | AST-1 | Smoke TBD |
| **AST-3** | Desktop terminal client (xterm-class) | AST-1, PTY | Smoke TBD |
| **AST-4** | TCP network app on desktop (`irssi` or equiv. IRC) | AST-3, F8 Internet | Manual or smoke with Liberachat/lab |
| **AST-5** | Audio stack + console/GUI player (MOC-class) | Sound driver + mixer | Smoke TBD |
| **AST-6** | OpenGL / `glxgears` (or soft-GL demo) | Mesa/GL or IR0 GL path | **BLOCKED** — no Mesa |
| **AST-7** | Ported GUI editor (Notepad++-class or native equiv.) | Stable X/session | Port checklist |
| **AST-8** | JVM / Mojang classic Minecraft | = **DESK-5** | **BLOCKED** |
| **AST-9** | Desktop product ISO (several of the above concurrent) | AST-1…4 + SEP rootfs | Ship gate TBD |

**Not done / not claimed:** CDE product shell, workspaces, audio, GL, irssi-on-IR0, Notepad++ port, Minecraft/JVM, Astral-class ISO.

## T3 prep checklist (no WM in kernel)

Canonical path: **runit PID1 + 9p** via `scripts/ktm_userdev_runit_run.sh` (`make smoke-t3-prep`).
Stub `ktm-userdev-*-run` remains lab-only.

| Prerequisite | Status | Evidence |
|--------------|--------|----------|
| Stream sockets (local) | OK (runit) | `smoke-stream-sock` → `*-runit-run` |
| `socketpair` / abstract unix | OK (runit) | `smoke-socketpair`, `smoke-unix-abstract` |
| `SCM_RIGHTS` | OK (runit) | `smoke-scm-rights` |
| SysV shm (MIT-SHM) | OK (runit) | `smoke-sysv-shm` |
| `memfd_create` MAP_SHARED | OK (runit) | `smoke-memfd-shared` |
| `getpeername` / multi `SCM_RIGHTS` | OK (runit) | `smoke-unix-harden` |
| SOCK flags / accept4 / MSG_PEEK / SO_REUSEADDR | OK (runit) | `smoke-unix-flags` |
| eventfd2 + timerfd | OK (runit) | `smoke-event-fds` |
| POSIX `/dev/shm` | OK (runit) | `smoke-posix-shm` |
| MAP_SHARED `/dev/fb0` | OK (runit) | `smoke-fb-map-shared` |
| epoll basic | OK (runit) | `smoke-epoll-basic` |
| poll on stream socks | OK | `SOCK_POLL_OK` in `smoke-unix-abstract` |
| Input events | OK | T2 path |
| Framebuffer | OK | T2 path |
| Kernel-side WM | **Out of scope** | T3 rule |
| X11 userspace (mini) | **OK under runit** | `make smoke-desk-xfbdev` / IR0-desktop `smoke/run-xfbdev-smoke.sh` (`IR0_XFBDEV_SMOKE_OK`) |
| DESK fb session (WM/panel) | **OK under runit** | `make smoke-desk-wm` → `IR0_DESK_WM_SMOKE_OK` |
| DESK X session (mini WM) | **LINUX-LIKE / flaky** | `make smoke-desk-session` → often `IR0_DESK_SESSION_OK`; see desktop `Documentation/DESK_SESSION.md` |
| ClassiCube soft_fb0 | **OK under runit** | `make smoke-desk-classicube` / `smoke-desk-play` → `CLASSICUBE_OK` / `DESK_PLAY_OK` |
| TinyX / Xfbdev full server | Lab green with `force_tinyx`; default smoke = **mini only** | Rebuild `out/Xfbdev`; optional `/mnt/host/force_tinyx` |
| `kill(2)` / default signal tear-down | **OK** | `smoke-kill-sigterm`; X harness uses `kill(SIGTERM)` + wait |
