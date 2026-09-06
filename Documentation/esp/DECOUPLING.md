# Mapa de desacople del kernel IR0

> **Última verificación:** 2026-09-02  
> **Fuente de verdad:** `Documentation/DECOUPLING.md` (inglés) + `includes/ir0/*` +
> [`uaccess.md`](uaccess.md)

Este documento es la versión en español de `Documentation/DECOUPLING.md`. La documentación
técnica principal se mantiene en inglés en el directorio `Documentation/`; aquí se resume el
mismo contenido sin mezclar idiomas.

**Oleada ISA (2026-07-26):** fachadas `irq.h`, `arch_switch.h`, `arch_mm.h`, `arch_signal.h`,
`sigcontext_{x86_64,arm64}.h` (aarch64 = uapi Linux completo); `switch_to` → `arch_switch_to`
con TTBR0/SP_EL*; ARM64 `irq_*` → VBAR+GICv2; `interrupt/arch/` = backend x86-only.
Residual: musl aarch64 + `ALL_OBJS` producto.

## Objetivos de diseño

- **Facades:** Las capas superiores (`fs/` y rutas de syscalls) deben usar `includes/ir0/*`
  antes que incluir directamente `drivers/*` o `arch/*`.
- **Callbacks / tablas de operaciones:** Los drivers exponen comportamiento mediante punteros
  a función (`ir0_driver_ops_t`, `block_dev_ops_t`, `vfs_ops`, `devfs_ops_t`, bootstrap).
- **Configuración:** Subsistemas opcionales mediante `CONFIG_*` en `setup/Kconfig`, `.config`,
  `Makefile` y `scripts/kconfig/menuconfig.py`.

Gran parte de `kernel/`, `mm/` y `net/` enruta ya **serial** y **reloj** vía `ir0/serial_io.h` y
`ir0/clock.h`. `fs/` usa `ir0/arch_port.h` para consultas de CPU (sin `#include <arch/...>` en el
fuente). Persisten backends (`video_backend`, `console_backend`) con includes internos a drivers;
`kernel/main.c` ya usa `ir0/init_drv.h`, `ir0/block_dev.h` e `ir0/bluetooth.h` en el idle loop.

## Inventario de fachadas (`includes/ir0/`)

Ver tabla detallada en `Documentation/DECOUPLING.md`. Áreas cubiertas: disco (`blockdev` /
`ir0_block_*`), consola (`console_backend`), MM opaco (`mm_port`), tiempo (`clock`, `rtc`),
red (`net`), entrada (`input_backend`), vídeo, `klog`, modelo de drivers (`driver`,
`driver_bootstrap`, `init_drv`), **scheduler (`sched.h`; `scheduler_api.h` es alias),
recursos (`resource_registry`), tabla pseudo-fs (`pseudo_fs.h`), credenciales, power
(`platform_ops`), y vistas de proceso (`process.h`).

## Patrones de registro

- **Registro de drivers:** `ir0_register_driver` + `ir0_driver_ops_t`.
- **Bloque:** `ir0_block_register` / `ir0_block_*`.
- **Sistemas de archivos:** `vfs_ops` / `vfs_fstype`.
- **Carácter (/dev):** `devfs_ops_t`.
- **Arranque:** `init_all_drivers()` vía `includes/ir0/init_drv.h`.
- **Timer → scheduler:** `includes/ir0/sched.h` desde `drivers/` (no headers internos de `sched/`).
- **Recursos IRQ/puertos:** `includes/ir0/resource_registry.h`.
- **Bluetooth poll / boot:** `ir0_bluetooth_poll()` y registro opcional (`ir0_bluetooth_register_driver()`) vía `includes/ir0/bluetooth.h`.

## Estado multi-arquitectura

- **x86-64:** `kernel-x64.bin` enlaza **`$(ALL_OBJS)`**: kernel completo (Multiboot, etc.).
- **ARM64:** bring-up temprano en QEMU virt (`make smoke-arm64`: MMU + VBAR + EL0 + PSCI).
  `kernel-arm64.bin` sigue enlazando **solo `$(ARCH_OBJS)`** — **no** es un port terminado
  (sin musl/BusyBox/sched/VFS parity). Orden de magnitud: privilegio CPU, no OS.

Objetivos de port real: `ALL_OBJS` en ARM64, fault/syscall/mm/fs/sched, rootfs.
## Fugas conocidas de acoplamiento

Listado cualitativo actualizado en `Documentation/DECOUPLING.md`: `kernel/main.c` (política de
drivers opcionales), backends de vídeo/entrada, y algunas facadas IR0 que siguen delegando en
drivers. Las reglas ejecutadas por `scripts/architecture_guard.py` están tabuladas en
`Documentation/DECOUPLING.md` (sección *Architecture guard rules*).

### Reglas del guard de arquitectura (`scripts/architecture_guard.py`)

| Etiqueta | Ámbito | Regla |
|----------|--------|-------|
| `forbidden-include` | `fs/`, `kernel/syscalls.c` | Sin `#include <drivers/...>` |
| `missing-facade` | `includes/ir0/` | Deben existir las fachadas requeridas (`arch_port.h`, `mm_port.h`, …) |
| `portable-no-interrupt-arch` | `fs/`, `kernel/`, `mm/`, `net/` | Sin `#include <interrupt/arch/...>` |
| `fs-no-direct-arch` | `fs/` | Sin `#include <arch/...>`; usar **`ir0/arch_port.h`** |
| `fs-no-mm-include` | `fs/` | Sin `#include <mm/...>`; usar **`ir0/mm_port.h`** u otras fachadas |
| `mm-net-no-arch-include` | `mm/`, `net/` | Sin `#include <arch/...>`; usar **`ir0/arch_port.h`** |
| `portable-no-kernel-header` | `fs/`, `mm/`, `net/`, `drivers/` | Sin `#include <kernel/...>` |
| `driver-block-dev-facade` | `drivers/` | Sin `#include <drivers/storage/block_dev.h>` crudo; **`ir0/block_dev.h`** |
| `drivers-no-arch` | `drivers/` | Sin `#include <arch/...>`; usar **`ir0/arch_port.h`** |
| `kernel-no-driver-include` | `kernel/` (árbol completo) | Sin `#include <drivers/...>` |
| `kernel-use-arch-port-facade` | `kernel/` | Sin `#include <arch/common/arch_portable.h>`; **`ir0/arch_port.h`** |
| `bluetooth-include-scope` | Fuera de `drivers/bluetooth/` | Sin `#include <bluetooth/...>` |
| `usercopy-no-cr3-memcpy` | `kernel/syscalls/**`, `signals.c` | Sin `load_page_directory` + `memcpy((void *)` crudo; ver [`uaccess.md`](uaccess.md) |
| `usercopy-signals` | `kernel/lib/signals.c` | Sin `memcpy((void *)` a stack user |
| `usercopy-sys-uname` | `sys_uname` | Bounce + `copy_to_user` |
| Recursos | `resource_register_irq`, `resource_register_ioport` | Los drivers usan [`includes/ir0/resource_registry.h`](../../includes/ir0/resource_registry.h). |

**Nota proc/sys:** el runtime legacy sigue siendo **FD + switch** en `fs/procfs.c`; la tabla registrada amplía algunos endpoints sin reemplazar todavía el árbol completo.

**Patrones preferidos para código nuevo**

| Caso | API preferida |
|------|----------------|
| Driver opcional en boot | `driver_boot_init_fn` + `CONFIG_INIT_*` |
| Almacenamiento en bloque | `block_dev_ops_t` registrado por nombre |
| Nodo `/dev` | `devfs_ops_t` por nodo |
| Filesystem montable | `struct vfs_ops` + fstype |
| NIC de red | vtable `struct net_device` en `ir0/net.h` |
| Bluetooth hacia VFS | funciones `ir0_bt_*` |
| Archivo `/proc` o `/sys` | tabla FD en `fs/procfs.c` / `fs/sysfs.c` (legacy) |

**pseudo_fs registry**

Las rutas `/proc` y `/sys` basadas en la tabla FD pueden registrarse vía [`includes/ir0/pseudo_fs.h`](../../includes/ir0/pseudo_fs.h) y [`fs/pseudo_fs_registry.c`](../../fs/pseudo_fs_registry.c): prefijo más largo, `pseudo_fs_ops_t`, y lookups en open/read/write. Donde coexisten con el modelo legacy (`strncmp` central), nuevos endpoints deben seguir ambos caminos hasta converger.

**/dev contrato open/close**

Los nodos `/dev` usan `devfs_ops_t`; `open`/`close` en el registro son opcionales pero obligatorios semánticamente para dispositivos con estado/refcount (`bluetooth/hci0`, consolas con sesión, NIC). Un `close` omitido ante hook presente cuenta como incoherencia de ABI POSIX-like.

Los drivers registran IRQ/puertos vía [`includes/ir0/resource_registry.h`](../../includes/ir0/resource_registry.h).

## Composición configuración ↔ build

La cadena defconfig/menuconfig → `Makefile` → flags y listas de objetos está descrita en
inglés en `DECOUPLING.md` y en `TOOLING.md`.

- **Digest estable de símbolo export:** `python3 scripts/kernel_export_digest.py kernel-x64.bin` (véase `DECOUPLING.md`).

## Rutas relacionadas en el código

`includes/ir0/driver.h`, `drivers/driver_bootstrap.c`, `drivers/storage/block_dev.h`,
`fs/vfs.h`, `includes/ir0/devfs.h`, `setup/Kconfig`, `Makefile`.

---

*Para texto completo tabular y líneas relacionadas CTR, véase `Documentation/DECOUPLING.md`.*
