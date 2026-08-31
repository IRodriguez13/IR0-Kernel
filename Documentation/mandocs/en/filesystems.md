# IR0 Filesystems (Backends)

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | vfs, syscalls, drivers |
| Man page | IR0-filesystems (section 7) |
| Primary sources | `fs/tmpfs.c`, `fs/devfs.c`, `fs/procfs.c`, `fs/pseudo_fs_registry.c`, `fs/minix_fs.c`, `fs/hostshare_9p.c`, `drivers/virtio/virtio_9p.c`, `includes/ir0/sysfs.h` |

## 1. Overview

IR0 exposes several filesystem backends with different routing and backing models.
Block-backed **minix** and in-memory **tmpfs** register as `vfs_fstype` drivers.
**virtio-9p hostshare** (`fstype "9p"`) mounts a QEMU `-virtfs` share for deep
tree ops (mkdir/readdir/rename/write-at/**symlink**/readlink). **procfs**,
**sysfs**, and **devfs** are primarily **syscall-side** namespaces (not full VFS
mounts). Static `/proc` and `/sys` nodes also use `pseudo_fs_registry.c` for
longest-prefix dispatch.

See IR0-vfs for the two-stage router diagram.

## 2. Internal architecture

| Backend | Router | Storage | Key file |
|---------|--------|---------|----------|
| minix | VFS mount | Block device (ATA/AHCI) | `fs/minix_fs.c` |
| fat16 | VFS mount | Block device | `fs/fat16_disk.c` |
| ext2 | VFS mount (RO) | Block device | EXT2 backend |
| tmpfs | VFS mount | RAM inode tree | `fs/tmpfs.c` |
| 9p hostshare | VFS mount | QEMU virtio-9p host dir | `fs/hostshare_9p.c`, `drivers/virtio/virtio_9p.c` |
| procfs | Syscall + registry | Generated kernel text | `fs/procfs.c` |
| sysfs | Syscall + registry | Kernel/driver state | `includes/ir0/sysfs.h` |
| devfs | Syscall only | Node ops table | `fs/devfs.c` |

**devfs node:** `devfs_node_t` with `device_id`, `ref_count`, optional `ops`
(read/write/ioctl/can_read hooks). Registry max **224** nodes.

**pseudo_fs_registry:** separate tables for `/proc` and `/sys`; fd bases 1500 and 3500; max 64 static entries each, 16 dynamic matchers.

## 3. Data flow

```text
  open("/etc/passwd")     → VFS → minix → block_dev → ATA
  open("/tmp/x")          → VFS → tmpfs → RAM inode
  open("/hostshare/...")  → VFS → 9p → virtio_9p_* (TSYMLINK/TREADLINK/…)
  open("/proc/meminfo")   → proc_open → pseudo_fs or procfs generator
  open("/sys/...")        → sysfs_open → registry ops
  open("/dev/console")    → devfs_find_node → console_ops → ir0_console_*
  open("/dev/fb0")        → devfs → fb mmap path in sys_mmap
```

**Endpoint classification:**

```text
  ┌─────────────┬──────────┬─────────────────────┐
  │ Prefix      │ Backing  │ Hardware?           │
  ├─────────────┼──────────┼─────────────────────┤
  │ / (minix)   │ disk     │ yes (block_dev)     │
  │ /tmp tmpfs  │ RAM      │ no                  │
  │ /hostshare  │ virtio-9p│ yes (QEMU virtfs)   │
  │ /proc       │ generated│ no                  │
  │ /sys        │ mixed    │ sometimes (CPU info)│
  │ /dev/null   │ sink     │ no                  │
  │ /dev/fb0    │ driver   │ yes (framebuffer)   │
  │ /dev/events0│ input    │ yes (keyboard/mouse)│
  └─────────────┴──────────┴─────────────────────┘
```

## 4. Responsibilities

- **minix/tmpfs/9p:** implement `vfs_ops`; enforce backend limits and permissions.
- **9p:** map VFS symlink/readlink/mkdir/rename onto `P9_TSYMLINK` / `TREADLINK` / etc.
- **procfs:** generate text at read time; per-process fd context where needed.
- **devfs:** register nodes at init; refcount on open/close; poll hooks per device.
- **Registry:** longest-prefix match; no duplicate full_path registration.

## 5. Subsystem boundaries

- Backends must not include syscalls or process-specific harness code (`vfs_backend.h`).
- procfs reads kernel state through `includes/ir0/*` facades, not raw driver headers in new code.
- devfs ioctl user copies whitelisted in `architecture_guard.py` for console/fb/audio.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| VFS | minix/tmpfs/9p registered in `vfs_init` |
| Drivers | devfs nodes for disk, net, fb, input; virtio-9p PCI |
| Process | proc pid directories; fd owner maps for pseudo fds |
| Block | minix LBA via `ir0/block_dev.h` |

## 7. Visual maps

```text
           syscall open path
                 │
     ┌───────────┼───────────┐
     ▼           ▼           ▼
  procfs      devfs        VFS mount table
     │           │           │
  registry    node ops    minix / tmpfs / 9p
     │           │           │
  kernel      drivers     block / RAM / virtfs
  state       facades
```

## 8. Important invariants

1. tmpfs: **128 files/instance**, **64 KiB/file**, **32 mount instances**.
2. `ramfs` fstype aliases to tmpfs at `vfs_mount`.
3. proc pseudo fds 1000–1999 with per-owner PID map; sysfs offsets 3000–3999.
4. minix is default root (`CONFIG_ROOT_FILESYSTEM="minix"`).
5. Negative errno throughout all backends.
6. 9p symlink requires backend `vfs_ops.symlink` / `readlink` (see IR0-vfs).
7. 9p `statfs` asks the host and degrades instead of failing: `virtio_9p_statfs()` issues `Tstatfs` (opcode 8) against the root fid, and `vfs_statfs()` keeps its zeroed defaults if the server does not answer.

## 9. Debugging tips

- `/proc/mounts`, `/proc/filesystems`, `/proc/drivers` — live introspection.
- Hostshare tree smoke: `make smoke-hostshare-tree` — tags include
  `HOSTSHARE_SYMLINK_OK` (guest) and host `test -L` on probe path.
- devfs open fails `-ENOENT`: node not registered in `devfs_register_node`.
- tmpfs `-ENOSPC`: file count or 64 KiB cap hit.
- MINIX root fail → tmpfs fallback (serial from `vfs_init_root`).

## 10. Future roadmap

- Unified VFS registration for proc/dev/sys (today: dual router debt).
- FAT16 (RO + write audit), EXT2 RO, GPT, AHCI(+NCQ) have QEMU smokes; NVMe is Future F6.
- Richer permission model on pseudo nodes (future chmod semantics).
- Process-local mount namespaces — **not implemented**.
- 9p: more 9P2000.L ops (xattr, flock) — **not implemented**. `Tstatfs` (8) is
  implemented; `fsid` from `Rstatfs` is parsed over but discarded.

Legacy: `Documentation/FILESYSTEM.md`, `Documentation/VIRTUAL_FILESYSTEMS.md`.
