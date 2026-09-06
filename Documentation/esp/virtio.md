# Virtio en IR0 (QEMU)

> **Última verificación:** 2026-09-02  
> **Fuente de verdad:** [`../virtio.md`](../virtio.md), `make smoke-hostshare-9p`,
> `make smoke-session-chaos`, `make smoke-nic-reach-virtio`

IR0 usa dispositivos **virtio-pci legacy** (`disable-modern=on`):

| Dispositivo | En el guest | Para qué |
|-------------|-------------|----------|
| **virtio-9p-pci** | `mount -t 9p ir0share /mnt/host` | Directorio del host ↔ archivos guest |
| **virtio-net-pci** | `virt0` | Red L3 / ping (alternativa a RTL8139) |

Fuera de alcance: virtiofs/FUSE, virtio-blk como root, VirtualBox shares.

## 9p rápido

```bash
SHARE=/tmp/ir0-share; mkdir -p "$SHARE"
# QEMU: -fsdev local,id=ir0fs,path=$SHARE,security_model=none
#       -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on
```

Guest:

```sh
mkdir -p /mnt/host
mount -t 9p ir0share /mnt/host
echo guest_ok >/mnt/host/from_guest.txt
umount /mnt/host
```

Gates: `make smoke-hostshare-9p`, `make smoke-session-chaos`.

## Red virtio

`make smoke-nic-reach-virtio` → `F8_NIC_REACH_OK` + `NIC_PING_REPLY_OK`.

## Chaos de sesión

`make smoke-session-chaos`: login + `/dev/*` + pipes + tmpfs + 9p R/W.
Falla con panic / `KERNEL_UACCESS_FAULT` / SEGV de sesión / sin prompt.

Detalle completo: [`../virtio.md`](../virtio.md).
