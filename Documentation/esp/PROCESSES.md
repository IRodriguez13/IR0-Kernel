# Modelo de Procesos en IR0

> **Última verificación:** 2026-09-02
> **Fuente de verdad:** `kernel/process/exit.c`, `kernel/process/wait.c`,
> [`PROCESSES.md`](../PROCESSES.md) (canónico en inglés), [`uaccess.md`](uaccess.md)

El manejo de procesos en IR0 prioriza ciclo de vida claro y semántica Unix de
credenciales en forma incremental.

## Áreas core

- Ciclo de vida bajo `kernel/process/` (`exit.c`, `wait.c`, fork/create, …).
- Integración con syscalls en `kernel/syscalls/process_syscalls.c`.
- Handoff al scheduler vía scheduler API.
- Rutas de señales y wait/reap integradas al estado de proceso.
- Frames/`siginfo` vía `copy_to_user_region_in_directory` (nunca CR3+`memcpy`);
  contrato: [`uaccess.md`](uaccess.md).

## Datos clave por proceso

- PID/PPID y enlaces de lista de procesos.
- Metadatos de contexto/tarea y address-space.
- Tabla de file descriptors y directorio de trabajo.
- Credenciales: `uid/gid/euid/egid` y `umask`.
- Estado de señales pendientes y metadata de salida.
- **`saved_context`:** copia kernel del contexto CPU para `rt_sigreturn`; acceso solo vía `process_saved_context_*()` (`kernel/process/saved_context.c`).

## Exit, reparent, wait

En `process_exit()`:

1. Reap de zombies propios.
2. **`process_reparent_children`** → hijos con `ppid` del moribundo pasan a
   **`ppid = 1`** (init).
3. El moribundo queda zombie hasta `wait` del padre (o de init).
4. Si no hay init (o muere el propio PID 1): huérfanos con **`ppid = 0`**.

ISD no reparenta en userspace; depende del kernel.

## Semántica actual de credenciales

- Los checks de permisos usan credenciales efectivas.
- Superficie de syscalls de identidad:
  - `getuid/geteuid/getgid/getegid`
  - `setuid/setgid`
  - `umask`
- Existe un modelo mínimo de usuarios para separación root/user.

## Puntos fuertes

- Ciclo de vida explícito con wait/reap bien definido.
- Reparent de huérfanos a init al estilo Unix clásico.
- Las credenciales ya participan en decisiones de política reales.

## Puntos débiles

- El modelo completo de cuentas/sesión sigue siendo liviano.
- Algunos casos borde de fork/exec/credenciales aún maduran.
- El modelo de threads no es foco principal por ahora.
- PID 1 debe hacer `wait` de zombies reparentados.
