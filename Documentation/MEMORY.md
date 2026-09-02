# IR0 Memory Subsystem

> **Last verified:** 2026-09-02  
> **Source of truth:** `mm/pmm.c`, `mm/paging.c`, `mm/page_fault.c`,  
> `arch/x86-64/sources/arch_pf_debug.c`, `Documentation/mandocs/en/mm.md`,  
> `Documentation/uaccess.md`, `make smoke-mm-cow-lazy`

IR0 memory management currently combines PMM, kernel allocator, and paging-based
process isolation.

## Core Layers

- `mm/pmm.c`: physical page frame tracking, allocation, and per-frame refcount
  (`pmm_frame_get` / `pmm_frame_put`; `pmm_free_frame` drops one reference).
- `mm/allocator.c` plus `includes/ir0/kmem.h`: kernel dynamic allocator.
- `mm/paging.c`: virtual mapping, page-table setup, fork share-on-fork (`PAGE_COW`).
- `mm/page_fault.c`: portable demand-zero, anon mmap, write-fault COW break, SIGSEGV policy.
- `arch/x86-64/sources/arch_pf_debug.c`: optional `DEBUG_D1_DIAG` register-frame forensics (`pf_debug_stack_adjacent`, `pf_debug_memmove_fault`); arm64 stubs in `arch/arm64/sources/arch_pf_debug.c`.

## Operational Model

- Frame allocator provides physical pages for kernel and mapping paths.
- Kernel allocator serves most dynamic structures across subsystems.
- Paging provides address-space boundaries and execution context transitions.
- Fork shares present user PFNs; formerly writable pages are mapped read-only with
  software `PAGE_COW` (PTE bit 9) until the first write fault.

## Page-fault policy notes (2026-09-01)

- **Soft-grow removed:** heap/stack VMAs are no longer extended implicitly on a fault one page past `heap_end` or stack top. Out-of-VMA faults deliver SIGSEGV (Linux expects explicit `brk`/`mmap` growth).
- **COW break:** write faults on `PAGE_COW` always copy to a new frame (`pmm_frame_get` pin when refcount is non-zero); shared frames are never promoted RW in place. Unchanged from prior FASE40 path; gate: `make smoke-mm-cow-lazy`.

## Stack top and signal frames (2026-09-02)

- Faults in `[USER_STACK_TOP, USER_STACK_TOP+4K)` classify as `STACK_TOP_OVERRUN`
  (see [`KTM.md`](KTM.md) §7.3). **Do not** reintroduce soft-grow past TOP; do not
  treat enlarging `USER_STACK_SIZE` as the primary fix for `addr=7ffff000`.
- User signal delivery (`handle_signals` and `signals_deliver_from_irq_frame`)
  places frames via `signal_pick_handler_sp()` with `SIGNAL_HANDLER_TOP_MARGIN`
  (`includes/ir0/abi/signal_contract.h`) so frames stay out of the canary band.
- Gate: `make smoke-pipeline-stress` (0× `STACK_TOP_OVERRUN` on that path);
  session chaos treats the tag as fatal (`make smoke-session-chaos`).

## Process Integration

- Process creation wires memory structures with per-process context.
- Scheduler/context switch path relies on paging state transition.
- User access validation and copy helpers enforce boundary checks.
  **Canonical contract:** [`uaccess.md`](uaccess.md) — never raw
  `memcpy`/`memset` into user VAs; always `copy_*_user` or
  `*_region_in_directory` (COW break). Kernel `#PF` on user VA with `cs=8`
  classifies as `KERNEL_UACCESS_FAULT` and is fatal in session soak.

## Strengths

- Clean separation of physical, heap, and virtual memory concerns.
- Real fork COW + lazy anon mmap/brk proven by `make smoke-mm-cow-lazy` (FASE40 A–F).
- Instrumentation available via proc endpoints and runtime logs.

## Weak Points

- No huge-page COW, no file-backed COW, no swap-class reclaim.
- Performance tuning for large workloads is not yet the main target.
- Some memory policies still prioritize simplicity over full POSIX-like depth.
