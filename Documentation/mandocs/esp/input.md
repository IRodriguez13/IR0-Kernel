# Subsistema de entrada de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T2 |
| Estado | stable |
| Depende de | interrupts, tty, drivers |
| Página man | IR0-input (sección 7) |
| Fuentes principales | `kernel/input_events.c`, `includes/ir0/input.c`, `interrupt/arch/keyboard.c`, `drivers/IO/ps2_mouse.c`, `fs/devfs.c` |

## 1. Visión general

Los eventos de entrada para clientes gráficos (DoomGeneric) fluyen por una cola
`input_event` compatible con Linux expuesta como `/dev/events0`. Los scancodes de
teclado también alimentan la ruta stdin TTY por separado. El ratón PS/2 añade
eventos `EV_REL`/`EV_KEY` cuando `CONFIG_ENABLE_MOUSE` está habilitado.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `input_events.c` | Cola ring, 64 `struct input_event`, productor IRQ |
| `includes/ir0/input.c` | `ir0_input_poll`, `ir0_input_read_event`, caps |
| `keyboard.c` | Scancode → ASCII (consola) + `KEY_*` (events0) |
| `ps2_mouse.c` | Movimiento/botones → `input_event_push` |
| `input_backend.c` | Fachada solo ratón (no teclado) |
| devfs | `/dev/events0` device_id **16**, mode 0660 |

## 3. Flujo de datos

```text
  IRQ1 → keyboard_poll_ps2 → keyboard_feed_scancode
       ├─ input_event_push(EV_KEY, KEY_*, value)  → ring events
       └─ keyboard_buffer_add(ascii)              → ruta stdin /dev/console

  IRQ ratón → input_mouse_handle_interrupt → input_event_push(EV_REL|EV_KEY)

  read(/dev/events0) → ir0_input_read_event → dequeue (lote hasta 16 events)
  poll → devfs_events0_can_read → ir0_input_poll()
```

Diagrama de doble ruta:

```text
                    IRQ teclado
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
        ring ASCII (256)      cola input_event (64)
              │                     │
              ▼                     ▼
        /dev/console           /dev/events0
        TTY / ash              Doom / clientes
```

## 4. Responsabilidades

- Productor: solo contexto IRQ empuja eventos.
- Consumidor: syscall read hace dequeue; overflow **descarta** en silencio (sin back-pressure).
- devfs copia caps ioctl a usuario; ioctl inject solo si `CONFIG_TEST_INPUT_INJECT`.

## 5. Límites del subsistema

- Código portable usa `includes/ir0/input.h`; no puertos PS/2 crudos desde `fs/`.
- Teclado compilado siempre en x86; ratón Kconfig opcional.
- stdin device id **17** (no 16 — colisión con events0 documentada en devfs).

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| TTY | Hardware teclado compartido, entrega separada |
| Graphics | Doom lee events0 junto a fb0 |
| Interrupts | IRQ1, IRQ12 ratón |
| devfs | ops events0, integración poll |

## 7. Mapas visuales

```text
  teclado PS/2 ──► tablas scancode ──┬──► consola (ICANON/raw)
  ratón PS/2    ──► ps2_mouse.c  ───┴──► ring input_events ──► events0
```

## 8. Invariantes importantes

1. Profundidad de cola **64** eventos; overflow descarta.
2. `EV_SYN` definido pero no empujado en código actual.
3. Scancodes extendidos (prefijo 0xE0) mapeados para subconjunto de teclas Doom.
4. `ir0_input_is_available()` devuelve 1 en bring-up x86; poll comprueba ring no vacío.
5. Read devuelve 0 si buffer vacío (amigable non-blocking).5. Read devuelve 0 si buffer vacío (amigable non-blocking).
6. **El reclamo del i8042 es atómico** — `keyboard_poll_ps2()` mantiene `irq_save()` sobre el test de estado y la lectura del dato. El sondeo idle corre con interrupciones habilitadas, así que sin esa serialización la IRQ1 puede colarse entre los dos `inb`, consumir el byte y dejar al camino idle releyendo el último byte del controlador. Eso producía teclas duplicadas (`/proc//uptime`, `uptimee`). Linux protege ese mismo par con `i8042_lock`.

## 9. Consejos de depuración

- `CONFIG_KEYBOARD_LAYOUT`: 0=US, 1=LATAM.
- `CONFIG_TEST_INPUT_INJECT` + ioctl `IR0_INPUT_IOCTL_INJECT` para tests deterministas.
- Smoke Doom: `init_fase54b-input`, `smoke-fase54b-input`.
- Smokes de producto/GUI sondean `/dev/events0` (sin shell in-kernel).

## 10. Roadmap futuro

- Teclado/ratón USB — PS/2 primario hoy.
- Informe `EV_SYN` para clientes estilo libinput.
- Back-pressure o cola mayor para repetición rápida de teclas.
- ARM64: helpers ring teclado solo bajo `#ifdef __x86_64__`.

Ver: `IR0-tty`, `IR0-graphics`.
