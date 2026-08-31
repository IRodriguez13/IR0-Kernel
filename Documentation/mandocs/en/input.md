# IR0 Input Subsystem

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T2 |
| Status | stable |
| Depends on | interrupts, tty, drivers |
| Man page | IR0-input (section 7) |
| Primary sources | `kernel/input_events.c`, `includes/ir0/input.c`, `interrupt/arch/keyboard.c`, `drivers/IO/ps2_mouse.c`, `fs/devfs.c` |

## 1. Overview

Input events for graphics clients (DoomGeneric) flow through a Linux-compatible
`input_event` queue exposed as `/dev/events0`. Keyboard scancodes also feed the
TTY stdin path separately. PS/2 mouse adds `EV_REL`/`EV_KEY` events when
`CONFIG_ENABLE_MOUSE` is enabled.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `input_events.c` | Ring queue, 64 `struct input_event`, IRQ producer |
| `includes/ir0/input.c` | `ir0_input_poll`, `ir0_input_read_event`, caps |
| `keyboard.c` | Scancode → ASCII (console) + `KEY_*` (events0) |
| `ps2_mouse.c` | Motion/buttons → `input_event_push` |
| `input_backend.c` | Mouse facade only (not keyboard) |
| devfs | `/dev/events0` device_id **16**, mode 0660 |

## 3. Data flow

```text
  IRQ1 → keyboard_poll_ps2 → keyboard_feed_scancode
       ├─ input_event_push(EV_KEY, KEY_*, value)  → events ring
       ├─ input_event_push(EV_SYN, SYN_REPORT, 0)
       └─ keyboard_buffer_add(ascii)              → /dev/console stdin path

  Mouse IRQ → input_mouse_handle_interrupt → keyboard_poll_ps2 (shared i8042)
  Idle poll → kernel_idle_poll → input_kbd_poll_ps2 → keyboard_poll_ps2

  read(/dev/events0) → ir0_input_read_event → dequeue (batch up to 16 events)
  poll → devfs_events0_can_read → ir0_input_poll()
```

Dual path diagram:

```text
                    keyboard IRQ
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
        ASCII ring (256)      input_event queue (64)
              │                     │
              ▼                     ▼
        /dev/console           /dev/events0
        TTY / ash              Doom / clients
```

## 4. Responsibilities

- Producer: IRQ context only pushes events.
- Consumer: read syscall dequeues; overflow **drops** silently (no back-pressure).
- devfs copies ioctl caps to user; inject ioctl only if `CONFIG_TEST_INPUT_INJECT`.

## 5. Subsystem boundaries

- Portable code uses `includes/ir0/input.h`; not raw PS/2 ports from `fs/`.
- Keyboard compiled always on x86; mouse optional Kconfig.
- stdin device id **17** (not 16 — collision with events0 documented in devfs).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| TTY | Shared keyboard hardware, separate delivery |
| Graphics | Doom reads events0 alongside fb0 |
| Interrupts | IRQ1, mouse IRQ12 |
| devfs | events0 ops, poll integration |

## 7. Visual maps

```text
  PS/2 keyboard ──► scancode tables ──┬──► console (ICANON/raw)
  PS/2 mouse    ──► ps2_mouse.c  ────┴──► input_events ring ──► events0
```

## 8. Important invariants

1. Queue depth **64** events; overflow drops.
2. `EV_SYN` is pushed after each `EV_KEY` from `keyboard_feed_scancode`.
3. Extended scancodes (0xE0 prefix) mapped for Doom key subset.
4. `ir0_input_is_available()` returns 1 on x86 bring-up; poll checks ring non-empty.
5. Read returns 0 if buffer empty (non-blocking friendly).
6. **i8042 claim is atomic** — `keyboard_poll_ps2()` holds `irq_save()` across the
   status test and the data read. Idle polling runs with interrupts enabled, so
   without that serialisation IRQ1 can land between the two `inb`s, consume the
   byte, and leave the idle path re-reading the controller's last byte. That
   produced duplicated keystrokes (`/proc//uptime`, `uptimee`). Linux protects
   the same pair under `i8042_lock`.

## 9. Debugging tips

- `CONFIG_KEYBOARD_LAYOUT`: 0=US, 1=LATAM.
- `CONFIG_TEST_INPUT_INJECT` + ioctl `IR0_INPUT_IOCTL_INJECT` for deterministic tests.
- Doom smoke: `init_fase54b-input`, `smoke-fase54b-input`.
- Product/GUI smokes probe `/dev/events0` (no in-kernel shell).

## 10. Future roadmap

- USB keyboard/mouse — PS/2 primary today.
- `EV_SYN` reporting for libinput-style clients.
- Back-pressure or larger queue for fast key repeat.
- Getty that prints `Enter your Unix username:` after `exit` but accepts no
  input (session soak stalls at relogin). Separate from the i8042 race above.
- ARM64: keyboard ring helpers under `#ifdef __x86_64__` only.

See: `IR0-tty`, `IR0-graphics`.
