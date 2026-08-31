# KTM — Kernel Test Module

> **Last verified:** 2026-08-30
> **Source of truth:** `includes/ir0/ktm/*`, `ktm/*.c`, `ktm/include/klog.h`,
> `mm/page_fault.c`, `scripts/smoke_session_soak.py`,
> `includes/ir0/klog_event.h`, [`KLOG.md`](KLOG.md),
> `tests/ktm/`, `setup/Kconfig` (`CONFIG_KTM*`), `scripts/ktm_*.py`,
> `scripts/ktm_userdev_runit_run.sh`, `scripts/smoke_autokill.py`,
> `scripts/make/class-b.mk`, root `Makefile` targets `ktm-*` / `smoke-class-b-*` / `smoke-klog-ktm-off`

KTM is IR0’s **canonical kernel test and diagnostic plane**: typed events, checkpoints,
snapshots, in-kernel scenarios, `/dev/ktm` for userspace-driven cases, and host runners
that autokill QEMU on protocol tags. Legacy kernel `[FASE` serial tags are **retired**
(enforced by `make arch-guard`).

Agent-oriented short index: [`ai_driven_dev/ktm.md`](ai_driven_dev/ktm.md).
FASE migration maps: [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md),
[`KTM_FASE_INVENTORY.md`](KTM_FASE_INVENTORY.md).

---

## 1. Architecture (what lives where)

```text
includes/ir0/ktm/     Public facades (kernel + uapi headers)
ktm/                  Runtime internals (ring, transport, registry, /dev/ktm, …)
tests/ktm/scenarios/  In-kernel scenario TUs (linked into the kernel)
tests/ktm/userdev/    musl pilots injected as /sbin/init for QEMU
tests/ktm/lib/        libktm_user — thin ioctl wrapper for pilots
scripts/ktm_*.py      Host runners / classifiers / inventory
```

| Layer | Path | Role |
|-------|------|------|
| Public kernel API | `#include <ir0/ktm/ktm.h>` (or `<ktm.h>` via thin `includes/ir0/ktm.h`) | Umbrella: events, asserts, scenarios, uapi, userdev hook |
| Private helpers | `#include <ktm_internal.h>` (`ktm/include/`) | Only inside `ktm/` and scenario TUs — **no** `../` or quoted includes (arch-guard `[ktm-include]`) |
| Userspace ABI | `#include <ir0/ktm/uapi.h>` | Ioctl numbers, caps, event/snapshot structs |
| Userspace helper | `tests/ktm/lib/libktm_user.h` | `ktm_open` / case / assert / scenario wrappers |

Makefile puts `-Iktm/include` **before** `-Iincludes/ir0` so `<ktm.h>` is not shadowed by the facade.

Boot wiring (kernel):

1. `ktm_core_init()` — registry + `ktm_scenarios_register_builtins()` (`ktm/registry.c`).
2. `ktm_userdev_register()` — `/dev/ktm` when `CONFIG_KTM_USERDEV`.
3. `ktm_scenarios_run_boot()` — runs registered scenarios (`kernel/main.c`).

---

## 2. Kconfig tiers

| Option | Default | Role |
|--------|---------|------|
| `CONFIG_KTM` | y | Core: checkpoints, panic class, snapshots hooks |
| `CONFIG_KTM_EVENTS` | y | Typed ring + serial `KTM|…` transport |
| `CONFIG_KTM_FLIGHT` | y | Dump ring on panic |
| `CONFIG_KTM_TEST` | (defconfig) | Boot scenario suite |
| `CONFIG_KTM_USERDEV` | depends on KTM+EVENTS | `/dev/ktm` control plane |
| `CONFIG_KTM_FAULT` | n | Fault-injection stub (v2); keep off in prod |
| `CONFIG_KTM_SERIAL_VERBOSE` | n | Full `CHECKPOINT`/`PROBE`/`KTM|LOG` on serial; leave off for product/desk |
| `CONFIG_KTM_PROBE_DIAG` / `MALLOC_FORENSICS` | n | Temporary bring-up noise |

When `CONFIG_KTM_USERDEV` is off, `includes/ir0/ktm/userdev.h` provides an empty
`static inline ktm_userdev_register()`. The real definition in `ktm/userdev.c` is
compiled only when the option is on — keep that pairing intact.

---

## 3. Internals (runtime under `ktm/`)

| Component | Files | Behavior |
|-----------|-------|----------|
| Event ring | `event_ring.c`, `includes/ir0/ktm/event.h` | Typed `ktm_event_t`; `ktm_event_emit4`; `ktm_event_copy_out` for `read(2)` |
| Transport | `transport_serial.c` | Host-visible lines: `KTM|<seq>|<KIND>|<name>|<status>` |
| Registry / probes | `registry.c`, `probe.h` | Named probes (`ktm_probe_register` / `ktm_probe_run`) |
| Snapshot | `snapshot.c` | Process/frame/fd/pipe counts for leak checks |
| Assert | `assert.c` | `KTM_V1_ASSERT_*`, `KTM_REQUIRE`, leak helpers, **`ASSERT_BATCH`** |
| Checkpoint | `checkpoint.c` | Lifecycle enum → event + transport |
| Scenario runner | `scenario.c` | Register / run one / run boot suite; pass/fail counters |
| Human log hub | `klog.c`, `ktm_klog.c`, [`KLOG.md`](KLOG.md) | Structured `klog_record` + `[#seq] [phase] [ts] [LEVEL] [COMP] msg`; facade `<ir0/ktm/klog.h>` |
| Userdev | `userdev.c` | `/dev/ktm` ioctl + read/poll over the ring |
| Flight / panic | `ktm_flight.c`, `ktm_panic_class.c`, … | Post-mortem classification |
| Diags | `d1_*`, `ktm_probe_diag.c` | Optional forensics (Kconfig off by default) |

### Logging layers (klog vs KTM vs host)

| Layer | Role | Include / entry |
|-------|------|-----------------|
| **klog** | Structured event core (sequence, phase, clock_state, sinks) | `#include <ir0/ktm/klog.h>` / [`KLOG.md`](KLOG.md) |
| **KTM protocol** | Test transport `KTM\|seq\|KIND\|name\|status` | `ktm_transport_emit`, scenarios, `/dev/ktm` |
| **Userspace smoke tags** | Bare tokens for autokill (`SMOKE_OK`, stage tags) | `ir0_smoke_tag()` in `IR0-userspace/lib/ir0_smoke_tag.h` (same role as `klog_smoke`) |
| **QEMU host chatter** | Version banner / GTK / warnings | Sibling `*.qemu-stderr` from `scripts/smoke_autokill.py` — **not** mixed into the guest serial log |

Rules of thumb:

- Prefer `klog_*` / `klog_event()` over raw `serial_print` outside `ktm/klog.c` and serial driver stubs.
- Classify lines use one COMP plus the word `CLASSIFY` in the message, e.g.
  `[#n] [phase] [ts] [INFO] [VFS] CLASSIFY VFS_FS_CONTRACT_ACTIVE`.
- **Boot banner is the first framed serial line on every ISA.** Call
  `ir0_boot_serial_ready()` after UART/serial init (`includes/ir0/boot_log.h`).
  Product kernels route through `klog_boot_hold` + `klog_event(BOOT_BANNER, …)`;
  freestanding ARM64 early boot (`IR0_FREESTANDING_BOOT`) emits the same layout
  via `arch/common/boot_log.c` (with `#sequence` + `EARLY_ARCH`).
- `CONFIG_KTM=n`: klog + `/proc/kmsg` still work (`make smoke-klog-ktm-off`).
- `CONFIG_KTM_SERIAL_VERBOSE` (default **n**): when off, product/desk boots omit noisy
  `CHECKPOINT` / `PROBE` / optional `KTM|LOG` mirrors; ASSERT / SUITE / smoke tags still emit.

### Class B context (KERNEL_CS + userspace RIP)

Illegal pairing for `kernel_ret`: task CS is ring-0 but RIP looks like userspace.
Product default `IR0_CLASS_B_REPAIR=1` (`config.h`) repairs from `syscall_frame` when
possible. Gates:

| Target | Expect |
|--------|--------|
| `make smoke-class-b-mitigated` | inject + `KERNEL_CS_USER_RIP_REPAIR` + `KTM_CLASS_B_OK` |
| `make smoke-class-b-repro` | rebuild with `IR0_CLASS_B_REPAIR=0` → `KERNEL_RET_BAD_RIP` |
| `make -C tests/host run` | `test_class_b_ctx_invariant` |

Impl: `sched/switch/arch_context_switch.c`, `process_arm_kernel_syscall_sleep` /
`want_kernel_ret`, `sched_context_switch_take_skip_prev_save` (IRQ preempt must not
overwrite a saved user iretq frame). Host fixtures: `tests/mocks/sched/class_b.h`.
Makefile fragment: `scripts/make/class-b.mk`.

### Serial protocol (grep-friendly)

Examples emitted by scenarios / userdev:

```text
KTM|706|TEST_BEGIN|tcp_wire
KTM|707|CHECKPOINT|tcp_wire_connect
KTM|708|ASSERT_PASS|tcp_wire_send
KTM|709|TEST_END|tcp_wire|PASS
KTM|…|ASSERT_BATCH|process.wait_drain|iterations=128|PASS
KTM|…|SUITE_END|…|…
```

Happy-path loops (e.g. `process.wait_drain`, `process.reclaim_exit`) wrap soft asserts in
`ktm_assert_batch_begin` / `ktm_assert_batch_end` so serial gets **one** `ASSERT_BATCH`
line instead of N× `ASSERT_PASS`.

User-driven cases also print plain tags (e.g. `KTM_USERDEV_OK`, `F8_TCP_WIRE_OK`) for
`scripts/smoke_autokill.py` / `ktm_userdev_runner.py` `--done` / `--require`.

---

## 4. Kernel-mode API — write and run tests

### 4.1 Include

```c
#include <ir0/ktm/ktm.h>   /* preferred from portable kernel code */
/* Scenario TUs may use: */
#include <ktm_internal.h>
```

### 4.2 Emit diagnostics from production paths

```c
KTM_CHECKPOINT(KTM_CP_PROCESS_FORK);

ktm_event_emit4(KTM_EVENT_PROCESS_CREATE, KTM_SUBSYS_PROC,
                (uint64_t)pid, 0, 0, 0);
```

`KTM_CHECKPOINT` is a no-op if `CONFIG_KTM` is off.

### 4.3 In-kernel scenario (boot suite)

1. Add `tests/ktm/scenarios/foo_bar.c`.
2. Implement `run(ktm_context_t *)` returning `KTM_OK` (0) or negative / `-1` on fail.
3. Register via `ktm_scenario_register` from a `ktm_scenario_register_foo_bar()` called
   inside `ktm_scenarios_register_builtins()` in `process_lifecycle.c` (current hub).
4. Add `tests/ktm/scenarios/foo_bar.o` to the kernel object list in the root `Makefile`.

Minimal pattern (see `tests/ktm/scenarios/process_lifecycle.c`):

```c
static int scenario_foo_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before, after;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	KTM_REQUIRE(ktm_invariants_run(KTM_INV_PROCESS | KTM_INV_FRAMES) == 0);

	/* … exercise subsystem … */
	KTM_V1_ASSERT_TRUE(condition);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_foo = {
	.name = "foo.bar",
	.flags = 0,
	.setup = NULL,
	.run = scenario_foo_run,
	.teardown = NULL,
};

void ktm_scenario_register_foo_bar(void)
{
	(void)ktm_scenario_register(&scenario_foo);
}
```

**Assert macros (kernel):**

| Macro | Behavior |
|-------|----------|
| `KTM_V1_ASSERT_TRUE(expr)` | Soft assert; records pass/fail on context |
| `KTM_V1_ASSERT_EQ(a, b)` | Soft equality |
| `KTM_REQUIRE(expr)` | Fail assert + `return -1` from `run` |
| `KTM_ASSERT_NO_PROCESS_LEAK(b, a)` | Snapshot delta |
| `KTM_ASSERT_NO_FRAME_LEAK(b, a)` | Snapshot delta |

**Invariants mask:** `KTM_INV_PROCESS`, `KTM_INV_FRAMES`, `KTM_INV_ALL`
(`includes/ir0/ktm/ktm.h`).

### 4.4 Run kernel scenarios from the host

```bash
make -s ktm-run                          # default SCENARIO=process.lifecycle
make -s ktm-run SCENARIO=ipc.pipe_lifecycle
```

Uses `scripts/ktm_runner.py` against `kernel-x64-userspace.iso` and expects
`TEST_END|<name>|PASS` on serial.

Boot suite runs **all** registered scenarios when `CONFIG_KTM_TEST` is enabled
(`ktm_scenarios_run_boot`).

---

## 5. Userspace API — drive tests via `/dev/ktm`

Requires `CONFIG_KTM_USERDEV`. Device node: `/dev/ktm` (devfs).

### 5.1 UAPI (`includes/ir0/ktm/uapi.h`)

| Ioctl | Purpose |
|-------|---------|
| `KTM_IOC_RUN_SCENARIO` | Run named in-kernel scenario; fills `result` |
| `KTM_IOC_RUN_INVARIANTS` | `ktm_invariants_run(mask)` |
| `KTM_IOC_TAKE_SNAPSHOT` | Frame/process/fd/pipe counts |
| `KTM_IOC_CONFIG_FAULT` | Fault inject (`-ENOTSUPP` if `KTM_FAULT` off) |
| `KTM_IOC_RESET` | Reset suite / ring cursor |
| `KTM_IOC_GET_CAPS` | `version` + `caps` bitmask |
| `KTM_IOC_USER_EVENT` | Emit CASE/ASSERT/CHECKPOINT from userspace |

Caps bits: `KTM_CAP_EVENTS`, `KTM_CAP_TEST`, `KTM_CAP_FAULT`, `KTM_CAP_USERDEV`.

`read(2)` on `/dev/ktm` returns zero or more `ktm_uapi_event_t` (same layout as
`ktm_event_t`). Poll uses `ktm_event_pending()`.

### 5.2 Helper library (`tests/ktm/lib`)

```c
#include "libktm_user.h"

int fd = ktm_open();                    /* open("/dev/ktm", O_RDWR) */
ktm_user_caps_t caps;
ktm_get_caps(fd, &caps);                /* require KTM_CAP_USERDEV */
ktm_reset(fd);
ktm_case_begin(fd, "my_case");
ktm_checkpoint(fd, "step1");
ktm_assert_true(fd, "cond_ok", cond);
ktm_assert_eq_u64(fd, "count", expect, actual);
ktm_run_scenario(fd, "process.lifecycle", &rc);
ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES);
ktm_snapshot_request(fd, &snap);
ktm_case_end(fd, "my_case", fails == 0 ? 0 : 1);
ktm_close(fd);
```

### 5.3 Add a userspace pilot

1. Create `tests/ktm/userdev/ktm_my_case.c` (static musl PID1; often `pause()` or `_exit`
   after printing a done tag).
2. Wire `KTM_MY_CASE_SRC` / `BIN` and `build-ktm-my-case` + `ktm-userdev-my-*-run` in
   the root `Makefile` (copy an existing `build-ktm-*-case` block).
3. Compile with `-Iincludes -Itests/ktm/lib` and link `libktm_user.c`.
4. Optional: write a result file under virtio-9p `/mnt/host/…` and pass
   `--host-file` / `--host-grep` to `scripts/ktm_userdev_runner.py`.

Reference pilots: `ktm_fork_wait_case.c`, `ktm_tcp_wire_case.c`,
`ktm_posix_pseudofs_case.c`.

### 5.4 Host runner

```bash
make build-ktm-fork-wait-case
make build-init-hostshare-exec       # stub PID1: mount 9p + execve payload
make smoke-hostshare-exec            # runit PID1 piloto + fork_wait as ir0_payload
make smoke-hostshare-exec-stub       # legacy stub PID1 path
make smoke-kill-sigterm              # kill(2) SIGTERM + kill(pid,0) without #UD
make ktm-userdev-run                 # default: stub init + share payload
make ktm-userdev-fork-storm-virtfs-run
make smoke-tcp-wire                  # F8-3 alias → tcp_wire virtfs run
make smoke-tcp-listen                # F8 host→guest listen/accept
make smoke-f8-net                    # F8 honest MVP battery
```

F8 honesty (2026-07-18): nic-reach requires real L3; tcp-guest uses hostfwd wire (not
in-memory); tcp-wire tags need peer evidence; listen is in the F8 battery. Loopback
regression remains `smoke-stream-sock` (**runit PID1 piloto** via
`ktm_prepare_runit_hostshare_disk.sh`).

`scripts/ktm_userdev_runner.py` (default **share-payload** path):

- Copies `disk.img`, injects **stub** `setup/pid1/init_hostshare_exec` as `/sbin/init`.
- Copies `--init` ELF to the virtio-9p share as **`ir0_payload`** (flat name) — also when
  `--disk` is a prebuilt image (hybrid runit path).
- Always attaches `-fsdev` / `virtio-9p-pci` (`mount_tag=ir0share`) unless `--no-fsdev`.
- Stub mounts `/mnt/host` and `execve("/mnt/host/ir0_payload")`.
- Cases report via `ktm_hostshare_report()` (tolerates share already mounted).

#### Hybrid: runit PID1 + 9p payload (preferred for product-shaped smokes)

Product PID1 is **runit** (`make smoke-runit-boot`). Large ELF pilots that still need
virtio-9p should **not** replace `/sbin/init` with the stub.

**Wrapper (canonical for T3 / product-shaped KTM):**

```bash
scripts/ktm_userdev_runit_run.sh --init BIN --log LOG --done TAG --require TAG …
```

Prepares a temp runit disk (`ktm_prepare_runit_hostshare_disk.sh`) and invokes
`ktm_userdev_runner.py --disk`. Supports `--inject SRC:DEST` (e.g. `f41true` for
exec-drain) and `--host-file` / `--host-grep`. Makefile targets
`ktm-userdev-*-runit-run` use this; `make smoke-t3-prep` and
`make smoke-ktm-drains-runit` point here. Stub `ktm-userdev-*-run` remains lab
residual. **Exception:** `init-exit-drain` stays stub/virtfs (SUT is PID1 `_exit`).

Manual pattern (same as the wrapper):

1. `install-to-disk.sh` → runit rootfs on a temp MINIX image.
2. `inject-smoke-service.sh --run-only DISK SERVICE IR0-userspace/out/stage-bin/runit_hostshare_payload_run`
   (service mounts `ir0share` and execs `/mnt/host/ir0_payload`).
3. Optional: overwrite noisy `console`/`logger` `run` stubs with `runit_pause_run` so serial
   done-tags are not split by ash.
4. `ktm_userdev_runner.py --disk DISK --init PAYLOAD --share DIR --done TAG …`

Reference (out of tree): sibling **IR0-desktop** — optional DESK gates
`make smoke-desk-xfbdev` / `smoke-desk-wm` / `smoke-desk-classicube` / `smoke-desk-play`
(or battery `make smoke-desk`). **Not** part of `release-0.0.1`. Tree contract:
`IR0-desktop/Documentation/TREE_CONTRACT.md`; debt list: `Documentation/ARCH_DEBT_SEP.md`.

Lab pilots that still use stub PID1 (`ktm-userdev-*-run` without `--disk` runit) remain
valid **dev aids**; T3 checklist smokes are already on the hybrid path.
- `--legacy-disk-init` restores old inject-`--init`-as-`/sbin/init` behavior.
- Extra QEMU args via `--qemu-arg`; if any arg contains `netdev`, no `-net none`.
- Autokills on `--done`; checks `--require` and optional `--host-file` / `--host-grep`.

---

## 6. Gates cheat sheet

```bash
# In-kernel suite / single scenario
make -s ktm-run
make -s ktm-run SCENARIO=mm.cow_fork

# Session-level corruption watchdogs (section 7)
make -s smoke-session-walk                  # 9p tree walk, kstack peak, name round-trip
make -s smoke-session-soak SOAK_ROUNDS=14   # long console soak; fails only on watchdogs

# Userspace /dev/ktm pilots
make -s ktm-userdev-run
make -s ktm-userdev-cow-run
make -s ktm-userdev-fork-storm-virtfs-run
make -s ktm-userdev-exec-drain-virtfs-run
make -s ktm-userdev-reap-drain-virtfs-run
make -s ktm-userdev-init-exit-drain-virtfs-run
make -s ktm-userdev-posix-pseudofs-virtfs-run
make -s ktm-userdev-input-det-virtfs-run
make -s smoke-nic-reach          # F8-1 L3 rtl8139 (NIC_PING_REPLY_OK)
make -s smoke-nic-reach-virtio   # F8-1 L3 virtio-net-pci legacy
make -s smoke-tcp-guest          # F8-2 guest→host wire :8889
make -s smoke-tcp-wire           # F8-3 wire send+recv (peer only)
make -s smoke-tcp-peer-cc        # peer loss + rexmit (LINUX-LIKE, port 8890)
make -s smoke-tcp-listen         # F8 listen host→guest :7777
make -s smoke-f8-net             # battery: nic + guest + wire + listen
make -s smoke-t3-prep            # T3 sockets/IPC/fb battery (all runit)
make -s smoke-desk-xfbdev        # OPTIONAL DESK-1 (sibling IR0-desktop; not release)
make -s smoke-desk-wm            # OPTIONAL DESK-2 fb session
make -s smoke-desk-classicube    # OPTIONAL DESK-3 soft_fb0
make -s smoke-desk-play          # OPTIONAL DESK-4
make -s smoke-desk               # OPTIONAL battery DESK-1..4
make -s smoke-ktm-drains-runit   # fork/exec/reap/pseudofs/input runit + init-exit stub
make -s smoke-epoll-basic        # alias → ktm-userdev-epoll-runit-run
make -s smoke-stream-sock        # alias → ktm-userdev-stream-sock-runit-run
make -s smoke-kill-sigterm       # kill(2) SIGTERM + probe0
make -s smoke-socketpair         # alias → ktm-userdev-socketpair-runit-run
make -s smoke-fb-map-shared      # alias → ktm-userdev-fb-map-shared-runit-run
make -s smoke-scm-rights         # alias → *-scm-rights-runit-run
make -s smoke-unix-abstract      # alias → *-unix-abstract-runit-run
make -s smoke-sysv-shm           # alias → *-sysv-shm-runit-run
make -s smoke-memfd-shared       # alias → *-memfd-shared-runit-run
make -s smoke-unix-harden        # alias → *-unix-harden-runit-run
make -s smoke-unix-flags         # alias → *-unix-flags-runit-run
make -s smoke-event-fds          # alias → *-event-fds-runit-run
make -s smoke-posix-shm          # alias → *-posix-shm-runit-run
make -s ktm-userdev-socketpair-run  # stub PID1 residual (lab)
make -s ktm-userdev-busybox-manifest-run  # BUSY-2 product applets (canonical)
make -s smoke-busybox-manifest   # alias → ktm-userdev-busybox-manifest-run
make -s ktm-userdev-doom-55d-run # hybrid: runit + WAD + KTM_DOOM_55D_OK
make -s ktm-userdev-tcc-power-halt-run  # hybrid: runit + TCC + POWER_TCC_KTM_OK
make -s smoke-runit-boot         # product PID1 (not userdev)

# Hygiene
make -s arch-guard               # no [FASE in kernel trees; ktm-include rules
make -s ktm-check                # host + classify selftest + panic inventory
python3 scripts/ktm_log_classify.py /tmp/some.log
```

Deep COW data-plane remains `make smoke-mm-cow-lazy` (not replaced by `mm.cow_fork`
bookkeeping scenario). Host-share: `make smoke-hostshare-9p` (MVP read) and
`make smoke-hostshare-exec` (stub + payload exec).

**Critical product battery (hybrid):**

```text
ktm-userdev-busybox-manifest-run   # full KTM runner (--disk prebuilt)
ktm-userdev-doom-55d-run           # hybrid runit (alias smoke-fase55d-doomgeneric)
ktm-userdev-tcc-power-halt-run     # hybrid runit (alias smoke-tcc-power-halt)
smoke-runit-boot                   # product PID1 — not migrated to userdev
```

---

## 7. Corruption watchdogs

Three mechanisms that turn a late, anonymous crash into a named event at the
point of damage. Added while chasing a session that died with a `rep movsq`
read fault one page above the user stack.

### 7.1 Ring dump quotas

`ktm_event_ring_dump()` selects newest-first **under a per-type quota**
(a quarter of the budget per event type, then a backfill pass).

Without it a dump is useless whenever one type dominates. `KTM_EVENT_CTX_USER_IRET`
fires on every return to ring 3, and `ktm_deferred_flush()` replays its backlog
at the head of the ring just before the walk, so a segfault dump came out as 48
identical resume-gate rows with the page faults and pipe transitions that explain
the crash already evicted.

Source: `ktm/event_ring.c` (`g_dump_selected`, `g_dump_type_count`).

### 7.2 User stack canary

The 16 bytes between the end of the initial `argv`/`envp`/auxv image and
`USER_STACK_TOP` are slack that no correct program touches. They are stamped at
`execve` and re-read on a rate-limited syscall poll and on every SIGSEGV.

| Symbol | Role |
|---|---|
| `ktm_user_canary_install()` | Stamp, from `elf_setup_stack()` |
| `ktm_user_canary_check()` | Verify; emits `KTM_USER_CANARY_BROKEN` and dumps the ring once per task |
| `ktm_user_canary_poll()` | Watchdog on syscall exit, 1 in 256 |

The pattern is **not** mixed with the pid: a fork child inherits the parent's
stack image verbatim, so a pid-derived value reports every child as corrupted.

Source: `ktm/user_canary.c`, `includes/ir0/ktm/user_canary.h`.

### 7.3 Stack-top overrun classification

A fault in the page immediately above `USER_STACK_TOP` is logged as
`STACK_TOP_OVERRUN` with rip and offset. `segv addr=7ffff000` reads as a wild
pointer; it is in fact the first address past the top of the stack, which means
something walked off the end of the initial image.

Source: `mm/page_fault.c` (`pf_user_segv`).

### 7.4 Soak harness

`make smoke-session-soak SOAK_ROUNDS=N` drives many rounds of pipelines, heavy
`readdir` and set-id exec over one console. It fails **only** on the watchdogs
above; a stalled command or a session segv is counted and reported, not treated
as the verdict.

Relogin is off by default (`--relogin-every 0`): the harness stalls after a few
logout cycles, which cut every run short before the soak accumulated any uptime.

Source: `scripts/smoke_session_soak.py`.

---

## 8. Policy

1. Prefer KTM scenarios / `libktm_user` / `KTM_CHECKPOINT` over new serial slogans.
2. Do not grow `ktm/` with test cases — put scenarios under `tests/ktm/scenarios/` and
   pilots under `tests/ktm/userdev/`.
3. Do not claim a FASE oleada “covered” without checking
   [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md).
4. Keep uapi event type numbers in sync between `event.h` and `uapi.h`
   (`KTM_UAPI_EVENT_*`).

### 7.1 Canonical test plane (when KTM is better)

| Plane | Owns |
|-------|------|
| **KTM** (`ktm-run` / `ktm-userdev-*`) | Kernel state, fork/exec/IPC depth, guest net wire, fault injection, syscall depth that fits a musl pilot; **BusyBox manifest** (`ktm-userdev-busybox-manifest-run`) |
| **Hybrid KTM + product HOST** | Doom 55d / TCC power-halt under **runit**; Xfbdev/mini under **runit + 9p** (`runit_hostshare_payload_run`); emit done tags + optional `KTM_*` |
| **Keep outside KTM** | `arch-guard`, `tests/host` ABI/facade units, `linux-abi-audit-*`, deep COW `smoke-mm-cow-lazy`, product HOST **runit-boot / ash** (PID1 SUT), storage/hw/ARM machine smokes |
| **Do not add** | A new serial-only smoke for a gap KTM already covers better — add/extend scenario or userdev + `--require KTM_*` / `KTM\|…` protocol |

Overlapping product smokes must **alias** to the KTM gate as the primary target. T3 prep
smokes alias to `ktm-userdev-*-runit-run` (wrapper above). Stub `*-run` is residual lab.
`smoke-busybox-manifest` → `ktm-userdev-busybox-manifest-run`. Legacy FASE serial storms
remain SUB / non-default. Doom/TCC keep runit recipes; KTM names are hybrid aliases.

---

## 9. Related docs

| Doc | Content |
|-----|---------|
| [`tests/ktm/README.md`](../tests/ktm/README.md) | Tree layout for test artifacts |
| [`ai_driven_dev/ktm.md`](ai_driven_dev/ktm.md) | Agent-facing summary |
| [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md) | FASE → KTM coverage class |
| [`KTM_FASE_INVENTORY.md`](KTM_FASE_INVENTORY.md) | Legacy smoke → KTM gate map |
| [`STABLE.md`](STABLE.md) | Release gates that mention KTM / net smokes |
