# Subsistema de Memoria en IR0

> **Última verificación:** 2026-09-02  
> **Fuente de verdad:** [`../MEMORY.md`](../MEMORY.md), [`uaccess.md`](uaccess.md)

La memoria en IR0 combina PMM, allocator del kernel y paginacion para
aislamiento de procesos.

## Cambios recientes (2026-09-02)

- Frontera uaccess documentada: [`uaccess.md`](uaccess.md) — toda copia a VA
  user pasa por `copy_*_user` / region helpers (COW-safe); fallos kernel en VA
  user clasifican como `KERNEL_UACCESS_FAULT`.
- Stack: sin soft-grow past `USER_STACK_TOP`; frames de señal vía
  `signal_pick_handler_sp` + margen TOP (detalle en [`../MEMORY.md`](../MEMORY.md)).

## Cambios recientes (2026-09-01)

- Eliminado soft-grow implicito heap/stack en `#PF` (fuera de VMA → SIGSEGV).
- Forensics x86 bajo `DEBUG_D1_DIAG` en `arch/x86-64/sources/arch_pf_debug.c`.
- COW sin cambio de contrato; gate: `make smoke-mm-cow-lazy`.

## Capas Principales

- `mm/pmm.c`: tracking y asignacion de frames fisicos.
- `mm/allocator.c` y `includes/ir0/kmem.h`: asignador dinamico del kernel.
- `mm/paging.c`: mapeo virtual y setup de tablas de paginas.

## Modelo Operativo

- El PMM entrega paginas fisicas para kernel y mapeos.
- El allocator del kernel cubre la mayoria de estructuras dinamicas.
- La paginacion da fronteras de address-space y transicion de contexto.

## Integracion con Procesos

- La creacion de procesos enlaza estructuras de memoria por proceso.
- Scheduler/context-switch depende del cambio de estado de paging.
- Validacion de acceso user y helpers de copia refuerzan limites — ver
  contrato canónico [`uaccess.md`](uaccess.md).

## Puntos Fuertes

- Separacion clara de responsabilidades fisica/heap/virtual.
- Estabilidad suficiente para sostener trabajo de estabilizacion en subsistemas.
- Instrumentacion disponible via endpoints proc y logs runtime.

## Puntos Debiles

- Features VM avanzadas siguen limitadas (por ejemplo, COW/swap de nivel full).
- El tuning para cargas grandes aun no es foco principal.
- Algunas politicas priorizan simplicidad sobre profundidad POSIX completa.
