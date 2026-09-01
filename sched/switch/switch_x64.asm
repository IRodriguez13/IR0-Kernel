global switch_context_x64
global iretq_checkpoint_buf
extern fork_ret_emit_pre_return
extern fork_ret_pre_regs
extern fork_flow_set_tf
extern fork_restore_audit
extern _etext
extern arch_report_bad_kernel_ret_rip
extern process_after_task_save

; Single source: includes/ir0/asm_offsets.h ↔ asm_offsets.inc
%include "includes/ir0/asm_offsets.inc"

section .bss
align 16
iretq_checkpoint_buf:
    resq 40
fork_flow_set_tf:
    resb 1

; fork_restore_audit_t offsets (must match kernel/process.c)
%define FRA_magic                  0
%define FRA_task_ptr               8
%define FRA_rax_slot_addr          16
%define FRA_rax_slot_mem           24
%define FRA_rsp_pre_gpr_load       32
%define FRA_stack_qwords           40
%define FRA_restore_method         200
%define FRA_stack_rax_slot_off     208
%define FRA_live_rax_after_load    216
%define FRA_live_rbx_after_load    224
%define FRA_live_rax_after_pr_call 232
%define FRA_live_rax_pre_iretq     240
%define FRA_live_rbx_pre_iretq     248
%define FRA_live_rcx_pre_iretq     256
%define FRA_live_rdx_pre_iretq     264
%define FRA_kernel_rsp_pre_iretq   272
%define FRA_iretq_rip              280
%define FRA_iretq_rflags           288
%define FRA_iretq_user_rsp         296

%macro FORK_RESTORE_DUMP_STACK 0
    push rsi
    push r10
    lea rsi, [rel fork_restore_audit]
    xor r10, r10
%%fr_dump_loop:
    cmp r10, 20
    jge %%fr_dump_done
    mov rax, [rsp + 16 + r10 * 8]
    mov [rsi + FRA_stack_qwords + r10 * 8], rax
    inc r10
    jmp %%fr_dump_loop
%%fr_dump_done:
    pop r10
    pop rsi
%endmacro

%macro FORK_RESTORE_SNAP_PRE_IRETQ 0
    push rsi
    push r10
    lea rsi, [rel fork_restore_audit]
    mov [rsi + FRA_live_rax_pre_iretq], rax
    mov [rsi + FRA_live_rbx_pre_iretq], rbx
    mov [rsi + FRA_live_rcx_pre_iretq], rcx
    mov [rsi + FRA_live_rdx_pre_iretq], rdx
    mov [rsi + FRA_kernel_rsp_pre_iretq], rsp
    mov r10, [rsp + 16]
    mov [rsi + FRA_iretq_rip], r10
    mov r10, [rsp + 32]
    mov [rsi + FRA_iretq_rflags], r10
    mov r10, [rsp + 40]
    mov [rsi + FRA_iretq_user_rsp], r10
    pop r10
    pop rsi
%endmacro

; Reload user GPRs from task_t after debug epilogue (must not clobber rax et al.).
%macro FORK_RESTORE_RELOAD_GPRS 0
    mov r10, [rel fork_restore_audit + FRA_task_ptr]
    test r10, r10
    jz %%fr_reload_done
    mov rax, [r10 + 0x00]
    mov rbx, [r10 + 0x08]
    mov rcx, [r10 + 0x10]
    mov rdx, [r10 + 0x18]
    mov rbp, [r10 + 0x78]
    mov r8,  [r10 + 0x30]
    mov r9,  [r10 + 0x38]
    mov r12, [r10 + 0x50]
    mov r13, [r10 + 0x58]
    mov r14, [r10 + 0x60]
    mov r15, [r10 + 0x68]
    push qword [r10 + 0x48]
    push qword [r10 + 0x40]
    push qword [r10 + 0x28]
    push qword [r10 + 0x20]
    pop rsi
    pop rdi
    pop r10
    pop r11
%%fr_reload_done:
%endmacro

section .text

;
; switch_context_x64(task_t *current, task_t *new)
;
; Save all register state into *current, load from *new, iretq to new task.
; On null pointers, returns to caller without switching.
;
; task_t offsets (from kernel/scheduler/task.h):
;   0x00 rax, 0x08 rbx, 0x10 rcx, 0x18 rdx, 0x20 rsi, 0x28 rdi,
;   0x30 r8,  0x38 r9,  0x40 r10, 0x48 r11, 0x50 r12, 0x58 r13,
;   0x60 r14, 0x68 r15, 0x70 rsp, 0x78 rbp, 0x80 rip, 0x88 rflags,
;   0x90 cs,  0x92 ds,  0x94 es,  0x96 fs,  0x98 gs,  0x9A ss,
;   0xB0 cr3
;
switch_context_x64:
    test rsi, rsi
    jz .skip

    ; rdi=NULL: user IRQ frame already in prev->task (skip clobbering save).
    test rdi, rdi
    jz .load_only

    ; ---- Save current context (rdi = &current->task) ----

    mov [rdi + 0x00], rax
    mov [rdi + 0x08], rbx
    mov [rdi + 0x10], rcx
    mov [rdi + 0x18], rdx
    mov [rdi + 0x20], rsi
    mov [rdi + 0x28], rdi
    mov [rdi + 0x30], r8
    mov [rdi + 0x38], r9
    mov [rdi + 0x40], r10
    mov [rdi + 0x48], r11
    mov [rdi + 0x50], r12
    mov [rdi + 0x58], r13
    mov [rdi + 0x60], r14
    mov [rdi + 0x68], r15
    mov [rdi + 0x78], rbp

    ; RSP: undo the `call` return-address push to get the caller's RSP
    lea rax, [rsp + 8]
    mov [rdi + 0x70], rax

    ; RIP: return address pushed by `call` — must be in kernel .text
    mov rax, [rsp]
    cmp rax, 0x101000
    jb .bad_save_rip
    lea r8, [rel _etext]
    cmp rax, r8
    jae .bad_save_rip
    mov [rdi + TASK_ARCH_RIP_OFFSET], rax
    jmp .save_rflags_ok

.bad_save_rip:
    ; [rsp] was not .text (seen: process_t* → later kernel_ret #UD)
    push rdi
    push rsi
    mov rsi, rdi
    mov rdi, rax
    call arch_report_bad_kernel_ret_rip
    pop rsi
    pop rdi
    mov rax, [rsp]
    mov [rdi + TASK_ARCH_RIP_OFFSET], rax

.save_rflags_ok:
    pushfq
    pop qword [rdi + 0x88]

    mov ax, cs
    mov [rdi + 0x90], ax
    mov ax, ds
    mov [rdi + 0x92], ax
    mov ax, es
    mov [rdi + 0x94], ax
    mov ax, fs
    mov [rdi + 0x96], ax
    mov ax, gs
    mov [rdi + 0x98], ax
    mov ax, ss
    mov [rdi + TASK_ARCH_SS_OFFSET], ax

    mov rax, cr3
    mov [rdi + TASK_ARCH_CR3_OFFSET], rax

    ; Class B close: honour want_kernel_ret now that rip/cs are kernel from save.
    push rsi
    call process_after_task_save
    pop rsi

.load_only:
    ; ---- Load new context (rsi = &new->task) ----

    mov r11, rsi

    ; CR3 before GPR restore: rax is scratch until task->rax is loaded below.
    mov rax, [r11 + TASK_ARCH_CR3_OFFSET]
    mov cr3, rax

    movzx eax, word [r11 + 0x90]
    test al, 3
    jnz .user_iretq_resume

    ; Ring-0 resume (blocked in kernel syscall path): ret to saved RIP/RSP.
.kernel_ret_resume:
    mov rax, [r11 + 0x00]
    mov rbx, [r11 + 0x08]
    mov rcx, [r11 + 0x10]
    mov rdx, [r11 + 0x18]
    mov rsi, [r11 + 0x20]
    mov rdi, [r11 + 0x28]
    mov rbp, [r11 + 0x78]
    mov r8,  [r11 + 0x30]
    mov r9,  [r11 + 0x38]
    mov r10, [r11 + 0x40]
    mov r12, [r11 + 0x50]
    mov r13, [r11 + 0x58]
    mov r14, [r11 + 0x60]
    mov r15, [r11 + 0x68]

    push rax
    push rcx
    push rdx
    mov rax, [r11 + PROC_FS_BASE_OFFSET]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr
    pop rdx
    pop rcx
    pop rax

    mov r10, [r11 + TASK_ARCH_RIP_OFFSET]
    cmp r10, 0x101000
    jb .bad_ret_rip
    lea r8, [rel _etext]
    cmp r10, r8
    jae .bad_ret_rip
    mov r8, [r11 + 0x48]
    mov rsp, [r11 + 0x70]
    mov r11, r8
    jmp r10

.bad_ret_rip:
    mov rdi, r10
    mov rsi, r11
    call arch_report_bad_kernel_ret_rip
    ; fall through unreachable (report panics)

.user_iretq_resume:
    ; Refuse non-canonical user RIP/RSP (heap #UD class if CS flipped alone).
    mov rax, [r11 + TASK_ARCH_RIP_OFFSET]
    cmp rax, 0x00400000
    jb .bad_user_iret_frame
    mov rbx, 0x00007FFFFFFFFFFF
    cmp rax, rbx
    ja .bad_user_iret_frame
    mov rax, [r11 + 0x70]
    cmp rax, 0x00400000
    jb .bad_user_iret_frame
    cmp rax, rbx
    ja .bad_user_iret_frame

    mov rax, [r11 + 0x00]
    mov rbx, [r11 + 0x08]
    mov rcx, [r11 + 0x10]
    mov rdx, [r11 + 0x18]
    mov rsi, [r11 + 0x20]
    mov rbp, [r11 + 0x78]
    mov r8,  [r11 + 0x30]
    mov r9,  [r11 + 0x38]
    mov r10, [r11 + 0x40]
    mov r12, [r11 + 0x50]
    mov r13, [r11 + 0x58]
    mov r14, [r11 + 0x60]
    mov r15, [r11 + 0x68]

    ; FASE 18: restore IA32_FS_BASE from process_t->fs_base (offset 0x140).
    ; task is at offset 0 of process_t, so r11 (=&task) doubles as process_t*.
    ; Guarded by _Static_assert in kernel/process.h.
    push rax
    push rcx
    push rdx
    mov rax, [r11 + PROC_FS_BASE_OFFSET]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr
    pop rdx
    pop rcx
    pop rax

    ; Build iretq frame on target CR3; segments reload on iretq (SS) / user entry.
    movzx rdi, word [r11 + TASK_ARCH_SS_OFFSET]
    push rdi
    push qword [r11 + 0x70]
    ; Force IF — task.rflags may have been saved under cli (IF=0).
    mov rdi, [r11 + 0x88]
    or rdi, 0x200
    push rdi
    movzx rdi, word [r11 + 0x90]
    push rdi
    push qword [r11 + TASK_ARCH_RIP_OFFSET]

    mov rdi, [r11 + 0x28]
    mov r11, [r11 + 0x48]

    iretq

.bad_user_iret_frame:
    mov rdi, [r11 + TASK_ARCH_RIP_OFFSET]
    mov rsi, r11
    call arch_report_bad_kernel_ret_rip

.skip:
    ret

;
; switch_to_user_task_asm(const task_t *task)
; Linux ret_from_fork-style entry: load CR3 + GPRs, iretq (no ring-0 MOV SS).
;
global switch_to_user_task_asm
switch_to_user_task_asm:
    test rdi, rdi
    jz .aus_ret

    mov r11, rdi

    ; --- CHECKPOINT A: snapshot intended iretq frame from task_t ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 0], 0xAAA1
    mov rcx, [r11 + TASK_ARCH_CR3_OFFSET]
    mov [rax + 8], rcx
    mov rcx, [r11 + TASK_ARCH_RIP_OFFSET]
    mov [rax + 16], rcx
    movzx rcx, word [r11 + 0x90]
    mov [rax + 24], rcx
    mov rcx, [r11 + 0x88]
    or rcx, 2
    and rcx, 0xFFFFFFFFFFFFFEFF
    mov [rax + 32], rcx
    mov rcx, [r11 + 0x70]
    mov [rax + 40], rcx
    movzx rcx, word [r11 + TASK_ARCH_SS_OFFSET]
    mov [rax + 48], rcx
    mov [rax + 56], r11
    pop rcx
    pop rax
    ; -------------------------

    sub rsp, 8
    mov rax, [r11 + TASK_ARCH_CR3_OFFSET]
    mov [rsp], rax
    mov cr3, rax

    ; [FORK_RESTORE] snapshot before GPR load from task_t (not stack pop).
    push rsi
    push r10
    lea rsi, [rel fork_restore_audit]
    mov eax, 0xF010CAFE
    mov [rsi + FRA_magic], rax
    mov [rsi + FRA_task_ptr], r11
    lea r10, [r11 + 0x00]
    mov [rsi + FRA_rax_slot_addr], r10
    mov r10, [r11 + 0x00]
    mov [rsi + FRA_rax_slot_mem], r10
    mov [rsi + FRA_rsp_pre_gpr_load], rsp
    mov qword [rsi + FRA_restore_method], 1
    mov qword [rsi + FRA_stack_rax_slot_off], 0xFFFFFFFFFFFFFFFF
    FORK_RESTORE_DUMP_STACK
    pop r10
    pop rsi

    mov rax, [r11 + 0x00]
    mov rbx, [r11 + 0x08]
    mov rcx, [r11 + 0x10]
    mov rdx, [r11 + 0x18]
    mov rsi, [r11 + 0x20]
    mov rbp, [r11 + 0x78]
    mov r8,  [r11 + 0x30]
    mov r9,  [r11 + 0x38]
    mov r10, [r11 + 0x40]
    mov r12, [r11 + 0x50]
    mov r13, [r11 + 0x58]
    mov r14, [r11 + 0x60]
    mov r15, [r11 + 0x68]

    ; [FORK_RESTORE] live GPRs after task_t load.
    push rsi
    lea rsi, [rel fork_restore_audit]
    mov [rsi + FRA_live_rax_after_load], rax
    mov [rsi + FRA_live_rbx_after_load], rbx
    pop rsi

    ; --- FASE 9A: capture rax around DS/ES load (no new regs, no CR3 touch) ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 256], 0xEEE1
    mov rcx, [r11 + 0x00]
    mov [rax + 264], rcx              ; ckpt_task_rax = task->rax
    mov rcx, [rsp + 8]                ; rax saved on stack (pre DS/ES load)
    mov [rax + 272], rcx              ; ckpt_rax_before_ds
    pop rcx
    pop rax
    ; -------------------------

    mov dx, 0x23
    mov ds, dx
    mov es, dx

    ; --- FASE 9A POST: capture rax AFTER segment load (now via dx) ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov rcx, [rsp + 8]                ; rax saved on stack (post DS/ES load)
    mov [rax + 280], rcx              ; ckpt_rax_after_ds
    pop rcx
    pop rax
    ; -------------------------

    ; FASE 18: restore IA32_FS_BASE from process_t->fs_base (offset 0x140).
    ; task is at offset 0 of process_t, so r11 doubles as process_t*.
    ; Guarded by _Static_assert in kernel/process.h.
    push rax
    push rcx
    push rdx
    mov rax, [r11 + PROC_FS_BASE_OFFSET]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr
    pop rdx
    pop rcx
    pop rax

    movzx rdi, word [r11 + TASK_ARCH_SS_OFFSET]
    push rdi
    push qword [r11 + 0x70]
    ; Force IF (0x200) + bit1; clear TF. Saved rflags may be IF=0 under cli.
    mov rdi, [r11 + 0x88]
    or rdi, 0x202
    and rdi, 0xFFFFFFFFFFFFFEFF
    push rdi
    movzx rdi, word [r11 + 0x90]
    push rdi
    push qword [r11 + TASK_ARCH_RIP_OFFSET]

    mov rdi, [r11 + 0x28]
    mov r11, [r11 + 0x48]

    ; [FORK_RET] live GPR dump after task restore, before iretq.
    push r11
    push rdi
    lea rdi, [rel fork_ret_pre_regs]
    mov [rdi + 0], rax
    mov [rdi + 8], rcx
    mov rax, [rsp + 8]
    mov [rdi + 16], rax
    mov [rdi + 24], rbx
    mov [rdi + 32], rbp
    mov [rdi + 40], r12
    mov [rdi + 48], r13
    mov [rdi + 56], r14
    mov [rdi + 64], r15
    mov rax, [rsp + 16 + 24]
    mov [rdi + 72], rax
    mov rax, [rsp + 16 + 0]
    mov [rdi + 80], rax
    pop rdi
    pop r11

    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    push r11
    push rcx
    push rax
    call fork_ret_emit_pre_return
    pop rax
    pop rcx
    pop r11
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    ; [FORK_RESTORE] live RAX after fork_ret_emit_pre_return call pops.
    push rsi
    lea rsi, [rel fork_restore_audit]
    mov [rsi + FRA_live_rax_after_pr_call], rax
    pop rsi

    ; Reload user GPRs from task (epilogue must not leave scratch in rax et al.).
    FORK_RESTORE_RELOAD_GPRS

    ; One-shot TF for fork-flow observation (cleared in #DB handler).
    cmp byte [rel fork_flow_set_tf], 0
    je .fork_flow_no_tf
    or qword [rsp + 16], 0x100
.fork_flow_no_tf:

    FORK_RESTORE_SNAP_PRE_IRETQ

    iretq

.aus_ret:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
