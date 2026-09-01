#!/bin/bash
# One-shot PORT rename: arch_* public APIs → simple names (headers + portable + arch impl).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Files to rewrite (exclude agent/build noise)
mapfile -t FILES < <(rg -l \
  'arch_fork_prepare_|arch_process_set_tls|arch_process_capture_syscall|arch_process_syscall_restore|arch_process_save_user_context|arch_irq_frame_is_user|arch_irq_frame_sp|arch_task_apply_syscall|arch_task_sync_syscall|arch_task_save_irq|arch_task_apply_kernel|arch_task_apply_user|arch_task_retval_slot|arch_task_load_sigcontext|arch_task_store_sigcontext|arch_task_context_clone|arch_task_set_user_segments|arch_task_set_kernel_segments|arch_task_clear_frame_pointer|arch_task_set_frame_pointer|arch_task_set_user_return|arch_task_prepare_fork_child|arch_task_context_init|arch_signal_|arch_restore_user_fs_base|arch_hypervisor_|arch_elf_machine_supported|arch_syscall_frame_ip|arch_syscall_frame_sp|arch_syscall_frame_flags|arch_syscall_frame_set_ip|arch_syscall_frame_set_sp|arch_syscall_frame_set_flags|arch_syscall_frame_arg|arch_syscall_frame_set_arg' \
  --glob '*.c' --glob '*.h' --glob '*.asm' \
  kernel mm fs net sched includes/ir0 arch tests/host interrupt 2>/dev/null || true)

run_sed() {
  local expr="$1"
  local f
  for f in "${FILES[@]}"; do
    [[ -f "$f" ]] || continue
    sed -i "$expr" "$f"
  done
}

# Fork / TLS
run_sed 's/\barch_fork_prepare_parent_return\b/fork_prepare_parent_return/g'
run_sed 's/\barch_fork_prepare_child_return\b/fork_prepare_child_return/g'
run_sed 's/\barch_process_set_tls\b/process_set_tls/g'

# Syscall frame (process scope)
run_sed 's/\barch_process_capture_syscall_frame_at_entry\b/syscall_capture_frame_at_entry/g'
run_sed 's/\barch_process_syscall_restore_exit_regs\b/syscall_restore_exit_regs/g'
run_sed 's/\barch_process_save_user_context_from_irq\b/syscall_save_user_context_from_irq/g'
run_sed 's/\barch_irq_frame_is_user\b/irq_frame_is_user/g'
run_sed 's/\barch_irq_frame_sp\b/irq_frame_sp/g'

# Task ops (backend)
run_sed 's/\barch_task_apply_syscall_frame\b/task_apply_syscall_frame/g'
run_sed 's/\barch_task_sync_syscall_soft_mirror\b/task_sync_syscall_soft_mirror/g'
run_sed 's/\barch_task_save_irq_user_frame\b/task_save_irq_user_frame/g'
run_sed 's/\barch_task_apply_kernel_segments\b/task_apply_kernel_segments/g'
run_sed 's/\barch_task_apply_user_segments\b/task_apply_user_segments/g'
run_sed 's/\barch_task_retval_slot_addr\b/task_retval_slot_addr/g'
run_sed 's/\barch_task_load_sigcontext\b/task_load_sigcontext/g'
run_sed 's/\barch_task_store_sigcontext\b/task_store_sigcontext/g'

# Task context (inline headers)
run_sed 's/\barch_task_context_clone\b/task_context_clone/g'
run_sed 's/\barch_task_context_init\b/task_context_init/g'
run_sed 's/\barch_task_set_user_segments\b/task_set_user_segments/g'
run_sed 's/\barch_task_set_kernel_segments\b/task_set_kernel_segments/g'
run_sed 's/\barch_task_clear_frame_pointer\b/task_clear_frame_pointer/g'
run_sed 's/\barch_task_set_frame_pointer\b/task_set_frame_pointer/g'
run_sed 's/\barch_task_set_user_return\b/task_set_user_return/g'
run_sed 's/\barch_task_prepare_fork_child\b/task_prepare_fork_child/g'

# Signals
run_sed 's/\barch_signal_fill_sigcontext_from_syscall_frame\b/signal_fill_sigcontext_from_syscall_frame/g'
run_sed 's/\barch_signal_fill_sigcontext_from_irq_frame\b/signal_fill_sigcontext_from_irq_frame/g'
run_sed 's/\barch_signal_redirect_irq_frame\b/signal_redirect_irq_frame/g'
run_sed 's/\barch_signal_prepare_task_handler\b/signal_prepare_task_handler/g'
run_sed 's/\barch_sigcontext_ip\b/sigcontext_ip/g'
run_sed 's/\barch_sigcontext_sp\b/sigcontext_sp/g'

# CPU / ELF / TLS
run_sed 's/\barch_restore_user_fs_base\b/restore_user_fs_base/g'
run_sed 's/\barch_hypervisor_present\b/hypervisor_present/g'
run_sed 's/\barch_hypervisor_vendor\b/hypervisor_vendor/g'
run_sed 's/\barch_elf_machine_supported\b/elf_machine_supported/g'

# Syscall frame accessors
run_sed 's/\barch_syscall_frame_set_arg\b/syscall_frame_set_arg/g'
run_sed 's/\barch_syscall_frame_arg\b/syscall_frame_arg/g'
run_sed 's/\barch_syscall_frame_set_flags\b/syscall_frame_set_flags/g'
run_sed 's/\barch_syscall_frame_set_sp\b/syscall_frame_set_sp/g'
run_sed 's/\barch_syscall_frame_set_ip\b/syscall_frame_set_ip/g'
run_sed 's/\barch_syscall_frame_flags\b/syscall_frame_flags/g'
run_sed 's/\barch_syscall_frame_sp\b/syscall_frame_sp/g'
run_sed 's/\barch_syscall_frame_ip\b/syscall_frame_ip/g'

# Type alias: keep struct name arch_syscall_frame for layout; add syscall_frame_t typedef
# (done in header edit below)

echo "PORT rename applied to ${#FILES[@]} files"
