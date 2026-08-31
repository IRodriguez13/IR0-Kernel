# IR0 Virtual File System (VFS)

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | memory, syscalls, filesystems |
| Man page | IR0-vfs (section 7) |
| Primary sources | `fs/vfs.c`, `fs/vfs.h`, `includes/ir0/vfs_backend.h`, `includes/ir0/named_symlink.h`, `kernel/syscalls/fs_syscalls.c`, `fs/pseudo_fs_registry.c` |

## 1. Overview

IR0 routes almost all file-oriented kernel activity through a **path-based VFS**
that selects a backend by **longest matching mount prefix** and dispatches
operations through a `vfs_ops` function table.

This is only half of the story. At the syscall boundary, paths under `/proc`,
`/sys`, and `/dev/` are handled **before** `vfs_open()` via dedicated code in
`kernel/syscalls/fs_syscalls.c`. Think of IR0 as a **two-stage router**:

1. **Syscall router** — fast paths for pseudo filesystems and Linux ABI fd ranges.
2. **VFS router** — mount table + `vfs_ops` for real mounts (`/`, `/tmp`, …).

This chapter documents both stages because user-visible `open("/dev/console")` never
calls `vfs_open()` today, while `open("/tmp/foo")` does.

## 2. Internal architecture

### Core structures (`fs/vfs.h`)

| Structure | Role |
|-----------|------|
| `struct vfs_fstype` | Registered FS driver: name, `vfs_ops *`, optional mount/umount hooks |
| `struct vfs_mount` | Active mount: path, device string, pointer to fstype |
| `struct vfs_file` | Per-open handle: path copy, `pos`, flags, ref_count |
| `struct vfs_ops` | Backend contract in `includes/ir0/vfs_backend.h` |

Global state in `fs/vfs.c`:

- `fs_types` — linked list of registered filesystem drivers.
- `mounts` — linked list of mount entries (newest first).

### Backend contract (`includes/ir0/vfs_backend.h`)

Backends implement path operations only: `stat`, `mkdir`, `create`, `read`, `write`,
`truncate`, `readdir`, `rename`, `symlink`, `readlink`, etc. Return **0 on success**,
**negative errno** on failure. They must not call syscalls or depend on a specific
userspace workload.

Optional ops may be `NULL`. `vfs_symlink` / `vfs_readlink` call
`vfs_ops.symlink` / `vfs_ops.readlink` when present; otherwise syscalls may fall
back to `named_symlink_*` (in-kernel path→target table) for mounts without native
symlink support.

### Registered block/in-memory drivers (`vfs_init`)

Config-gated registration in `vfs_init()`:

- `minix` — disk-backed root (default in defconfig)
- `tmpfs` — in-memory tree (also serves `ramfs` alias on mount)
- `9p` — virtio-9p hostshare (`fs/hostshare_9p.c`)
- `simplefs`, `fat16` — optional experimental backends

Pseudo trees (`procfs`, `devfs`, `sysfs`) are **not** full `vfs_fstype` mounts in
the current tree; they use syscall-side dispatch plus `pseudo_fs_registry.c` for
static `/proc` and `/sys` endpoints.

### Pseudo endpoint registry (`fs/pseudo_fs_registry.c`)

Longest-prefix tables for `/proc` and `/sys` nodes registered at init from
`fs/pseudo_fs_nodes.c`. Dynamic matchers exist for per-PID paths and similar patterns.

### Syscall-side fd model (`kernel/syscalls/fs_syscalls.c`)

Process fd table uses **reserved ranges** for pseudo opens:

- `/proc` → `proc_open()` → fd in proc range
- `/sys` → `sysfs_open()`
- `/dev/...` → `devfs_find_node()` + `devfs_open_node()`

All other paths: resolve against `cwd` via `ir0_resolve_kpath_at()`, then `vfs_open()`.

## 3. Data flow

### Path A — regular file on a mount (e.g. `/sbin/init`, `/tmp/x`)

1. User calls `open(2)` → `sys_open` in `fs_syscalls.c`.
2. Path is not `/proc`, `/sys`, or `/dev/*`.
3. `ir0_resolve_kpath_at(IR0_AT_FDCWD, …)` produces absolute path.
4. Linux `O_*` flags translated by `linux_open_flags_to_ir0()`.
5. `vfs_open(path, ir0_flags, mode, &vfs_file)`:
   - `validate_path()` — length, component length, forbidden characters.
   - `check_dir_traverse()` — execute permission on each directory component (non-root).
   - `ops_for_path()` → `find_mount()` longest prefix.
   - Optional `O_CREAT` / access checks / `O_TRUNC` via `vfs_truncate`.
   - Allocate `struct vfs_file`, call backend as needed on read/write.
6. Syscall layer binds fd to vfs file or copies path into fd table.

### Path B — `/proc/meminfo`

1. `is_proc_path()` true in `sys_open`.
2. `proc_open()` — may use `pseudo_fs_registry` match or procfs-specific logic.
3. Returns fd in proc fd range; subsequent `read`/`write` bypass VFS mount ops.

### Path C — `/dev/console`

1. Prefix match on `/dev/` in `sys_open`.
2. `ensure_devfs_init()`, `devfs_find_node()`, `devfs_open_node()`.
3. `devfs_bind_fd_slot()` — TTY/console ops may reach `console_backend` facades.

### Path D — root mount at boot

1. `vfs_init_root()` from kernel bring-up.
2. `vfs_init()` registers enabled fstypes.
3. Build `/dev/<CONFIG_ROOT_BLOCK_DEVICE>`, `vfs_mount(..., "/", CONFIG_ROOT_FILESYSTEM)`.
4. On failure: fallback `vfs_mount("none", "/", "tmpfs")` if tmpfs enabled.

### ASCII routing map (mandoc-safe)

```text
  open(path)
      |
      v
  +---+-------------------+
  | fs_syscalls.c         |
  +---+-------------------+
      |
      +-- /proc/* ------> procfs + pseudo_fs_registry
      +-- /sys/*  ------> sysfs  + pseudo_fs_registry
      +-- /dev/*  ------> devfs nodes (char/block ops)
      |
      +-- else: resolve cwd -> vfs_open()
                |
                v
            find_mount(longest prefix)
                |
                v
            mount->fs->ops->stat/read/write/...
                |
                +-- minix ----> block_dev (real storage)
                +-- tmpfs ----> RAM inode tree
```

Mermaid source (richer map): `Documentation/mandocs/diagrams/vfs-routing.mmd`

## 4. Responsibilities

**VFS (`fs/vfs.c`) must:**

- Validate paths and enforce generic open/create/truncate policy.
- Select the correct mount and ops table.
- Apply credential checks via `ir0_check_file_access` and `ir0_current_cred`.
- Keep syscall-facing semantics for `O_TRUNC`, relative paths (via caller resolution), negative errno.

**Syscall layer must:**

- Translate Linux open flags before VFS sees them.
- Route `/proc`, `/sys`, `/dev` without assuming they are VFS mounts.
- Copy user paths safely; resolve `cwd` per process.

**Backends must:**

- Implement only their namespace (no cross-FS path logic).
- Enforce backend-specific limits (tmpfs 64 KiB/file, 128 files, etc.).

**Callers must not:**

- Include `drivers/*` from `fs/` (use `includes/ir0/*` facades).
- Call `minix_*` or `tmpfs_*` directly from syscalls — use `vfs_*` helpers.

## 5. Subsystem boundaries

| Rule | Enforcement |
|------|-------------|
| No `#include <drivers/...>` in `fs/` | `scripts/architecture_guard.py` |
| No `#include <arch/...>` in `fs/` | use `ir0/arch_port.h` |
| No `#include <mm/...>` in `fs/` | use `ir0/mm_port.h`, `ir0/kmem.h` |
| Mount/umount privileged | `vfs_mount` / `vfs_umount` require root cred |
| Backends stay workload-agnostic | documented in `vfs_backend.h` |

Known coupling (debt): `fs/vfs.c` includes `<ir0/vga.h>` and `<ir0/serial_io.h>` for
diagnostics — acceptable for T0 but not ideal long term.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|-------------|
| Syscalls | `fs_syscalls.c` front door; `kernel/syscalls.c` still holds monolithic dispatch |
| MM | `kmalloc` for mounts and `vfs_file`; ELF loader uses `vfs_read_file` |
| Process | Per-process `cwd`; fd table; proc-specific `/proc/[pid]/` context |
| Drivers | devfs ops → block, net, fb, input via facades |
| Config | `CONFIG_ENABLE_FS_*`, `CONFIG_ROOT_FILESYSTEM`, `CONFIG_ROOT_BLOCK_DEVICE` |
| Root layout | `kernel/rootfs_base.c` creates `/dev`, `/proc`, … dirs on MINIX via `vfs_mkdir` |

## 7. Visual maps

See section 3 ASCII map and `Documentation/mandocs/diagrams/vfs-routing.mmd`.

**Endpoint classification** (where data comes from):

| Path prefix | Router stage | Backing | Hardware? |
|-------------|--------------|---------|-----------|
| `/` (minix) | VFS | Disk blocks via `block_dev` | Yes (ATA/etc.) |
| `/tmp` (tmpfs mount) | VFS | RAM inode tree | No |
| `/proc/*` | Syscall | Generated kernel/process text | No |
| `/sys/*` | Syscall | Registry + kernel state | Mixed |
| `/dev/fb0`, `/dev/events0` | Syscall/devfs | Driver-backed ops | Yes |
| `/dev/null`, `/dev/zero` | Syscall/devfs | In-kernel sinks | No |

## 8. Important invariants

1. **Longest mount prefix wins** — `find_mount()` in `vfs.c`; must stay consistent with `pseudo_fs` longest-prefix registry for `/proc`/`/sys`.
2. **IR0_O_* only inside VFS** — raw Linux flags rejected (`ir0_open_flags_ok_for_vfs`).
3. **Root umount forbidden** — `vfs_umount("/")` returns `-EBUSY`.
4. **Nested mount busy** — cannot umount if another mount path is underneath.
5. **ramfs is tmpfs** — `vfs_mount(..., "ramfs")` resolves to tmpfs driver.
6. **Proc fd isolation** — per-process pseudo-fd context avoids cross-process collisions (see procfs chapter when available).
7. **Negative errno throughout** — VFS and backends do not return positive error codes.
8. **Symlink dispatch** — prefer backend `symlink`/`readlink`; else `named_symlink_*` fallback from `fs_syscalls.c`.
9. **statfs matches pseudo prefixes first** — `vfs_statfs()` checks `/proc`, `/sys` and `/dev` before `find_mount()`. They are not mounts, so the longest-prefix rule would otherwise hand them the root mount and report `/proc` with the size of the disk.

### statfs (`vfs_statfs`)

| Path | `f_type` | Blocks |
|---|---|---|
| `/proc` | `IR0_PROC_SUPER_MAGIC` | 0 |
| `/sys` | `IR0_SYSFS_MAGIC` | 0 |
| `/dev` | `IR0_TMPFS_MAGIC` (as devtmpfs) | 0 |
| minix mount | `IR0_MINIX_SUPER_MAGIC` | `minix_fs_statfs()` |
| tmpfs mount | `IR0_TMPFS_MAGIC` | 0 |
| 9p mount | `IR0_9P_MAGIC` | host `Tstatfs`, else 0 |
| unknown | 0 | 0 |

Magics live in `includes/ir0/statfs.h`. Zero blocks make `df` skip the row
without `-a`, which is the intent for the pseudo rows.

## 9. Debugging tips

Serial classify tags (grep serial log):

- `[ts] [INFO] [VFS] CLASSIFY VFS_FS_CONTRACT_ACTIVE` — init succeeded (`klog_info`)
- `[ts] [INFO] [VFS] CLASSIFY VFS_LINUX_RAW_FLAGS_REJECTED` — flag translation bug
- `[EXEC_AUDIT][VFS]` — ELF load path auditing when `vfs_exec_audit_begin` active

Runtime introspection:

- `/proc/mounts`, `/proc/filesystems` — static/registry text
- `/proc/drivers` — driver registry snapshot
- ktest / linux-abi audits — path open/read contracts

Host tests: `make -C tests/host run` (vfs-related cases when present).

Common failures:

| Symptom | Likely cause |
|---------|----------------|
| `-ENODEV` on VFS path | No mount covers prefix; root mount failed |
| `-ENOENT` on `/dev/x` | Node not registered in devfs |
| `-EACCES` on create | Parent not writable or traverse check failed |
| `-EINVAL` on open | Linux flags not translated before VFS |

## 10. Future roadmap

**Not implemented / debt:**

- Unified VFS registration for proc/dev/sys as true mount types (today: dual router).
- Full mount namespace per process; bind mounts.
- Rich permission model (ACLs, default ACLs on tmpfs).
- `ext2` or other disk FS — hooks exist via `vfs_register_fs`, no production backend yet.
- Split `kernel/syscalls.c` — new FS logic should land in `fs_syscalls.c` or helpers under `includes/ir0/`.
- Move VFS diagnostics off `vga.h`/`serial_io.h` to logging facade only.

**Tradeoffs accepted:**

- Syscall fast paths reduce indirection for pseudo-fs but duplicate routing rules.
- Linked-list mount table is simple for uniprocessor T0, not optimized for many mounts.
- tmpfs size caps trade POSIX fidelity for predictable kernel RAM use.

Legacy overview docs: `Documentation/FILESYSTEM.md`, `Documentation/VIRTUAL_FILESYSTEMS.md`
— prefer this chapter for routing semantics.
