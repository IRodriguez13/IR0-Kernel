# IR0 TTY and Console

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1–T2 |
| Status | stable |
| Depends on | drivers, syscalls, devfs |
| Man page | IR0-tty (section 7) |
| Primary sources | `includes/ir0/console.c`, `kernel/console_backend.c`, `fs/devfs.c`, `interrupt/arch/keyboard.c`, `drivers/video/console_renderer.c` |

## 1. Overview

The TTY layer provides line discipline, echo, and blocking read for `/dev/console`
and `/dev/tty`. Input arrives from the PS/2 keyboard path; output goes through
`console_backend` to serial and VGA/framebuffer typewriter rendering. BusyBox
`ash` on `/dev/console` is the primary interactive userspace consumer.

## 2. Internal architecture

| Layer | File | Role |
|-------|------|------|
| Line discipline | `includes/ir0/console.c` | canonical buffer, echo, termios |
| Backend | `kernel/console_backend.c` | serial + typewriter dispatch |
| devfs | `fs/devfs.c` | `/dev/console` (id 3), `/dev/tty` (4), stdio aliases |
| Keyboard | `interrupt/arch/keyboard.c` | scancode → ASCII + input events |
| Renderer | `drivers/video/console_renderer.c` | 80×25 cells, CSI/SGR, FB scale |
| Syscalls | `kernel/syscalls.c` | `ir0_console_read`, poll wake, keymap |

**Default termios:** ICANON | ECHO | ECHOE | ISIG, ICRNL, OPOST|ONLCR, VMIN=1, VTIME=0.

## 3. Data flow

```text
  IRQ1 / keyboard_poll_ps2()
        │
        ▼
  keyboard_feed_scancode → ir0_console_keypress(c)
        │
        ├─ ICANON: edit canon_line → canon_readq (max 256)
        └─ raw: keyboard ring (256) if store_key_in_ring allowed
        │
        ▼
  read(/dev/console) → dev_console_read → ir0_console_read → tty_read_kernel
        │
        ▼
  blocked? tty_sleep_for_input → PROCESS_BLOCKED → stdin_wake_check on IRQ

  write(/dev/console) → ir0_console_write → ONLCR → console_backend_write
        │
        ▼
  serial + typewriter_vga_print → console_renderer / FB
```

**poll/readiness:** `devfs_console_can_read` → `ir0_console_poll()` → canonical queue or keyboard ring (raw mode).

## 4. Responsibilities

- TTY layer: no direct user pointer access; devfs copies termios/ioctl buffers.
- Keyboard: always feed console; ring append only when non-canonical or policy allows.
- Backend: after userspace attach, printk-to-screen may defer to typewriter FAST mode.

## 5. Subsystem boundaries

- Console must not bypass devfs for userspace I/O (ash and smokes use syscalls).
- Keyboard scancode tables under `#ifdef __x86_64__` in `keyboard.c`.
- User ioctl copies only in whitelisted devfs handlers (`architecture_guard.py`).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| devfs | console_ops on nodes 3, 4, 17, 40, 41 |
| Input | `/dev/events0` parallel evdev-style queue |
| Video | VBE framebuffer, `console_get_fb_scale` for winsize |
| Scheduler | blocked readers woken from idle poll |
| Syscalls | `__NR_console_scroll/clear`, keymap get/set |

## 7. Visual maps

```text
  PS/2 IRQ ──► keyboard.c ──► console.c (line disc.)
                                    │
                    read/write ◄────┤
                                    ▼
                              console_backend
                                    │
                         ┌──────────┴──────────┐
                         ▼                     ▼
                      serial              typewriter/FB
```

Canonical vs raw:

```text
  ICANON=1:  keys → line buffer → read returns full line
  ICANON=0:  keys → ring buffer → read honors VMIN/VTIME
```

## 8. Important invariants

1. `IR0_TTY_MAX_READ_WAITERS = 8`; `IR0_TTY_CANON_MAX = 256`.
2. stdin device id 17 (not 16 — collision with events0 documented in devfs).
3. ioctl: TCGETS/TCSETS/TCSETSW/TCSETSF, TIOCGWINSZ; other requests `-ENOTTY`.
4. `/dev/console` open triggers `ir0_console_on_userspace_attach()` once.
5. TTY does not touch user pointers — devfs/syscall layer copies.
6. Keyboard drain is shared with the input subsystem — see IR0-input invariant
   on atomic i8042 claim. Duplicated characters on the line are almost always
   that race, not a TTY echo bug.

## 9. Debugging tips

- Interactive ash smoke: `Documentation/fase58e-ash-interactive-console.md`.
- Session soak with relogin: `make smoke-session-soak SOAK_EXTRA="--relogin-every 3"`
  (or run `scripts/smoke_session_soak.py` with `--relogin-every`). Login must
  sample the getty prompt baseline *before* typing `exit`, and settle ~1.5 s
  after the username prompt; typing too early makes getty respawn without
  printing `Password:`.
- Serial: keyboard layout via `CONFIG_KEYBOARD_LAYOUT`; syscall keymap get/set.
- Blank echo but serial OK: check ICANON/ECHO termios; verify backend attach.
- poll blocked: ensure `stdin_wake_check` runs from idle loop.
- Relogin prints `Enter your Unix username:` but accepts no input: open P0 —
  getty after `CONSOLE_SESSION_REPROMPT` does not reattach stdin to the
  keyboard ring. Distinct from the i8042 duplication race (fixed).

Build/run: `make run-fase58e-ash-gui` (see SETUP.md).

## 10. Future roadmap

- Job control (tty foreground group) — **not implemented**.
- Full termios flag parity with Linux — subset only.
- USB keyboard — PS/2 primary path today.
- Multiple virtual terminals — single console focus.
- Fix getty dead-input after session end / `CONSOLE_SESSION_REPROMPT`.
