# Sistemas de ficheros de IR0 (backends)

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | vfs, syscalls, drivers |
| Página man | IR0-filesystems (sección 7) |
| Fuentes principales | `fs/tmpfs.c`, `fs/devfs.c`, `fs/procfs.c`, `fs/pseudo_fs_registry.c`, `fs/minix_fs.c`, `fs/hostshare_9p.c`, `drivers/virtio/virtio_9p.c`, `includes/ir0/sysfs.h` |

## 1. Visión general

IR0 expone varios backends de sistema de ficheros con modelos de enrutamiento y
respaldo distintos. **minix** respaldado por bloque e **tmpfs** en memoria se
registran como drivers `vfs_fstype`. **virtio-9p hostshare** (`fstype "9p"`)
monta un share QEMU `-virtfs` para ops de árbol profundas (mkdir/readdir/rename/
write-at/**symlink**/readlink). **procfs**, **sysfs** y **devfs** son
principalmente espacios de nombres **del lado syscall** (no montajes VFS
completos). Los nodos estáticos `/proc` y `/sys` también usan
`pseudo_fs_registry.c` para dispatch longest-prefix.

Ver IR0-vfs para el diagrama del router en dos etapas.

## 2. Arquitectura interna

| Backend | Router | Almacenamiento | Archivo clave |
|---------|--------|----------------|---------------|
| minix | Montaje VFS | Dispositivo de bloque (ATA/AHCI) | `fs/minix_fs.c` |
| fat16 | Montaje VFS | Dispositivo de bloque | `fs/fat16_disk.c` |
| ext2 | Montaje VFS (RO) | Dispositivo de bloque | backend EXT2 |
| tmpfs | Montaje VFS | Árbol inode RAM | `fs/tmpfs.c` |
| 9p hostshare | Montaje VFS | Dir host virtio-9p QEMU | `fs/hostshare_9p.c`, `drivers/virtio/virtio_9p.c` |
| procfs | Syscall + registry | Texto generado kernel | `fs/procfs.c` |
| sysfs | Syscall + registry | Estado kernel/driver | `includes/ir0/sysfs.h` |
| devfs | Solo syscall | Tabla ops de nodos | `fs/devfs.c` |

**Nodo devfs:** `devfs_node_t` con `device_id`, `ref_count`, `ops` opcional
(hooks read/write/ioctl/can_read). Registry máx. **224** nodos.

**pseudo_fs_registry:** tablas separadas para `/proc` y `/sys`; bases fd 1500 y 3500; máx. 64 entradas estáticas cada una, 16 matchers dinámicos.

## 3. Flujo de datos

```text
  open("/etc/passwd")     → VFS → minix → block_dev → ATA
  open("/tmp/x")          → VFS → tmpfs → inode RAM
  open("/hostshare/...")  → VFS → 9p → virtio_9p_* (TSYMLINK/TREADLINK/…)
  open("/proc/meminfo")   → proc_open → pseudo_fs o generador procfs
  open("/sys/...")        → sysfs_open → ops registry
  open("/dev/console")    → devfs_find_node → console_ops → ir0_console_*
  open("/dev/fb0")        → devfs → ruta mmap fb en sys_mmap
```

**Clasificación de endpoints:**

```text
  ┌─────────────┬──────────┬─────────────────────┐
  │ Prefijo     │ Respaldo │ ¿Hardware?          │
  ├─────────────┼──────────┼─────────────────────┤
  │ / (minix)   │ disco    │ sí (block_dev)      │
  │ /tmp tmpfs  │ RAM      │ no                  │
  │ /hostshare  │ virtio-9p│ sí (QEMU virtfs)    │
  │ /proc       │ generado │ no                  │
  │ /sys        │ mixto    │ a veces (info CPU)  │
  │ /dev/null   │ sumidero │ no                  │
  │ /dev/fb0    │ driver   │ sí (framebuffer)    │
  │ /dev/events0│ input    │ sí (teclado/ratón)  │
  └─────────────┴──────────┴─────────────────────┘
```

## 4. Responsabilidades

- **minix/tmpfs/9p:** implementar `vfs_ops`; imponer límites y permisos del backend.
- **9p:** mapear VFS symlink/readlink/mkdir/rename a `P9_TSYMLINK` / `TREADLINK` / etc.
- **procfs:** generar texto en tiempo de lectura; contexto fd por proceso donde haga falta.
- **devfs:** registrar nodos al init; refcount en open/close; hooks poll por dispositivo.
- **Registry:** coincidencia longest-prefix; sin registro duplicado de full_path.

## 5. Límites del subsistema

- Los backends no deben incluir syscalls ni harness específico de proceso (`vfs_backend.h`).
- procfs lee estado kernel vía fachadas `includes/ir0/*`, no headers crudos de drivers en código nuevo.
- Copias user ioctl devfs en lista blanca en `architecture_guard.py` para console/fb/audio.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| VFS | minix/tmpfs/9p registrados en `vfs_init` |
| Drivers | nodos devfs para disco, red, fb, input; virtio-9p PCI |
| Process | directorios pid proc; mapas owner fd para fds pseudo |
| Block | minix LBA vía `ir0/block_dev.h` |

## 7. Mapas visuales

```text
           ruta open syscall
                 │
     ┌───────────┼───────────┐
     ▼           ▼           ▼
  procfs      devfs        tabla montajes VFS
     │           │           │
  registry    node ops    minix / tmpfs / 9p
     │           │           │
  estado      drivers     bloque / RAM / virtfs
  kernel      fachadas
```

## 8. Invariantes importantes

1. tmpfs: **128 ficheros/instancia**, **64 KiB/fichero**, **32 instancias de montaje**.
2. El fstype `ramfs` es alias de tmpfs en `vfs_mount`.
3. Fds pseudo proc 1000–1999 con mapa PID por owner; sysfs offsets 3000–3999.
4. minix es raíz por defecto (`CONFIG_ROOT_FILESYSTEM="minix"`).
5. errno negativo en todos los backends.
6. Symlink 9p requiere `vfs_ops.symlink` / `readlink` (ver IR0-vfs).6. Symlink 9p requiere `vfs_ops.symlink` / `readlink` (ver IR0-vfs).
7. El `statfs` de 9p consulta al host y degrada en vez de fallar: `virtio_9p_statfs()` emite `Tstatfs` (opcode 8) contra el fid raíz, y `vfs_statfs()` conserva sus valores en cero si el servidor no responde.

## 9. Consejos de depuración

- `/proc/mounts`, `/proc/filesystems`, `/proc/drivers` — introspección en vivo.
- Smoke árbol hostshare: `make smoke-hostshare-tree` — tags incluyen
  `HOSTSHARE_SYMLINK_OK` (guest) y `test -L` en host sobre la sonda.
- open devfs falla `-ENOENT`: nodo no registrado en `devfs_register_node`.
- tmpfs `-ENOSPC`: tope de conteo de ficheros o cap 64 KiB alcanzado.
- Fallo raíz MINIX → fallback tmpfs (serial desde `vfs_init_root`).

## 10. Hoja de ruta futura

- Registro VFS unificado para proc/dev/sys (hoy: deuda de router dual).
- FAT16 (RO + write audit), EXT2 RO, GPT, AHCI(+NCQ) con smokes QEMU; NVMe es Future F6.
- Modelo de permisos más rico en nodos pseudo (semántica chmod futura).
- Namespaces de montaje por proceso — **no implementado**.
- 9p: más ops 9P2000.L (xattr, flock) — **no implementado**.

Legado: `Documentation/FILESYSTEM.md`, `Documentation/VIRTUAL_FILESYSTEMS.md`.
