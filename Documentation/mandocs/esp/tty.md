# TTY y consola de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T1–T2 |
| Estado | stable |
| Depende de | drivers, syscalls, devfs |
| Página man | IR0-tty (sección 7) |
| Fuentes principales | `includes/ir0/console.c`, `kernel/console_backend.c`, `fs/devfs.c`, `interrupt/arch/keyboard.c`, `drivers/video/console_renderer.c` |

## 1. Visión general

La capa TTY proporciona disciplina de línea, eco y lectura bloqueante para
`/dev/console` y `/dev/tty`. La entrada llega desde la ruta PS/2 del teclado;
la salida pasa por `console_backend` hacia serial y renderizado typewriter
VGA/framebuffer. BusyBox `ash` sobre `/dev/console` es el consumidor interactivo
de userspace principal.

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| Disciplina de línea | `includes/ir0/console.c` | buffer canónico, eco, termios |
| Backend | `kernel/console_backend.c` | despacho serial + typewriter |
| devfs | `fs/devfs.c` | `/dev/console` (id 3), `/dev/tty` (4), alias stdio |
| Teclado | `interrupt/arch/keyboard.c` | scancode → ASCII + eventos input |
| Renderer | `drivers/video/console_renderer.c` | celdas 80×25, CSI/SGR, escala FB |
| Syscalls | `kernel/syscalls.c` | `ir0_console_read`, poll wake, keymap |

**termios por defecto:** ICANON | ECHO | ECHOE | ISIG, ICRNL, OPOST|ONLCR, VMIN=1, VTIME=0.

## 3. Flujo de datos

```text
  IRQ1 / keyboard_poll_ps2()
        │
        ▼
  keyboard_feed_scancode → ir0_console_keypress(c)
        │
        ├─ ICANON: editar canon_line → canon_readq (máx. 256)
        └─ raw: ring teclado (256) si store_key_in_ring permitido
        │
        ▼
  read(/dev/console) → dev_console_read → ir0_console_read → tty_read_kernel
        │
        ▼
  ¿bloqueado? tty_sleep_for_input → PROCESS_BLOCKED → stdin_wake_check en IRQ

  write(/dev/console) → ir0_console_write → ONLCR → console_backend_write
        │
        ▼
  serial + typewriter_vga_print → console_renderer / FB
```

**poll/readiness:** `devfs_console_can_read` → `ir0_console_poll()` → cola canónica o ring teclado (modo raw).

## 4. Responsabilidades

- Capa TTY: sin acceso directo a punteros user; devfs copia buffers termios/ioctl.
- Teclado: alimentar siempre consola; append al ring solo en no canónico o cuando la política lo permita.
- Backend: tras attach userspace, printk a pantalla puede diferir a modo typewriter FAST.

## 5. Límites del subsistema

- La consola no debe saltarse devfs para I/O userspace (ash y smokes usan syscalls).
- Tablas scancode teclado bajo `#ifdef __x86_64__` en `keyboard.c`.
- Copias user ioctl solo en handlers devfs en lista blanca (`architecture_guard.py`).

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| devfs | console_ops en nodos 3, 4, 17, 40, 41 |
| Input | cola estilo evdev paralela `/dev/events0` |
| Video | framebuffer VBE, `console_get_fb_scale` para winsize |
| Scheduler | lectores bloqueados despertados desde idle poll |
| Syscalls | `__NR_console_scroll/clear`, get/set keymap |

## 7. Mapas visuales

```text
  PS/2 IRQ ──► keyboard.c ──► console.c (disc. línea)
                                    │
                    read/write ◄────┤
                                    ▼
                              console_backend
                                    │
                         ┌──────────┴──────────┐
                         ▼                     ▼
                      serial              typewriter/FB
```

Canónico vs raw:

```text
  ICANON=1:  teclas → buffer línea → read devuelve línea completa
  ICANON=0:  teclas → ring buffer → read respeta VMIN/VTIME
```

## 8. Invariantes importantes

1. `IR0_TTY_MAX_READ_WAITERS = 8`; `IR0_TTY_CANON_MAX = 256`.
2. id dispositivo stdin 17 (no 16 — colisión con events0 documentada en devfs).
3. ioctl: TCGETS/TCSETS/TCSETSW/TCSETSF, TIOCGWINSZ; otras peticiones `-ENOTTY`.
4. open `/dev/console` dispara `ir0_console_on_userspace_attach()` una vez.
5. TTY no toca punteros user — devfs/capa syscall copian.5. TTY no toca punteros user — devfs/capa syscall copian.
6. El drenado del teclado se comparte con el subsistema de entrada — ver el invariante de reclamo atómico del i8042 en IR0-input. Los caracteres duplicados en la línea casi siempre son esa carrera, no un bug de eco del TTY.

## 9. Consejos de depuración

- Smoke ash interactivo: `Documentation/fase58e-ash-interactive-console.md`.
- Serial: layout teclado vía `CONFIG_KEYBOARD_LAYOUT`; get/set keymap syscall.
- Eco en blanco pero serial OK: comprobar termios ICANON/ECHO; verificar attach backend.
- poll bloqueado: asegurar que `stdin_wake_check` corre desde bucle idle.

Build/run: `make run-fase58e-ash-gui` (ver SETUP.md).

## 10. Hoja de ruta futura

- Control de jobs (grupo foreground tty) — **no implementado**.
- Paridad completa de flags termios con Linux — solo subconjunto.
- Teclado USB — ruta PS/2 primaria hoy.
- Múltiples terminales virtuales — foco en consola única.
