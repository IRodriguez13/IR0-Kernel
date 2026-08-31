# Sistema de ficheros virtual (VFS) de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | memory, syscalls, filesystems |
| Página man | IR0-vfs (sección 7) |
| Fuentes principales | `fs/vfs.c`, `fs/vfs.h`, `includes/ir0/vfs_backend.h`, `includes/ir0/named_symlink.h`, `kernel/syscalls/fs_syscalls.c`, `fs/pseudo_fs_registry.c` |

## 1. Visión general

IR0 encamina casi toda la actividad orientada a ficheros del kernel mediante un
**VFS basado en rutas** que elige un backend por **prefijo de montaje más largo**
y despacha operaciones mediante una tabla de funciones `vfs_ops`.

Eso es solo la mitad del diseño. En la frontera de syscalls, las rutas bajo
`/proc`, `/sys` y `/dev/` se tratan **antes** de `vfs_open()` en
`kernel/syscalls/fs_syscalls.c`. IR0 actúa como **router en dos etapas**:

1. **Router de syscalls** — fast paths para pseudo filesystems y rangos de fd ABI Linux.
2. **Router VFS** — tabla de montajes + `vfs_ops` para montajes reales (`/`, `/tmp`, …).

Este capítulo documenta ambas etapas: `open("/dev/console")` no llama a `vfs_open()`
hoy, mientras que `open("/tmp/foo")` sí.

## 2. Arquitectura interna

### Estructuras centrales (`fs/vfs.h`)

| Estructura | Rol |
|------------|-----|
| `struct vfs_fstype` | Driver registrado: nombre, `vfs_ops *`, hooks mount/umount opcionales |
| `struct vfs_mount` | Montaje activo: ruta, cadena de dispositivo, puntero al fstype |
| `struct vfs_file` | Handle por apertura: copia de ruta, `pos`, flags, ref_count |
| `struct vfs_ops` | Contrato de backend en `includes/ir0/vfs_backend.h` |

Estado global en `fs/vfs.c`:

- `fs_types` — lista enlazada de drivers registrados.
- `mounts` — lista enlazada de montajes (el más reciente primero).

### Contrato de backend (`includes/ir0/vfs_backend.h`)

Los backends implementan solo operaciones por ruta: `stat`, `mkdir`, `create`,
`read`, `write`, `truncate`, `readdir`, `rename`, `symlink`, `readlink`, etc.
Devuelven **0 en éxito**, **errno negativo** en fallo. No deben invocar syscalls
ni depender de un workload concreto de userspace.

Ops opcionales pueden ser `NULL`. `vfs_symlink` / `vfs_readlink` llaman a
`vfs_ops.symlink` / `vfs_ops.readlink` si existen; si no, las syscalls pueden
caer a `named_symlink_*` (tabla in-kernel path→target) en montajes sin symlink
nativo.

### Drivers registrados (`vfs_init`)

Registro condicionado por Kconfig en `vfs_init()`:

- `minix` — root en disco (predeterminado en defconfig)
- `tmpfs` — árbol en RAM (alias `ramfs` al montar)
- `9p` — virtio-9p hostshare (`fs/hostshare_9p.c`)
- `simplefs`, `fat16` — backends opcionales experimentales

Los árboles pseudo (`procfs`, `devfs`, `sysfs`) **no** son montajes `vfs_fstype`
completos en el árbol actual; usan dispatch en syscalls más
`pseudo_fs_registry.c` para endpoints estáticos de `/proc` y `/sys`.

### Registro pseudo (`fs/pseudo_fs_registry.c`)

Tablas longest-prefix para nodos `/proc` y `/sys`, registrados al init desde
`fs/pseudo_fs_nodes.c`. Existen matchers dinámicos para rutas por PID, etc.

### Modelo de fd en syscalls (`kernel/syscalls/fs_syscalls.c`)

La tabla de fd del proceso usa **rangos reservados** para aperturas pseudo:

- `/proc` → `proc_open()` → fd en rango proc
- `/sys` → `sysfs_open()`
- `/dev/...` → `devfs_find_node()` + `devfs_open_node()`

Resto de rutas: resolución contra `cwd` con `ir0_resolve_kpath_at()`, luego
`vfs_open()`.

## 3. Flujo de datos

### Ruta A — fichero en un montaje (p. ej. `/sbin/init`, `/tmp/x`)

1. `open(2)` → `sys_open` en `fs_syscalls.c`.
2. La ruta no es `/proc`, `/sys` ni `/dev/*`.
3. `ir0_resolve_kpath_at(IR0_AT_FDCWD, …)` produce ruta absoluta.
4. Flags Linux `O_*` traducidos por `linux_open_flags_to_ir0()`.
5. `vfs_open(path, ir0_flags, mode, &vfs_file)`:
   - `validate_path()` — longitud, componentes, caracteres prohibidos.
   - `check_dir_traverse()` — permiso de ejecución en cada directorio (no root).
   - `ops_for_path()` → `find_mount()` prefijo más largo.
   - `O_CREAT` / comprobaciones de acceso / `O_TRUNC` vía `vfs_truncate`.
   - Asignar `struct vfs_file`; read/write delegan en el backend.
6. La capa syscall asocia el fd al fichero VFS.

### Ruta B — `/proc/meminfo`

1. `is_proc_path()` verdadero en `sys_open`.
2. `proc_open()` — registry o lógica procfs.
3. Fd en rango proc; `read`/`write` posteriores no pasan por ops de montaje VFS.

### Ruta C — `/dev/console`

1. Prefijo `/dev/` en `sys_open`.
2. `ensure_devfs_init()`, `devfs_find_node()`, `devfs_open_node()`.
3. `devfs_bind_fd_slot()` — ops TTY/consola vía facades `console_backend`.

### Ruta D — montaje root en boot

1. `vfs_init_root()` en el arranque del kernel.
2. `vfs_init()` registra fstypes habilitados.
3. `vfs_mount(/dev/<CONFIG_ROOT_BLOCK_DEVICE>, "/", CONFIG_ROOT_FILESYSTEM)`.
4. Si falla: fallback `vfs_mount("none", "/", "tmpfs")` si tmpfs está activo.

### Mapa ASCII (compatible mandoc)

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
      +-- /dev/*  ------> devfs nodes (ops char/block)
      |
      +-- else: resolve cwd -> vfs_open()
                |
                v
            find_mount(prefijo mas largo)
                |
                v
            mount->fs->ops->stat/read/write/...
                |
                +-- minix ----> block_dev (almacen real)
                +-- tmpfs ----> arbol inode en RAM
```

Fuente Mermaid: `Documentation/mandocs/diagrams/vfs-routing.mmd`

## 4. Responsabilidades

**VFS (`fs/vfs.c`) debe:**

- Validar rutas y aplicar política genérica open/create/truncate.
- Elegir montaje y tabla ops correctos.
- Aplicar credenciales vía `ir0_check_file_access` y `ir0_current_cred`.
- Mantener semántica syscall para `O_TRUNC`, rutas relativas (resueltas por el caller), errno negativo.

**Capa syscall debe:**

- Traducir flags Linux antes de que VFS los vea.
- Enrutar `/proc`, `/sys`, `/dev` sin asumir montajes VFS.
- Copiar rutas de usuario con seguridad; resolver `cwd` por proceso.

**Backends deben:**

- Implementar solo su namespace.
- Respetar límites propios (tmpfs 64 KiB/fichero, 128 ficheros, etc.).

**Los callers no deben:**

- Incluir `drivers/*` desde `fs/`.
- Llamar `minix_*` o `tmpfs_*` desde syscalls — usar helpers `vfs_*`.

## 5. Límites del subsistema

| Regla | Refuerzo |
|-------|----------|
| Sin `#include <drivers/...>` en `fs/` | `scripts/architecture_guard.py` |
| Sin `#include <arch/...>` en `fs/` | usar `ir0/arch_port.h` |
| Sin `#include <mm/...>` en `fs/` | usar `ir0/mm_port.h`, `ir0/kmem.h` |
| mount/umount privilegiados | requieren credencial root |
| Backends agnósticos al workload | `vfs_backend.h` |

Deuda conocida: `fs/vfs.c` incluye `<ir0/vga.h>` y `<ir0/serial_io.h>` para
diagnóstico — aceptable en T0, no ideal a largo plazo.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Syscalls | `fs_syscalls.c` como puerta; dispatch monolítico aún en `syscalls.c` |
| MM | `kmalloc` para montajes y `vfs_file`; ELF usa `vfs_read_file` |
| Proceso | `cwd` por proceso; tabla fd; contexto `/proc/[pid]/` |
| Drivers | ops devfs → block, net, fb, input vía facades |
| Config | `CONFIG_ENABLE_FS_*`, `CONFIG_ROOT_*` |
| Layout root | `kernel/rootfs_base.c` crea `/dev`, `/proc`, … con `vfs_mkdir` |

## 7. Mapas visuales

Ver mapa ASCII de la sección 3 y `Documentation/mandocs/diagrams/vfs-routing.mmd`.

**Clasificación de endpoints:**

| Prefijo | Etapa router | Respaldo | ¿Hardware? |
|---------|--------------|----------|-------------|
| `/` (minix) | VFS | Bloques disco vía `block_dev` | Sí |
| `/tmp` (tmpfs) | VFS | Árbol inode RAM | No |
| `/proc/*` | Syscall | Texto kernel/proceso generado | No |
| `/sys/*` | Syscall | Registry + estado kernel | Mixto |
| `/dev/fb0`, `/dev/events0` | Syscall/devfs | Ops con driver | Sí |
| `/dev/null`, `/dev/zero` | Syscall/devfs | Sumideros en kernel | No |

## 8. Invariantes importantes

1. **Gana el prefijo de montaje más largo** — `find_mount()`; coherente con registry pseudo.
2. **Solo IR0_O_* dentro de VFS** — flags Linux crudos rechazados.
3. **Prohibido umount de `/`** — devuelve `-EBUSY`.
4. **Montaje anidado ocupado** — no se puede desmontar si hay hijos montados.
5. **ramfs es tmpfs** — alias en `vfs_mount`.
6. **Aislamiento fd proc** — contexto pseudo-fd por proceso.
7. **errno negativo** — VFS y backends no devuelven códigos de error positivos.
8. **Dispatch symlink** — preferir backend `symlink`/`readlink`; si no, fallback `named_symlink_*` desde `fs_syscalls.c`.8. **Dispatch symlink** — preferir backend `symlink`/`readlink`; si no, fallback `named_symlink_*` desde `fs_syscalls.c`.
9. **statfs reconoce primero los prefijos pseudo** — `vfs_statfs()` chequea `/proc`, `/sys` y `/dev` antes de `find_mount()`. No son montajes, así que el prefijo más largo les asignaría el montaje raíz y `df` mostraría `/proc` con el tamaño del disco.

### statfs (`vfs_statfs`)

| Ruta | `f_type` | Bloques |
|---|---|---|
| `/proc` | `IR0_PROC_SUPER_MAGIC` | 0 |
| `/sys` | `IR0_SYSFS_MAGIC` | 0 |
| `/dev` | `IR0_TMPFS_MAGIC` (como devtmpfs) | 0 |
| montaje minix | `IR0_MINIX_SUPER_MAGIC` | `minix_fs_statfs()` |
| montaje tmpfs | `IR0_TMPFS_MAGIC` | 0 |
| montaje 9p | `IR0_9P_MAGIC` | `Tstatfs` del host, si no 0 |
| desconocido | 0 | 0 |

Los magics viven en `includes/ir0/statfs.h`. Cero bloques hace que `df` omita la fila sin `-a`, que es la intención en las filas pseudo.

## 9. Consejos de depuración

Tags serial (grep en log):

- `[ts] [INFO] [VFS] CLASSIFY VFS_FS_CONTRACT_ACTIVE` (`klog_info`)
- `[ts] [INFO] [VFS] CLASSIFY VFS_LINUX_RAW_FLAGS_REJECTED`
- `[EXEC_AUDIT][VFS]` — auditoría de carga ELF

Introspección en runtime:

- `/proc/mounts`, `/proc/filesystems`, `/proc/drivers`
- ktest / auditorías linux-abi

Fallos frecuentes:

| Síntoma | Causa probable |
|---------|----------------|
| `-ENODEV` en ruta VFS | Sin montaje; fallo de root |
| `-ENOENT` en `/dev/x` | Nodo no registrado en devfs |
| `-EACCES` en create | Padre no escribible o traverse fallido |
| `-EINVAL` en open | Flags Linux sin traducir |

## 10. Roadmap futuro

**No implementado / deuda:**

- Registro VFS unificado para proc/dev/sys como montajes reales (hoy: router dual).
- Namespace de montajes por proceso; bind mounts.
- Modelo de permisos rico (ACLs).
- `ext2` u otro FS disco — hooks vía `vfs_register_fs`, sin backend de producción.
- Split de `kernel/syscalls.c` — lógica FS nueva en `fs_syscalls.c` o `includes/ir0/`.
- Sacar diagnóstico VFS de `vga.h`/`serial_io.h`.

**Tradeoffs aceptados:**

- Fast paths en syscall reducen indirección pero duplican reglas de enrutamiento.
- Lista enlazada de montajes: simple para T0 uniprocesador, no escalada a muchos montajes.
- Límites tmpfs: fidelidad POSIX vs RAM predecible.

Docs legacy: `Documentation/FILESYSTEM.md`, `Documentation/VIRTUAL_FILESYSTEMS.md`
— preferir este capítulo para semántica de enrutamiento.
