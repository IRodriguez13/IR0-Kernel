# IR0 Virtual Filesystems

> **Last verified:** 2026-09-04
> **Source of truth:** `fs/procfs.c`, `fs/sysfs.c`, `fs/devfs.c`, `fs/heartfs.c`,
> `fs/pseudo_fs_registry.c`, `fs/pseudo_fs_nodes.c`,
> `kernel/syscalls/mm_syscalls.c` (`sys_sysinfo`),
> `fs/vfs.c` (`vfs_statfs`), `includes/ir0/statfs.h`,
> `kernel/test/test_procfs.c` (`procfs_pid_maps` ktest),
> [`PSEUDO_FS_HEART.md`](PSEUDO_FS_HEART.md),
> [`KLOG.md`](KLOG.md) (`/proc/kmsg`, `/dev/kmsg`)

This document focuses on pseudo-filesystems exposed through VFS.

## `/heart`

IR0-only unified read-only facade (does **not** replace `/proc` or `/sys`).
See [`PSEUDO_FS_HEART.md`](PSEUDO_FS_HEART.md) for layout, gates, and ARCH-3 notes.

## `/proc`

`procfs` exposes runtime kernel and process data.

### Common Endpoints

- `/proc/meminfo` — includes `MemAvailable`, `Buffers` and `Cached`
- `/proc/uptime`
- `/proc/stat` — aggregate + `cpu0` jiffy lines for BusyBox `top` (see below)
- `/proc/loadavg`
- `/proc/version`
- `/proc/filesystems`
- `/proc/mounts`
- `/proc/drivers`
- `/proc/interrupts`
- `/proc/blockdevices`
- `/proc/partitions`
- `/proc/kmsg` — structured klog records (`klog_read_records`, see [`KLOG.md`](KLOG.md))
- `/proc/[pid]/status`
- `/proc/[pid]/cmdline`
- `/proc/[pid]/stat` — Linux `proc(5)` field order (CPU counters still mostly 0)
- `/proc/[pid]/maps` — mapped memory regions (heap, mmap, stack) from `mm_struct`
- `/proc/[pid]/statm` — page-count summary (`size resident shared text lib data dt`)
- `/proc/self/...` — resolves to the calling process PID (Linux style)
- `/proc/self` — symlink (`DT_LNK` in `ls /proc`); `readlink` target is the
  decimal PID string (Linux `proc(5)`)
- `/proc/[pid]/exe` — symlink to the absolute path last passed to `execve`
  (`process_t.exe_path`, set in `elf_loader.c`)

### `/proc/[pid]/maps` and `/proc/[pid]/statm`

Both files are served by the dynamic `/proc` matcher
(`proc_pid_file_match` in `fs/pseudo_fs_nodes.c`) and read a locked snapshot of
`mm_struct` (`proc_fs_snap_acquire` in `fs/procfs.c`).

`maps` uses the Linux `proc(5)` line layout
`start-end perms offset dev inode  pathname` and emits only regions IR0
actually tracks, with real bounds and prot bits:

```text
<heap_start>-<heap_end> rw-p 00000000 00:00 0          [heap]
<mmap_addr>-<mmap_end>  <r/w/x><p|s> 00000000 00:00 0
<stack_start>-<end>     rw-p 00000000 00:00 0          [stack]
```

`offset`/`dev`/`inode` are `0`/`00:00`/`0` because user mappings are not backed
by on-disk inodes yet. Nothing is fabricated — a kernel thread with no user
`mm` produces an empty `maps`.

`statm` reports real total virtual size (`size`, in pages) and resident pages
(`resident`, from `mm_count_resident_user_pages`); `shared`, `text`, `lib`,
`data` and `dt` are `0` (no per-segment accounting yet).

Runnable proof: the `procfs_pid_maps` ktest (`kernel/test/test_procfs.c`) opens
`/proc/self/maps` and `/proc/self/statm` via `sys_open`/`sys_read` and asserts
the format under `make kernel-tests`.

### `/proc/[pid]/fd` and `/proc/[pid]/environ`

- **`/proc/<pid>/fd/`** — directory listing numeric symlinks `0..63` for in-use
  `fd_table` slots (`proc_readdir` + `DT_LNK`). Each `/proc/<pid>/fd/N` resolves
  via `readlink(2)` / `proc_readlink()` to `fd_entry.path` when set, or honest
  synthetics such as `[pipe]` / `[socket]` when the slot has no path string.
- **`/proc/<pid>/environ`** — NUL-separated environment blob copied at exec into
  `process_t.saved_environ` (`process_saved_environ_set` in `elf_loader.c`);
  inherited on fork. Empty for kernel threads with no exec-time env.

Runnable proof: `procfs_pid_fd` and `procfs_pid_environ` ktests in
`kernel/test/test_procfs.c`.

### `/proc/self` and `/proc/[pid]/exe` symlinks

- **`/proc/self`** — visible in `ls /proc` as `self` (`proc_readdir` +
  `DT_LNK`). `readlink("/proc/self")` returns the caller's PID as decimal text;
  `stat` reports `S_IFLNK`. Opening `/proc/self` as a directory lists the same
  entries as `/proc/<pid>/` for the calling task.
- **`/proc/<pid>/exe`** — listed under each live PID directory (`DT_LNK`).
  `readlink` returns `process_t.exe_path` when set at exec; `-ENOENT` when the
  task has no recorded executable (e.g. kernel thread).

Runnable proof: `procfs_self_symlink` and `procfs_pid_exe` ktests in
`kernel/test/test_procfs.c`.

### `/sys/kernel/mm`

Read-only kernel MM snapshot (same PMM + heap sources as `/proc/meminfo`):
`MemTotal`, `MemFree`, `MemAvailable`, `MemUsed`, raw frame counters
(`PmmTotalFrames`, `PmmUsedFrames`, `PmmFreeFrames`), `Slab`, `SlabTotal`,
`SlabAllocs`, `PageSize`, `KStackMinFree`, `IrqNestMax`, `KStackPeak`.
Registered in `fs/pseudo_fs_nodes.c` as `/sys/kernel/mm`.

### `/proc` intermediate directories

Registry subdirectories such as `/proc/net` and `/proc/bluetooth` have no exact
node but hold registered leaves. `proc_stat()` reports them as real directories
(`S_IFDIR | 0555`) with a live timestamp via `pseudo_fs_path_has_children()`, so
`stat`/`ls -ld` behave like Linux instead of returning `ENOENT`.

### `/proc/stat` (BusyBox `top`)

Registered via `pseudo_fs_register("/proc", "stat", …)` → `proc_stat_read()`.

Contract (Linux `proc_stat(5)` / BusyBox `read_cpu_jiffy`):

```text
cpu  user nice system idle iowait irq softirq steal
cpu0 user nice system idle iowait irq softirq steal
…
```

- Jiffies use `CONFIG_TICK_RATE_HZ`; idle from `clock_get_idle_milliseconds()`.
- Non-idle time is split user/system heuristically (no per-process cputime yet).
- Guest check: `python3 scripts/smoke_proc_stat_top.py` (ISD development disk +
  `top -bn1`).

### `/proc` readdir and `getdents`

`proc_readdir("/proc")` lists **numeric PID directories first**, then `pid/`, then
static registry children. Reason: `sys_getdents` uses `GETDENTS_BATCH_MAX` (24)
on a stack-sized `vfs_dirent` array; filling the batch with static names alone
left BusyBox `top`/`ps` with **no** digit dirents → `no process info in /proc`.
Static files remain openable by path even when truncated from a directory listing.

### Notes

- Data is generated at read time.
- Numeric formatting was hardened for 64-bit values.
- Opens install real `fd_table` slots (`is_pseudo`); no global virtual fds for new opens.
- Path-based readdir for `/proc`, `/proc/pid`, `/proc/pid/N` via `proc_readdir()`.
- `/proc/kmsg` mirrors the same event ring as serial (not the legacy textual-only path).

### Memory and uptime reporting

BusyBox `free` and `uptime` go through **`sysinfo(2)`** (syscall 99), not
through `/proc`. While it was unimplemented the applets printed uninitialised
stack after `ENOSYS`, which is where "57 days of uptime" and gigabytes of used
RAM came from. `sys_sysinfo` fills uptime, load averages, memory totals and
process count from kernel state; `_Static_assert`s pin the struct layout to the
Linux ABI.

`MemAvailable` is reported equal to `MemFree`, and `Buffers` / `Cached` are
zero. That is accurate rather than unimplemented: IR0 has no reclaimable page
cache, so nothing is held back that a request could reclaim. `free` reads
`MemAvailable` for its `available` column and printed `0` while the field was
absent.

### Timestamps

Pseudo-filesystem `stat` handlers must call `pseudo_fs_stat_now()`, which sets
`st_atime` / `st_mtime` / `st_ctime` from `clock_get_current_time()`. Handlers
that only `memset` the struct report the 1970 epoch, which is what `ls -l`
showed across `/proc`, `/sys` and `/heart`.

Wall-clock time, not uptime: `tmpfs` and `minix_fs` both derived timestamps
from the millisecond uptime counter and produced 1970 dates on a freshly booted
system.

### Mount visibility

`/proc/mounts` lists the real VFS mount list, and `/etc/mtab` is a symlink to
it, so `df` and `mount` see whatever is actually mounted. Only the root
filesystem is mounted at boot, so a single real row is expected output, not a
missing-mount bug.

Pseudo-filesystems are not VFS mounts, but `proc_mounts_read()`
(`fs/procfs.c`) appends them so the session sees the namespaces it is actually
using, as Linux does:

```
proc /proc proc rw 0 0
sysfs /sys sysfs rw 0 0
devtmpfs /dev devtmpfs rw 0 0
```

`vfs_statfs()` (`fs/vfs.c`) matches those prefixes before `find_mount()` and
reports the Linux magic (`PROC_SUPER_MAGIC`, `SYSFS_MAGIC`, `TMPFS_MAGIC` for
devfs, as devtmpfs) with zero blocks. Without that match `find_mount()` would
attribute `/proc` to the root mount and `df` would print it with the size of
the disk; the zero blocks also make `df` skip these rows without `-a`.

`/heart` is served through the same registry but is IR0-specific and is not
listed.

## `/dev`

`devfs` exposes kernel device entry points.

### Common Nodes

- `/dev/null`, `/dev/zero`
- `/dev/console`, `/dev/tty`
- `/dev/kmsg` — same event backend as `/proc/kmsg` on read; writes → `klog_info("USER", …)`
- `/dev/disk`
- `/dev/net`
- `/dev/audio`
- `/dev/mouse`

### Notes

- Access uses standard syscall I/O from user-style binaries.
- Device registration is routed through driver/bootstrap infrastructure.

## `/sys`

`sysfs` exposes kernel/system data in a structured filesystem namespace.
Nodes are registered in `pseudo_fs_nodes_register_all()` (`fs/pseudo_fs_nodes.c`)
and backed by handlers in `fs/sysfs.c`.

### Common Endpoints

- `/sys/kernel/hostname` — live hostname (read/write)
- `/sys/kernel/version` / `/sys/kernel/osrelease` / `/sys/kernel/build`
- `/sys/kernel/features` — compiled-in feature summary
- `/sys/kernel/max_processes` — configured process table limit
- `/sys/kernel/panic` — **write** any byte to force `panicex(TESTING)`
  (root-only, `0644`); read returns help text
- `/sys/devices/system/cpu<N>` and `/sys/devices/system/cpu<N>/online`
- `/sys/devices/system` / `/sys/devices/block`
- `/sys/console/mode`
- `/sys/class/net` and `/sys/class/net/<iface>/...` (dynamic per interface)
- `/sys/class/bluetooth/hci0/{address,state}`,
  `/sys/class/bluetooth/{topology/neighbors,sessions}`

### Notes

- Error handling paths use consistent negative errno returns.
- Console and backend exposure route through facade-backed interfaces.
- Directory `stat` uses `pseudo_fs_stat_now()` so `ls -l` shows wall-clock dates.
- `/sys/kernel/panic` mirrors the full panic dump to VGA/serial **and** the GTK
  framebuffer (`console_backend_panic_screen_on` + `klog` screen sink); guest
  check: `python3 scripts/smoke_sys_panic.py`.

## In-Memory Pseudo Backends

- `tmpfs`: writable memory-backed tree with uid/gid and umask-aware create.
- `procfs`, `devfs`, `sysfs`: dynamic pseudo filesystems.

## Strengths

- Strong observability at runtime without external debug tooling.
- Consistent user-facing access model through open/read/write/stat patterns.
- Product exploration: ash + `cat /proc/kmsg` (in-kernel dbgshell removed).
- BusyBox `top -bn1` works once `/proc/stat` + digit `/proc` dirents are present.

## Weak Points

- Some endpoints remain intentionally minimal and need richer semantics.
- Coverage of edge-case parsing/format compatibility still depends on runtime tests.
- `getdents` batch size still caps how many `/proc` names a single listing returns.
- `/proc/stat` CPU breakdown is approximate until real per-task accounting exists.
- `/heart` is absent from `/proc/mounts`, unlike the other pseudo-filesystems.
- `statfs` on a pseudo-filesystem reports zero blocks by design; there is no
  per-node accounting behind it.
