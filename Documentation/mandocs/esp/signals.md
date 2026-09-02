# Señales de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T1 |
| Estado | stable |
| Depende de | process, scheduler, interrupts |
| Página man | IR0-signals (sección 7) |
| Fuentes principales | `includes/ir0/signals.c`, `includes/ir0/signals.h`, `kernel/syscalls.c`, `sched/rr_sched.c`, `interrupt/arch/isr_handlers.c` |

## 1. Visión general

IR0 implementa un subconjunto de señales POSIX: bitmask pending, handlers, mask,
`kill`, `rt_sigaction`, `rt_sigprocmask` y `rt_sigreturn`. La entrega corre desde
`handle_signals()` durante el cambio de contexto. Muchas acciones por defecto
terminan el proceso sin invocar handlers de usuario; la semántica POSIX completa
no está terminada.

## 2. Arquitectura interna

**Campos `process_t`:**

```text
  signal_pending (bitmask uint32_t)
  signal_handlers[_NSIG]  (_NSIG = 32)
  signal_mask, signal_ignored
  saved_context (para sigreturn)
```

| Función | Rol |
|---------|-----|
| `send_signal` | Pone bit pending en proceso destino |
| `handle_signals` | Entrega en cambio de contexto |
| `register_signal_handler` | Instala handler; rechaza SIGKILL/SIGSTOP |
| Syscalls | `sys_kill`, `sys_sigaction`, `sys_rt_sigprocmask`, `sys_sigreturn` |

## 3. Flujo de datos

```text
  sys_kill / excepción ISR / padre SIGCHLD al exit
       → send_signal(target, sig)
       → signal_pending |= mask

  sched_schedule_next (tarea RUNNING saliente)
       → handle_signals()
            ├─ SIGKILL → process_exit
            ├─ SIGSTOP → PROCESS_BLOCKED
            ├─ SIGSEGV/FPE/ILL/BUS → process_exit (sin handler usuario)
            ├─ SIGTERM/INT/QUIT/ABRT → process_exit
            └─ handler custom → construye sigframe en stack usuario, redirige RIP/RSP
```

Mapa ASCII:

```text
  origen ──► máscara pending ──► handle_signals (en switch)
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
                 exit            block          handler usuario
                                                    │
                                                    ▼
                                              rt_sigreturn
```

## 4. Responsabilidades

- ISR mapea excepciones CPU a señales para frames usuario (`isr_handlers.c`).
- Path page fault puede `send_signal(SIGSEGV)` luego **síncrono** `process_exit` en `fault.c`.
- Spawn resetea handlers a `SIG_DFL`; fork copia estado señal vía `memcpy`.

## 5. Límites del subsistema

- `_NSIG = 32`; API `sigset_t` es 64-bit — deuda de desajuste de tamaño.
- `sys_sigaction` no es `rt_sigaction` Linux completo (sin arg sigsetsize).
- Exec **no** resetea estado de señales (no-POSIX).

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Scheduler | `handle_signals` antes de cambio de contexto |
| Process | exit envía SIGCHLD al padre |
| Syscalls | sigreturn restaura `task_t` desde frame guardado |
| Interrupts | mapeo excepción → señal |

## 7. Mapas visuales

```text
  pending:  [ bit sig ... ]
  mask:     bloquea entrega en bucle handler
  ignored:  SIG_IGN omite handler

  orden de entrega (hard-coded):
    KILL > STOP > fallos fatales > grupo term > CONT > CHLD > handlers custom
```

## 8. Invariantes importantes

1. SIGKILL/SIGSTOP no pueden capturarse ni ignorarse.
2. `sys_kill`: solo `pid > 0`; sin grupos de proceso ni `kill(-1)`.
3. Handler usuario necesita stack usuario válido en [0x400000, 0x7FFFFFFFFFFF], alineado 16 bytes.
   Los frames se colocan con `signal_pick_handler_sp()` (también desde
   `signals_deliver_from_irq_frame`) y `SIGNAL_HANDLER_TOP_MARGIN` bajo
   `USER_STACK_TOP` para no pisar la banda del canary / overrun.
4. `sa_flags` no implementado (siempre 0).
5. `act->sa_mask` sobrescribe máscara completa del proceso en sigaction.

## 9. Consejos de depuración

- musl puede esperar `SA_RESTORER` — no implementado (gap T1).
- Señales durante syscalls bloqueadas: parcial vía `irq_frame_saved` / captura frame syscall.
- Preempt timer deshabilitado — entrega ligada a puntos schedule explícitos.

## 10. Roadmap futuro

- `SA_RESTART`, `SA_SIGINFO`, `siginfo_t` — no implementado.
- Grupos de proceso, `kill(0)`, comprobaciones de permiso al enviar.
- Reset estado señales en exec (POSIX).
- `pause`, `sigsuspend`, `signalfd`.
- Señales en tiempo real encoladas.

Ver: `IR0-process`, `IR0-interrupts`, `IR0-scheduler`.
