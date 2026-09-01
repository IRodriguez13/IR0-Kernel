; Linux x86-64 syscall instruction entry (MSR LSTAR)
;
; ABI contract (Linux x86-64 syscall):
;   IN : rax = syscall_nr
;        rdi, rsi, rdx, r10, r8, r9 = args 0..5
;        rcx = user RIP   (set by syscall insn)
;        r11 = user RFLAGS (set by syscall insn)
;   OUT: rax = return value
;        rcx, r11 = clobbered (sysret restores RIP/RFLAGS from them)
;        ALL OTHER GPR (rbx, rbp, rdi, rsi, rdx, r10, r8, r9, r12-r15)
;        must be preserved across the syscall.
;
; Stack discipline (FASE 24):
;   The syscall instruction does NOT switch RSP automatically (unlike int 0x80,
;   which uses TSS.RSP0). Linux/BSD use SWAPGS + per-CPU; IR0 single-CPU uses a
;   global dedicated syscall kernel stack to avoid running the kernel on the
;   user stack (which can cross unmapped user pages and panic).
;
; Violating ABI corrupts user computation across any syscall whose caller keeps
; a live value in those registers (e.g. musl _Fork keeps the TLS pointer in
; %rdx across `syscall gettid`).
[BITS 64]

global syscall_insn_entry_asm
extern syscall_dispatch
extern process_capture_syscall_frame_at_entry
extern fase24_log_stack_once
extern restore_user_fs_base

section .bss
align 16
kernel_syscall_stack_base:
    resb 8192                          ; 8 KiB dedicated syscall kernel stack
kernel_syscall_stack_end:

global user_rsp_save
global fase24_user_rsp_snap
global fase24_kernel_rsp_snap
global fase24_rsp_pre_sysret
global fase25_user_rsp_saved
global fase25_rsp_before_restore
global fase25_rsp_after_restore
global fase25_rcx_before_sysret
global fase25_r11_before_sysret
global fase27_rax_before_sysret
global fase27_rsp_before_sysret
global fase27_rcx_before_sysret
global fase27_r11_before_sysret
global fase27_rdx_before_sysret
global fase27_rsi_before_sysret
global fase27_rdi_before_sysret
global fase27_r8_before_sysret
global fase27_r9_before_sysret
global fase27_r10_before_sysret
global fase29_entry_rip
global fase29_entry_rsp
global fase30_entry_user_rsp
user_rsp_save:           resq 1
fase24_user_rsp_snap:    resq 1
fase24_kernel_rsp_snap:  resq 1
fase24_rsp_pre_sysret:   resq 1
fase25_user_rsp_saved:   resq 1
fase25_rsp_before_restore: resq 1
fase25_rsp_after_restore:  resq 1
fase25_rcx_before_sysret:  resq 1
fase25_r11_before_sysret:  resq 1
fase27_rax_before_sysret:  resq 1
fase27_rsp_before_sysret:  resq 1
fase27_rcx_before_sysret:  resq 1
fase27_r11_before_sysret:  resq 1
fase27_rdx_before_sysret:  resq 1
fase27_rsi_before_sysret:  resq 1
fase27_rdi_before_sysret:  resq 1
fase27_r8_before_sysret:   resq 1
fase27_r9_before_sysret:   resq 1
fase27_r10_before_sysret:  resq 1
fase29_entry_rip:          resq 1
fase29_entry_rsp:          resq 1
fase30_entry_user_rsp:     resq 1

section .data
global kernel_syscall_stack_top
kernel_syscall_stack_top:
    dq kernel_syscall_stack_end        ; top of the syscall kernel stack

section .text

syscall_insn_entry_asm:
    ; ---- FASE 24: stack switch ------------------------------------------
    ; Save user RSP and load the dedicated syscall kernel stack BEFORE
    ; touching memory off RSP. Single-CPU, no SWAPGS, no per-CPU.
    mov [rel fase30_entry_user_rsp], rsp
    mov [rel user_rsp_save], rsp
    mov rsp, [rel kernel_syscall_stack_top]

    ; ---- FASE 24: snapshot for the one-shot log (overwritten on every
    ; syscall; the C helper only emits the first time it's called). ------
    mov [rel fase24_kernel_rsp_snap], rsp
    push rax
    mov rax, [rel user_rsp_save]
    mov [rel fase25_user_rsp_saved], rax
    pop rax

    ; ---- Stage 1: snapshot callee-saved + RIP/RFLAGS first ----
    ;
    ; Layout consumed by process_capture_syscall_frame_at_entry (frame_base ptr):
    ;   [+0]=rbx [+8]=rbp [+16]=r12 [+24]=r13 [+32]=r14 [+40]=r15
    ;   [+48]=rflags [+56]=rip [+64]=user_rsp
    push qword [rel user_rsp_save] ; user RSP (consumed by process_capture)
    push rcx           ; user RIP
    push r11           ; user RFLAGS
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    mov rbx, [rel user_rsp_save]
    mov [rel fase24_user_rsp_snap], rbx

    ; ---- Stage 2: PRESERVE Linux-ABI args BEFORE any C call ----
    ;
    ; A System V C call (process_capture / syscall_dispatch) is free to clobber
    ; rdi, rsi, rdx, rcx, r8, r9, r10, r11. We must save them BEFORE such a
    ; call so we can restore them at sysret per the Linux syscall ABI.
    push rdi           ; user arg0
    push rsi           ; user arg1
    push rdx           ; user arg2
    push r10           ; user arg3
    push r8            ; user arg4
    push r9            ; user arg5
    push rax           ; syscall nr (also volatile across C call)

    ; Stack layout from current rsp:
    ;   [+0]=rax(nr)
    ;   [+8]=r9   [+16]=r8   [+24]=r10  [+32]=rdx  [+40]=rsi  [+48]=rdi
    ;   [+56]=rbx [+64]=rbp  [+72]=r12  [+80]=r13  [+88]=r14  [+96]=r15
    ;   [+104]=rflags [+112]=rip [+120]=user_rsp

    ; ---- Stage 3: capture user frame (rdi = &rbx in stack) ----
    mov qword [rel fase29_entry_rip], 0
    lea rdi, [rsp + 56]
    call process_capture_syscall_frame_at_entry

    ; ---- Stage 4: switch DS/ES to kernel data segments ----
    push rax           ; preserve nr-shadow across mov ax,0x10
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    pop rax

    ; ---- Stage 5: marshal args from saved stack to System V ABI ----
    ;
    ; syscall_dispatch(uint64_t nr, a1, a2, a3, a4, a5, a6) — System V uses
    ; rdi, rsi, rdx, rcx, r8, r9 for args 1-6, and stack for arg 7+.
    pop rax            ; rax = syscall_nr
    ; After pop, stack [+0]=r9 [+8]=r8 [+16]=r10 [+24]=rdx [+32]=rsi [+40]=rdi
    mov rdi, rax       ; arg1 = nr
    mov rsi, [rsp + 40]   ; arg2 = user rdi
    mov rdx, [rsp + 32]   ; arg3 = user rsi
    mov rcx, [rsp + 24]   ; arg4 = user rdx
    mov r8,  [rsp + 16]   ; arg5 = user r10
    mov r9,  [rsp + 8]    ; arg6 = user r8
    push qword [rsp + 0]  ; arg7 (stack) = user r9
    call syscall_dispatch
    add rsp, 8            ; drop stack arg slot
    ; rax = retval; C call clobbered volatile regs (we'll restore from stack).

    ; ---- Stage 6: restore user GPR per Linux syscall ABI ----
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15
    pop r11               ; user RFLAGS (sysret loads into RFLAGS)
    pop rcx               ; user RIP    (sysret loads into RIP)
    add rsp, 8            ; drop the user RSP shadow pushed in Stage 1

    ; ---- Stage 7: restore user DS/ES (sysret doesn't reload them) ----
    ;
    ; Preserve rax (syscall retval) across the scratch ax load; otherwise
    ; mov ax,0x23 clobbers the low16 of the user-visible return value.
    push rax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    pop rax

    ; ---- FASE 24: one-shot stack switch log ------------------------------
    ; Snapshot kernel RSP just before swap-back; this should equal
    ; fase24_kernel_rsp_snap if every push/pop balanced. The C helper
    ; emits to serial only the first time it's called.
    mov [rel fase24_rsp_pre_sysret], rsp
    mov [rel fase25_rsp_before_restore], rsp
    mov [rel fase25_rcx_before_sysret], rcx
    mov [rel fase25_r11_before_sysret], r11
    mov [rel fase27_rax_before_sysret], rax
    mov [rel fase27_rcx_before_sysret], rcx
    mov [rel fase27_r11_before_sysret], r11
    mov [rel fase27_rdx_before_sysret], rdx
    mov [rel fase27_rsi_before_sysret], rsi
    mov [rel fase27_rdi_before_sysret], rdi
    mov [rel fase27_r8_before_sysret], r8
    mov [rel fase27_r9_before_sysret], r9
    mov [rel fase27_r10_before_sysret], r10
    mov [rel fase29_entry_rip], rcx
    push rax
    mov rax, [rel user_rsp_save]
    mov [rel fase25_rsp_after_restore], rax
    mov [rel fase27_rsp_before_sysret], rax
    mov [rel fase29_entry_rsp], rax
    pop rax
    ;
    ; C helpers below clobber SysV volatiles. Linux syscall ABI must preserve
    ; all GPR except rax (retval), rcx, and r11 — musl _Fork keeps the TLS
    ; pointer in %rdx across gettid after fork returns 0. Save the full set.
    ;
    push rax
    push rcx
    push r11
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    sub rsp, 8                  ; 10 pushes = 80 bytes; keep 16-byte align
    call fase24_log_stack_once
    call restore_user_fs_base
    add rsp, 8
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop r11
    pop rcx
    pop rax

    ; ---- FASE 24: restore user RSP before sysret -------------------------
    mov rsp, [rel user_rsp_save]

    ; Force IF in R11. Shared syscall stack / cli save can leave IF=0 and
    ; sysret would return ash with IRQs masked (dead keyboard after login).
    or r11, 0x200

    o64 sysret

section .note.GNU-stack noalloc noexec nowrite progbits
