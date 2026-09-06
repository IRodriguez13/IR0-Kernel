# Kernel Decoupling Map

> **Last verified:** 2026-09-02  
> **Source of truth:** `includes/ir0/*`, `scripts/architecture_guard.py`, `ktm/include/klog.h`,
> [`uaccess.md`](uaccess.md)

This document maps how IR0 separates subsystems, where stable interfaces live, and where
coupling still exists. It complements `DRIVERS.md`, `MAKEFILE.md`, `TOOLING.md`, and the CTR checklist in
`Documentation/ai_driven_dev/skills/ctr/SKILL.md`.

## Goals (design intent)

- **Facades:** Upper layers (especially `fs/` and syscall paths) reach hardware-facing code
  through `includes/ir0/*` rather than including `drivers/*` or `arch/*` directly where
  possible.
- **Callbacks / ops tables:** Drivers and pluggable backends expose behavior through function
  pointers (`ir0_driver_ops_t`, `block_dev_ops_t`, `vfs_ops`, `devfs_ops_t`, bootstrap
  hooks) registered at startup.
- **Config-driven composition:** Optional subsystems are selected via `CONFIG_*` symbols wired
  through `setup/Kconfig`, `setup/defconfig`, `Makefile` object lists, and
  `scripts/kconfig/menuconfig.py`.

“Total decouple” progresses as portable trees route optional services through façades (`serial_io.h`,
`clock.h`). Some subsystems (`main.c` drivers bring-up, `includes/ir0/` thin headers) still include
underlying `drivers/*` via those façades; direct `#include <drivers/serial/...>` outside `drivers/`
/`arch/` is reduced in **`kernel/`**, **`mm/`**, **`net/`** to prefer `includes/ir0/*`.

## Portable architecture boundary (implemented conventions)

| Area | Mechanism |
|------|-----------|
| Syscall ISA hook | **`syscall_init()`** (x86‑64 installs int `0x80` trap to assembly stub in `arch/common/arch_interface.c`); portable [`kernel/syscalls.c`](../../kernel/syscalls.c) does not include `interrupt/arch/idt.h`. |
| Ring‑3 transition | **`switch_to_user(entry, stack)`** / **`switch_to_user_task`** ([`arch/x86-64/sources/user_mode.c`](../../arch/x86-64/sources/user_mode.c)); portable code uses [`includes/ir0/arch_cpu.h`](../../includes/ir0/arch_cpu.h) only. |
| Context switch | **`switch_to(prev, next)`** → **`arch_switch_to`** ([`includes/ir0/context.h`](../../includes/ir0/context.h), [`arch_switch.h`](../../includes/ir0/arch_switch.h)); first entry **`first_switch_to`**. ISA bodies in `arch/*/sources/arch_switch.c`. |
| IRQ bring-up | **`irq_tables_init` / `irq_controller_init` / `irq_keyboard_*` / `irq_unmask_line`** ([`includes/ir0/irq.h`](../../includes/ir0/irq.h)); backends in `arch/*/sources/arch_irq_init.c`. Portable trees must not `#include <interrupt/arch/...>`. |
| Signals / sigcontext | ISA layouts in [`sigcontext_x86_64.h`](../../includes/ir0/sigcontext_x86_64.h) / [`sigcontext_arm64.h`](../../includes/ir0/sigcontext_arm64.h); portable delivery via **`arch_signal_*`** + **`arch_task_{load,store}_sigcontext`**. |
| Syscall frame | Opaque **`process_syscall_*`** accessors; fill/restore in **`arch_syscall_frame`**. Guard: `[syscall-frame-accessor]`. |
| MM root half | **`arch_mm_copy_kernel_half` / `arch_mm_user_root_slots`** ([`arch_mm.h`](../../includes/ir0/arch_mm.h)). |
| User VA window | **`mm_user_va_ok(addr, size)`** ([`mm.h`](../../includes/ir0/mm.h)); ISA bodies in `arch/*/sources/arch_mm.c`. |
| Usercopy | **`copy_to/from_user` / `clear_user` / region helpers** ([`copy_user.h`](../../includes/ir0/copy_user.h)); never CR3+`memcpy` to user VA. Full contract: [`uaccess.md`](uaccess.md). |
| IRQ / MM / TLB | Simple names: **`irq_save`/`irq_restore`**, **`mm_activate`**, **`tlb_invalidate_*`**, **`cpu_relax`**, **`smp_mb`**, **`timer_read`** ([`includes/ir0/cpu.h`](../../includes/ir0/cpu.h) + `arch_cpu.h`). No `arch_` prefix on new hot-path facades. |
| **`fs/`** vs `arch/` | No `#include <arch/...>` in `fs/`; use **`includes/ir0/arch_port.h`** (`scripts/architecture_guard.py` enforces). |

### Architecture guard rules (`scripts/architecture_guard.py`)

| Tag | Scope | Rule |
|-----|-------|------|
| `forbidden-include` | `fs/`, `kernel/syscalls.c` | No `#include <drivers/...>` |
| `missing-facade` | `includes/ir0/` | Required façade headers exist (`arch_port.h`, `mm_port.h`, `sched.h`, `task.h`, …) |
| `facade-no-drivers-include` | `includes/ir0/*.h` | No `#include <drivers/…>` |
| `facade-no-arch-include` | `includes/ir0/*.h` | No `#include <arch/…>` |
| `facade-no-sched-include` | `includes/ir0/*.h` | No `#include <sched/…>` |
| `portable-no-interrupt-arch` | `fs/`, `kernel/`, `mm/`, `net/` | No `#include <interrupt/arch/...>` (use **`ir0/irq.h`**) |
| `process-pgd-accessor` | broad | No `process->page_directory`; use **`process_pgd`/`process_set_pgd`** |
| `syscall-frame-accessor` | portable trees | No `syscall_frame.<gpr>`; use **`process_syscall_*`** |
| `portable-no-task-arch-field` | portable | No `task->arch.<field>`; use **`task_get_*`/`task_set_*`** |
| `fs-no-direct-arch` | `fs/` | No `#include <arch/...>`; use **`ir0/arch_port.h`** |
| `fs-no-mm-include` | `fs/` | No `#include <mm/...>`; use **`ir0/mm_port.h`** or narrower facades |
| `mm-net-no-arch-include` | `mm/`, `net/` | No `#include <arch/...>`; use **`ir0/arch_port.h`** |
| `portable-no-kernel-header` | `fs/`, `mm/`, `net/`, `drivers/` | No `#include <kernel/...>` |
| `driver-block-dev-facade` | `drivers/` | No raw `#include <drivers/storage/block_dev.h>`; use **`ir0/block_dev.h`** |
| `drivers-no-arch` | `drivers/` | No `#include <arch/...>`; use **`ir0/arch_port.h`** |
| `kernel-no-driver-include` | `kernel/` (whole tree) | No `#include <drivers/...>` |
| `kernel-use-arch-port-facade` | `kernel/` | No `#include <arch/common/arch_portable.h>`; use **`ir0/arch_port.h`** |
| `bluetooth-include-scope` | Outside `drivers/bluetooth/` | No `#include <bluetooth/...>` |
| `portable-no-isa-asm` | `kernel/`, `net/`, `mm/`, `sched/`, `drivers/`, `fs/`, `includes/ir0/` | No ISA mnemonics in asm; use **`cpu_relax`/`smp_mb`/`inb`/…** ([`ir0/cpu.h`](../../includes/ir0/cpu.h)). Allowlist: `syscall_x86_64.h` / `syscall_arm64.h`. |
| `usercopy-no-cr3-memcpy` | `kernel/syscalls/**`, `kernel/lib/signals.c` | No `load_page_directory` + raw `memcpy((void *)` / `memset((void *)` in the same function; use **`copy_*_user` / region helpers** ([`uaccess.md`](uaccess.md)). |
| `usercopy-signals` | `kernel/lib/signals.c` | No `memcpy((void *)` (signal frame must use region copy). |
| `usercopy-sys-uname` | `sys_uname` | No `memset(buf` / `strncpy(buf->`; bounce + **`copy_to_user`**. |
| `devfs-io-contract-usercopy` | `fs/devfs.c` | Device I/O must usercopy via allowlisted helpers ([`devfs-io-contract.md`](devfs-io-contract.md)). |

### Binary stability check

After building **`kernel-x64.bin`**, a SHA‑256 of sorted globally defined symbols (`nm`) is:

```bash
python3 scripts/kernel_export_digest.py kernel-x64.bin
```

Use this to compare before/after refactors **with the same toolchain**; bytecode-identical reproducible ELF is a stronger (optional) goal.

## Layered overview

```text
  IR0-userspace (sibling) / ash
           │
           ▼  syscalls only
       kernel/, fs/, mm/, net/
           │
           ├── includes/ir0/*     (preferred public API toward “drivers + services”)
           │
           ├── kernel/*           some files still include drivers/ or arch/ (see leaks)
           │
           ▼
       drivers/, arch/, interrupt/
```

**Policy (CTR):** In `fs/*` and `kernel/syscalls.c`, avoid adding direct
`#include <drivers/...>`. Prefer extending facades under `includes/ir0/`.

## Facade inventory (`includes/ir0/`)

These headers are the intended stable surface for callers that should not depend on a
specific driver implementation file:

| Area | Facade header(s) | Typical consumer |
|------|------------------|------------------|
| CPU / ISA queries for pseudo-fs | `arch_port.h` | **`fs/`** (`/proc` CPU text; wraps `arch_portable.h`) |
| Block I/O | `blockdev.h` (`ir0_block_*`; `block_dev.h` compat) | `fs/` (Minix/FAT/ext2/devfs) |
| Partitions | `partition.h` | `fs/devfs.c`, proc/sys helpers |
| Console output | `console_backend.h` | `fs/` (no `vga.h` in `fs/`) |
| MM stats (opaque) | `mm_port.h` | `fs/procfs.c` |
| Clock / wall time | `clock.h`, `rtc.h` | Time-related code |
| Networking abstractions | `net.h` | Stack integration points |
| Input | `input_backend.h` (mouse + kbd); `keyboard.h` thin compat | `/dev`, console, syscalls |
| Video | `video_backend.h`, `vga.h` (non-`fs/` legacy) | Diagnostics, optional UI |
| Kernel log | **`ktm/klog.h`** via `<ir0/ktm/klog.h>` (`[ts] [LEVEL] [COMP]`) | Prefer over raw `serial_print`; hub in `ktm/klog.c` |
| Serial (low-level) | `serial_io.h` | Sink used by klog + `/dev/serial` putchar |
| Syscalls ABI | `syscall*.h`, `copy_user.h` | Arch-specific stubs |
| Driver model | `driver.h`, `driver_bootstrap.h`, `init_drv.h` | Registry, boot order (opaque headers) |
| Power | `platform_ops.h` | `kernel/power/power_manag.c` |
| Scheduler | **`sched.h`** (`scheduler_api.h` → alias) | Process/timer/IRQ → `sched_schedule_next()` |
| IRQ subsystem | **`irq.h`** | Boot IDT/PIC/GIC + keyboard poll; not `interrupt/arch/*` from portable/drivers |
| UP spinlock | **`spinlock.h`** (`ir0_spinlock_t`; irq-save on UP) | Hot paths e.g. `kernel/lib/pipe.c` read/write |
| Page-fault debug | **`arch_pf_debug.h`** (`pf_debug_*`; x86 frame layout) | `mm/page_fault.c` calls; impl in `arch/*/sources/arch_pf_debug.c` |
| Signal sigreturn ctx | `process_saved_context_*()` in `process.h` | Only `kernel/process/saved_context.c` may touch `->saved_context` |
| Context switch | **`context.h`**, **`arch_switch.h`** | `switch_to` / kernel stack handoff |
| MM root layout | **`arch_mm.h`** | User/kernel half of page-table root |
| Resources | `resource_registry.h` | IRQ / I/O port registration from drivers |
| VFS-backed devices | `devfs.h`, `procfs.h`, `sysfs.h` | Virtual filesystems |
| Pseudo-fs registry | `pseudo_fs.h` | Longest-prefix table for `/proc` / `/sys` nodes |
| Credential helpers | `credentials.h` | **`kernel/credentials.c`** (`ir0_current_cred`, PID, access checks without pulling `kernel/*.h`) |
| `process_t` / task globals | `process.h` | `/proc`, `mm/paging.c` fork paths needing struct layout |
| Bluetooth façade | `bluetooth.h` | `ir0_bluetooth_poll`, `ir0_bluetooth_register_driver` without `drivers/bluetooth/*.c` callers |

REQUIRED façades under `includes/ir0/*.h` must not `#include <drivers/…>`,
`<arch/…>`, or `<sched/…>` (`facade-no-drivers-include` /
`facade-no-arch-include` / `facade-no-sched-include` in `architecture_guard.py`).
`arch_port.h` pulls `arch_io.h` + `arch_cpu.h` (no `<arch/…>`). Canonical
`task_t` lives in `ir0/task.h`; `sched/task.h` and `ir0/context.h` reexport it.
Implementations may include drivers from `.c` adapters (`input_backend.c`,
`blockdev.c`, …). `mm_port.h` is opaque (no `<mm/…>`). Prefer `blockdev.h` /
`ir0_block_*` over legacy `block_dev_*` names in new `fs/` code.

## Registration and callback patterns

| Mechanism | Types / APIs | Purpose |
|-----------|----------------|---------|
| **Driver registry** | `ir0_driver_ops_t`, `ir0_register_driver()` | Lifecycle + generic I/O; see `kernel/driver_registry.c`. |
| **Block devices** | `ir0_block_ops` / `ir0_block_register()` | Sector I/O; ATA/AHCI/virtio register via facade. |
| **Filesystems** | `struct vfs_ops`, `vfs_fstype` | Mountable FS plugins (`minix_fs.c`, `tmpfs.c`, `simplefs.c`, …). |
| **Character devices** | `devfs_ops_t`, `devfs_register_device()` | Per-node read/write/ioctl/open/close in `fs/devfs.c`. |
| **Bootstrap** | `driver_boot_init_fn`, `driver_bootstrap_register()` | Staged early init wired from `drivers/init_drv.c` with `CONFIG_INIT_*`. |
| **Resources** | `resource_register_irq`, `resource_register_ioport` | Naming / tracking; drivers include [`includes/ir0/resource_registry.h`](../../includes/ir0/resource_registry.h). |

**Preferred patterns for new code**

| Use case | Preferred API |
|----------|----------------|
| Optional driver at boot | `driver_boot_init_fn` + `CONFIG_INIT_*` |
| Block storage | `ir0_block_*` registered by name |
| `/dev` node I/O | `devfs_ops_t` per node |
| Mountable filesystem | `struct vfs_ops` + fstype registration |
| Network NIC | `struct net_device` vtable in `ir0/net.h` |
| Bluetooth to VFS | `ir0_bt_*` facade functions |
| `/proc` or `/sys` file | FD dispatch in `fs/procfs.c` / `fs/sysfs.c` (legacy) |

**Proc/sys dispatch note:** `proc_entry_t` in [`includes/ir0/procfs.h`](../../includes/ir0/procfs.h) documents a tree-shaped callback model; runtime dispatch remains **FD + switch** in [`fs/procfs.c`](../../fs/procfs.c). Extend internal tables when adding pseudo-files.

**pseudo_fs registry**

`pseudo_fs_register()` / `pseudo_fs_lookup()` ([`includes/ir0/pseudo_fs.h`](../../includes/ir0/pseudo_fs.h),
[`fs/pseudo_fs_registry.c`](../../fs/pseudo_fs_registry.c)) own the longest-prefix routing for `/proc`
and `/sys` paths that use the FD table model. Consumers register a stable `pseudo_fs_ops_t` (`read`,
`write`, `open`, `close`, optional `stat`); lookups run at `open`/read/write time so new nodes stay
orthogonal to legacy `strncmp` switches where both coexist.

**/dev open/close contract**

`/dev` nodes use `devfs_ops_t`: `open`/`close` are optional hooks on the registry entry (`fs/devfs.c`).
Callers closing an FD must drive `close` when present so refcounted/stateful devices (`bluetooth/hci0`,
session-oriented consoles, NIC helpers) remain consistent with POSIX expectations; skips count as ABI
bugs for multi-open devices.

## Configuration pipeline

1. **`setup/Kconfig`** defines symbols (`ENABLE_*`, `ARCH_*`, `INIT_*`, filesystem toggles).
2. **`.config`** is produced by `make menuconfig` / `make defconfig` (defaults from
   `setup/defconfig`).
3. **`Makefile`** maps `CONFIG_*` to object lists (`*_OBJS`), `CFLAGS` defines, and
   architecture objects.
4. **Guards:** `build-matrix-min`, `build-matrix-full`, `config-wiring-check`,
   `arch-config-check`, and `arch-guard` validate that configs still build together.

Unset or missing `.config` often treats optional features as **enabled** (`ifneq (...),n)`
pattern); treat explicit `n` as the compile-out signal.

## Multi-architecture state (x86-64 vs ARM64)

| Aspect | x86-64 | ARM64 |
|--------|--------|-------|
| **Link rule** | `kernel-x64.bin` links **`$(ALL_OBJS)`** (full kernel). | `kernel-arm64.bin` links **only `$(ARCH_OBJS)`** (stubs + early bring-up). |
| **Boot** | Multiboot → full kernel bring-up. | Freestanding `boot_stub` + `mmu_early` + VBAR/EL0 (`make smoke-arm64`). |
| **Userspace** | musl / BusyBox / TCC / Doom on x86. | **None** in the ARM image (EL0 SVC smoke only). |
| **Maturity** | Production ISA for 0.0.1. | **≪10% of a real port** — CPU privilege path only. |

So **menuconfig toggles primarily affect the full x86-64 image**. ARM64 F7.1–F7.3 closed
**early EL1/EL0 bring-up** on QEMU virt; they did **not** deliver OS parity. A real port still
needs `ALL_OBJS` (or equivalent) for `ARCH=arm64`, fault/syscall/mm/fs/sched, and a rootfs.
## Known upward coupling (not via `includes/ir0/` alone)

Direct `#include <drivers/...>` or tight `#include <arch/...>` in non-driver trees (grep
baseline; evolves with refactors):

- **`kernel/main.c`:** portable boot calls **`boot_irq_unmask()`** after **`irq_init()`**; PIC details live in `arch/common/arch_interface.c`.
- **`mm/`:** use **`ir0/serial_io.h`** for logging where applicable.
- **`net/`:** use **`ir0/serial_io.h`** and **`ir0/clock.h`** for diagnostics and time.
- **`fs/`:** CPU queries go through **`includes/ir0/arch_port.h`** (no direct `<arch/...>` includes).
- **`includes/ir0/`:** thin facades may still include concrete driver headers (`serial_io.h` →
  `drivers/serial/serial.h`) until split API-only headers exist.

Drivers register IRQ/I/O ports through **`includes/ir0/resource_registry.h`**. They must not pull
`kernel/*.h` headers directly (`scripts/architecture_guard.py` requires facades); scheduler entry still
uses **`includes/ir0/sched.h`**.

## Assessment: are we on a scalable path?

**Yes, for subsystem composition:** facades + registry + Kconfig/Makefile gating give a
clear pattern for optional drivers (including a future USB host stack) **if** new code wires
through `includes/ir0/*`, `CONFIG_*`, and `Makefile` consistently.

**Gaps vs “total” decoupling:**

1. **`kernel/` and `net/`:** reduce remaining direct `arch_portable` surface where a narrower façade
   helps testing (optional split of “CPU info” vs “I/O port” APIs).
2. **Facades that include drivers:** optionally split **API-only** headers from `.c`.
3. **ARM64 product image:** freestanding bring-up exists (`smoke-arm64`); `kernel-arm64.bin`
   still links **only** `$(ARCH_OBJS)`. ISA facades are wired:
   `sigcontext_arm64` = Linux aarch64 uapi; `arch_switch` / `arch_mm` = TTBR0 model;
   `irq_*` → VBAR + GICv2. Full **musl aarch64 + ALL_OBJS** (x86 drivers excluded) remains
   future work.
4. **`interrupt/arch/`** is the **x86-only** IDT/PIC/ISR backend (see `interrupt/arch/README.md`).
   Portable and driver code enter via **`ir0/irq.h`** / **`ir0/arch_io.h`**; arm64 never
   links this tree.

## Testing and menuconfig ergonomics

- **Unit / harness:** ops tables (`block_dev_*`, vfs, devfs) are natural seam for fake
  backends; keep tests on the façade side where possible.
- **Integration:** rely on `build-matrix-*`, `runtime-*` checks from the Makefile as
  exercised in CTR.
- **Menuconfig:** user-chosen profiles work best when each `CONFIG_*` has one Makefile owner
  and avoids “silent default on when unset”; document quirks in `TOOLING.md` if behavior
  changes.

## Related files

| File | Role |
|------|------|
| `includes/ir0/driver.h` | `ir0_driver_ops_t` and registry API |
| `drivers/driver_bootstrap.c` | Bootstrap callback list |
| `drivers/storage/block_dev.h` | `block_dev_ops_t` |
| `fs/vfs.h` | `vfs_ops` / fstype registration |
| `includes/ir0/arch_port.h` | Arch-neutral CPU/I/O façade (no `<arch/…>` includes) |
| `includes/ir0/task.h` / `context.h` | Canonical `task_t` + context-switch decls |
| `scripts/architecture_guard.py` | Driver + facade + pseudo-fs/bluetooth/block_dev include policy |
| `scripts/kernel_export_digest.py` | SHA-256 of sorted `nm -g` export list |
| `Makefile` | `ALL_OBJS`, `ARCH_OBJS`, gated `*_OBJS` |

---

*Document generated from codebase review (facades, registries, Makefile, Kconfig). Update when
major refactors change include boundaries.*
