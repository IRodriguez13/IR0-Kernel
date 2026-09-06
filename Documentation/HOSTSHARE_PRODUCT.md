# Host-share product flow — drop binary → guest exec

> **Last verified:** 2026-09-02  
> **Source of truth:** `make smoke-hostshare-exec`, `scripts/ktm_userdev_runner.py`,  
> [`virtio.md`](virtio.md),  
> [`TREE_CONTRACT.md`](../../IR0-desktop/Documentation/TREE_CONTRACT.md)  
> **Canonical hypervisor:** QEMU virtio-9p (`-virtfs` / `-fsdev`+`virtio-9p-pci`). VirtualBox is **not** supported yet.

## What this is

Development / CI path to put an ELF on the **host**, mount it in the guest as `/mnt/host`, and `execve` it under **runit** PID1 — without baking the binary into the ISO.

Kernel role: 9p + exec ABI only. No desktop code in the kernel.

## Quick path (already wired)

```bash
cd /path/to/IR0
make -s smoke-hostshare-9p      # mount + write visible on host
make -s smoke-hostshare-exec    # runit PID1 + payload exec (fork_wait case)
make -s smoke-session-chaos     # getty login + /dev chaos + 9p R/W
```

Full QEMU flag reference and virtio-net: [`virtio.md`](virtio.md).

Under the hood:

1. Temp MINIX disk with runit (`ktm_prepare_runit_hostshare_disk.sh`).
2. Supervised service runs `runit_hostshare_payload_run` → mounts `ir0share` at `/mnt/host`.
3. Guest execs `/mnt/host/ir0_payload` (or case binary renamed into the share).
4. Autokill on done tag (`HOSTSHARE_EXEC_MOUNT_OK` + case tag).

## Manual “drop a binary” recipe (QEMU)

```bash
# 1) Build a static musl ELF for the guest
MUSL_CC="${MUSL_CC:-x86_64-linux-musl-gcc}"
$MUSL_CC -static -Os -o /tmp/myprog myprog.c

# 2) Use an existing smoke share pattern, or:
SHARE=$(mktemp -d /tmp/ir0-drop.XXXXXX)
cp /tmp/myprog "$SHARE/ir0_payload"
chmod +x "$SHARE/ir0_payload"

# 3) Boot with runner --share (see smoke-hostshare-exec / DESK smokes)
#    Guest sees /mnt/host/ir0_payload after 9p mount.
```

DESK smokes in sibling IR0-desktop use the same 9p + runit pattern (`IR0_ROOT` + `run-*-smoke.sh`).

## Honesty

| Claim | Status |
|-------|--------|
| QEMU `-virtfs` 9p → `/mnt/host` | **Done** (`smoke-hostshare-9p`) |
| Exec ELF from share under runit | **Done** (`smoke-hostshare-exec`) |
| VirtualBox virtio share | **Not canónico** — later; do not claim |
| virtiofs / FUSE | **Out** — 9p MVP only |
| Product “drag file into VM GUI” | Not implemented — use share dir on host |

## Related

- IR0 `Documentation/KTM.md` § hybrid runit + 9p  
- IR0 `Documentation/STABLE.md` — Host-share 9p row  
- IR0-desktop `Documentation/TREE_CONTRACT.md` — tree separation  
