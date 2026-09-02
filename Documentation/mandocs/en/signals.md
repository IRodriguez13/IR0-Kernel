# IR0 Signals

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1 |
| Status | stable |
| Depends on | process, scheduler, interrupts |
| Man page | IR0-signals (section 7) |
| Primary sources | `includes/ir0/signals.c`, `includes/ir0/signals.h`, `kernel/syscalls.c`, `sched/rr_sched.c`, `interrupt/arch/isr_handlers.c` |

## 1. Overview

IR0 implements a subset of POSIX signals: pending bitmask, handlers, mask,
`kill`, `rt_sigaction`, `rt_sigprocmask`, and `rt_sigreturn`. Delivery runs
from `handle_signals()` during context switch. Many default actions exit the
process without invoking user handlers; full POSIX semantics are not complete.

## 2. Internal architecture

**`process_t` fields:**

```text
  signal_pending (uint32_t bitmask)
  signal_handlers[_NSIG]  (_NSIG = 32)
  signal_mask, signal_ignored
  saved_context (for sigreturn)
```

| Function | Role |
|----------|------|
| `send_signal` | Set pending bit on target process |
| `handle_signals` | Deliver on context switch |
| `register_signal_handler` | Install handler; reject SIGKILL/SIGSTOP |
| Syscalls | `sys_kill`, `sys_sigaction`, `sys_rt_sigprocmask`, `sys_sigreturn` |

## 3. Data flow

```text
  sys_kill / ISR exception / parent SIGCHLD on exit
       → send_signal(target, sig)
       → signal_pending |= mask

  sched_schedule_next (outgoing RUNNING task)
       → handle_signals()
            ├─ SIGKILL → process_exit
            ├─ SIGSTOP → PROCESS_BLOCKED
            ├─ SIGSEGV/FPE/ILL/BUS → process_exit (no user handler)
            ├─ SIGTERM/INT/QUIT/ABRT → process_exit
            └─ custom handler → build sigframe on user stack, redirect RIP/RSP
```

ASCII:

```text
  source ──► pending mask ──► handle_signals (on switch)
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
                 exit            block          user handler
                                                    │
                                                    ▼
                                              rt_sigreturn
```

## 4. Responsibilities

- ISR maps CPU exceptions to signals for user frames (`isr_handlers.c`).
- Page fault path may `send_signal(SIGSEGV)` then **synchronous** `process_exit` in `fault.c`.
- Spawn resets handlers to `SIG_DFL`; fork copies signal state via `memcpy`.

## 5. Subsystem boundaries

- `_NSIG = 32`; API `sigset_t` is 64-bit — size mismatch debt.
- `sys_sigaction` is not full Linux `rt_sigaction` (no sigsetsize arg).
- Exec does **not** reset signal state (non-POSIX).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Scheduler | `handle_signals` before context switch |
| Process | exit sends SIGCHLD to parent |
| Syscalls | sigreturn restores `task_t` from saved frame |
| Interrupts | exception → signal mapping |

## 7. Visual maps

```text
  pending:  [ bit sig ... ]
  mask:     blocks delivery in handler loop
  ignored:  SIG_IGN skips handler

  delivery order (hard-coded):
    KILL > STOP > fatal faults > term group > CONT > CHLD > custom handlers
```

## 8. Important invariants

1. SIGKILL/SIGSTOP cannot be caught or ignored.
2. `sys_kill`: only `pid > 0`; no process groups or `kill(-1)`.
3. User handler needs valid user stack in [0x400000, 0x7FFFFFFFFFFF], 16-byte aligned.
   Frames are placed by `signal_pick_handler_sp()` (also used from
   `signals_deliver_from_irq_frame`) with `SIGNAL_HANDLER_TOP_MARGIN` below
   `USER_STACK_TOP` so delivery does not land in the canary / overrun page.
4. `sa_flags` not implemented (always 0).
5. `act->sa_mask` overwrites entire process mask on sigaction.

## 9. Debugging tips

- musl may expect `SA_RESTORER` — not implemented (T1 gap).
- Signals during blocked syscalls: partial via `irq_frame_saved` / syscall frame capture.
- Timer preemption disabled — delivery tied to explicit schedule points.

## 10. Future roadmap

- `SA_RESTART`, `SA_SIGINFO`, `siginfo_t` — not implemented.
- Process groups, `kill(0)`, permission checks on send.
- Reset signal state on exec (POSIX).
- `pause`, `sigsuspend`, `signalfd`.
- Real-time queued signals.

See: `IR0-process`, `IR0-interrupts`, `IR0-scheduler`.
