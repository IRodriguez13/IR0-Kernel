/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ir0/task.h>
#include <ir0/signals.h>
#include <ir0/types.h>
#include <ir0/fd_types.h>
#include <ir0/files_struct.h>

#ifndef ECHILD
#define ECHILD 10 /* No child processes (POSIX; matches ir0/errno.h) */
#endif

/* waitpid / wait4 options and status inspection (POSIX-style) */
#define WNOHANG 1

#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFEXITED(s) (((s) & 0x7F) == 0)

#ifndef IR0_NGROUPS_MAX
#define IR0_NGROUPS_MAX 32
#endif

struct robust_list_head;

/* Process-local binding for /proc /sys /heart opens (not global virtual fds). */
typedef struct pseudo_fd_bind
{
	const void *ops; /* const pseudo_fs_ops_t * */
	void *ctx;
	int refs;
	int dynamic;
} pseudo_fd_bind_t;

/* Process execution mode */
typedef enum
{
	KERNEL_MODE = 0,  /* Running in kernel (dbgshell, embedded init) */
	USER_MODE = 1     /* Running in userspace (real processes) */
} process_mode_t;

/*
 * Sched mirror on process_t during migration — must stay in sync with
 * task.state via process_set_sched_state(). Lifecycle (alive/zombie/dead)
 * lives in process_lifecycle_t; do not use PROCESS_ZOMBIE as a sched state
 * writer path (use process_mark_zombie()).
 */
typedef enum
{
	PROCESS_READY = 0,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_ZOMBIE
} process_state_t;

typedef enum
{
	PROCESS_LIFECYCLE_ALIVE = 0,
	PROCESS_LIFECYCLE_ZOMBIE,
	PROCESS_LIFECYCLE_DEAD
} process_lifecycle_t;

/* Tracked anonymous/file mmap regions for demand paging and munmap */
struct mmap_region
{
	void *addr;
	void *hint_addr;
	size_t length;
	int prot;
	int flags;
	struct mmap_region *next;
};

/*
 * User register snapshot at syscall entry (Linux pt_regs subset).
 * Prefer the ISA-neutral name arch_syscall_frame_t in new code.
 * Layout decode/fill remains ISA-private (arch_syscall_frame / arch_switch).
 */
typedef struct
{
	uint64_t rip;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t r10;
	uint64_t r8;
	uint64_t r9;
} arch_syscall_frame_t;

/* Legacy alias — same type; do not invent a second frame layout. */
typedef arch_syscall_frame_t syscall_user_frame_t;

/*
 * Arch-owned thread TLS (x86: IA32_FS_BASE). Lives in a union with fs_base so
 * ASM IR0_PROC_FS_BASE_OFFSET stays stable; portable code uses process_tls_*.
 */
typedef struct arch_thread_state
{
	uint64_t tls_base;
} arch_thread_state_t;

typedef struct process
{
	task_t task;
	/*
	 * TLS / arch thread state. Prefer process_tls_get/set and
	 * p->arch_thread.tls_base — not open-coded fs_base outside asm_offsets.
	 */
	union
	{
		uint64_t fs_base;
		arch_thread_state_t arch_thread;
	};
	pid_t ppid;
	/*
	 * Address space: process->mm is the sole owner of page_directory,
	 * mmap_list, heap/stack cursors (see mm_struct). Use process_pgd(),
	 * process_heap_*, process_mmap_list(), process_mm_owns_tables().
	 */
	struct mm_struct *mm;
	struct files_struct *files;
	process_state_t state; /* sched mirror — use process_set_sched_state() */
	process_lifecycle_t lifecycle; /* ALIVE / ZOMBIE / DEAD */
	process_mode_t mode;  /* Execution mode (kernel vs user) */
	int exit_code;
	int exit_signal; /* >0 if killed by signal (wait WIFSIGNALED / WTERMSIG) */
	struct process *next;

	/* User and permissions */
	uint32_t uid;
	uint32_t gid;
	uint32_t euid;
	uint32_t egid;
	uint32_t umask;
	gid_t groups[IR0_NGROUPS_MAX];
	uint8_t ngroups;
	/*
	 * POSIX saved set-user-ID / set-group-ID: written from the effective IDs
	 * on setuid-on-exec, so a privileged program can drop and regain its
	 * effective ID (setuid(2) / setresuid(2) semantics).
	 */
	uint32_t suid;
	uint32_t sgid;
	/* prctl(PR_SET_NO_NEW_PRIVS): execve must not raise privileges. */
	uint8_t no_new_privs;
	/* AT_SECURE of the running image; recomputed on every execve. */
	uint8_t at_secure;
	pid_t tgid;
	pid_t sid;  /* session id (Linux setsid) */
	pid_t pgid; /* process group id */
	/* Ticks since boot at creation — /proc/[pid]/stat field 22 (starttime). */
	uint64_t start_ticks;
	struct robust_list_head *robust_list;
	
	/* Current working directory (host-absolute). */
	char cwd[256];
	/* chroot(2) root (host-absolute); "/" = no jail. Inherited on fork. */
	char root[256];
	
	/* Process command name (for ps) */
	char comm[16]; /* Process command name (max 15 chars + null) */

	/* Resource limits (Linux rlimit indices 0..15). */
#define IR0_RLIM_NLIMITS 16
	struct
	{
		uint64_t rlim_cur;
		uint64_t rlim_max;
	} rlimits[IR0_RLIM_NLIMITS];
	
	/* Poll: waiter activo mientras el proceso está bloqueado en poll() */
	void *poll_waiter;
	uint8_t poll_resume_via_arch;
	uint8_t clock_wait_armed;
	uint8_t syscall_interrupted;
	uint64_t clock_wait_deadline_ms;

	/*
	 * ITIMER_REAL (setitimer/alarm): absolute expire in uptime ms; 0 = off.
	 * interval_ms 0 = one-shot. Cleared on fork child and exec.
	 */
	uint64_t it_real_expire_ms;
	uint64_t it_real_interval_ms;

	/* Signal management */
	uint32_t signal_pending; /* Bitmask of pending signals */
	/* Signal handlers (function pointers to userspace handlers) */
	void (*signal_handlers[_NSIG])(int);  /* Array of signal handler functions */
	uint32_t signal_mask;  /* Mask of signals to block */
	uint32_t signal_ignored;  /* Mask of signals to ignore (SIG_IGN) */
	uint32_t signal_sa_flags[_NSIG]; /* Per-signal sa_flags from sigaction */
	uint32_t signal_sa_mask[_NSIG];  /* Per-signal sa_mask (during handler only) */
	void (*signal_restorer[_NSIG])(void); /* sa_restorer (SA_RESTORER / musl) */
	int *set_tid_ptr;      /* set_tid_address(2) userspace pointer */
	struct sigcontext *saved_context;  /* Saved context before signal handler (for sigreturn) */
	/*
	 * Set when a userspace handler frame is armed; cleared on the first
	 * switch_to_user_task into that handler. saved_context must stay until
	 * rt_sigreturn — without this flag, nested syscalls in the handler
	 * (sendto/setitimer from BusyBox ping SIGALRM) re-enter the handler.
	 */
	uint8_t signal_enter_pending;
	/*
	 * When set, handle_signals() leaves catchable user handlers pending
	 * (no sigframe / signal_enter_pending). Interruptible waits (e.g.
	 * tcp_wire_connect) can map SIGALRM → -ETIMEDOUT instead of jumping
	 * into BusyBox die-from-handler (exit from SIGALRM SEGVs on repeat).
	 */
	uint8_t signal_defer_catchable;

	/* Linux syscall insn frame (for fork child / blocked syscall return). */
	arch_syscall_frame_t syscall_frame;
	uint64_t syscall_resume_rax;
	uint8_t irq_frame_saved; /* blocked syscall: resume via switch_to_user_task */
	int *wait_status_ptr;    /* userspace wait4 status word while irq_frame_saved */
	/*
	 * wait4 blocked-syscall contract (D1.17): while irq_frame_saved from wait4,
	 * wait_target_pid holds the pid argument (>0 specific child; -1/0 any child).
	 * Wake/resume/reap paths must honour this — never complete wait4(pid>0) for
	 * another child, even if syscall_resume_rax was stale or mis-set.
	 */
	uint8_t wait_blocked;
	pid_t wait_target_pid;
	int wait_options;
	pid_t wait_resume_child_pid;

	/*
	 * Child blocked off runqueue until parent completes fork syscall return
	 * (process_fork_wake_pending at syscall exit; IRQs off until sysret).
	 */
	struct process *fork_pending_child;
	/*
	 * Parent: rewrite syscall_insn_entry stack slots from syscall_frame once
	 * after fork (global syscall stack can clobber saves during fork()).
	 */
	uint8_t fork_resync_syscall_stack;

	/*
	 * Cooperative in-syscall reschedule (syscall-insn / musl tasks).
	 * syscall_frame_fresh: this entry captured a valid Linux pt_regs in
	 *   syscall_frame (set only by process_capture_syscall_frame_at_entry,
	 *   i.e. the `syscall` insn path; int 0x80 tasks leave it 0).
	 * coop_resched_resume: the pending resume was armed by a cooperative
	 *   reschedule (not wait4), so the resume path must skip the zombie reap.
	 * Both let a time-sliced task resume via its syscall_frame (fresh iretq)
	 * instead of kernel_ret on the single shared global syscall stack, whose
	 * frame a peer task's syscall would clobber from the top.
	 */
	uint8_t syscall_frame_fresh;
	uint8_t coop_resched_resume;
	/*
	 * Class B close: arm requested kernel_ret resume but task.arch.rip was still
	 * userspace — do not set KERNEL CS until switch_context has saved kernel
	 * [rsp] (see process_after_task_save / process_arm_kernel_syscall_sleep).
	 */
	uint8_t want_kernel_ret;

	/*
	 * Per-process kernel stack (see IR0_PROC_KSTACK_SIZE). kstack_base is the
	 * kmalloc_aligned allocation (freed in process_destroy); kstack_top is the
	 * 16-byte aligned top loaded into kernel_syscall_stack_top and TSS.rsp0 when
	 * this task is scheduled. saved_user_rsp shadows the global user_rsp_save
	 * across context switches so a task resuming an in-kernel block loop restores
	 * its own user RSP at sysret instead of a peer's clobbered value.
	 */
	void *kstack_base;
	uint64_t kstack_top;
	uint64_t saved_user_rsp;

	/*
	 * Priority-band scheduler (CONFIG_SCHEDULER_POLICY==2): 0 = lowest,
	 * IR0_SCHED_PRIO_MAX = highest. RR and CFS-alias backends ignore this.
	 * Placed after ASM-critical fields so PROC_FS_BASE_OFFSET stays stable.
	 */
	int sched_prio;
} process_t;

#ifndef IR0_SCHED_PRIO_BANDS
#define IR0_SCHED_PRIO_BANDS 8
#endif
#ifndef IR0_SCHED_PRIO_DEFAULT
#define IR0_SCHED_PRIO_DEFAULT 4
#endif
#ifndef IR0_SCHED_PRIO_MAX
#define IR0_SCHED_PRIO_MAX (IR0_SCHED_PRIO_BANDS - 1)
#endif

/*
 * ASM offset contract: switch_x64.asm restores FS_BASE by reading
 * [r11 + PROC_FS_BASE_OFFSET]. The asm hard-codes the numeric offset
 * for portability with NASM; this assert keeps both ends in sync.
 *
 * The placeholder forces a compile error showing the real value if it drifts.
 */
/*
 * FS_BASE offset: includes/ir0/asm_offsets.h + asm_offsets.inc (NASM).
 * arch-guard verifies the C header matches the .inc file.
 */
#include <ir0/asm_offsets.h>
#if defined(__x86_64__) || defined(__amd64__)
_Static_assert(offsetof(process_t, task) == 0, "process_t.task must be at offset 0");
_Static_assert(offsetof(process_t, fs_base) == IR0_PROC_FS_BASE_OFFSET,
	       "asm_offsets.h PROC_FS_BASE out of sync with process_t");
#endif

#include <ir0/mm_struct.h>

static inline fd_entry_t *process_fd_table(const process_t *p)
{
	if (!p || !p->files)
		return NULL;
	return p->files->fd_table;
}

static inline process_t *task_to_process(task_t *task)
{
	return (process_t *)((char *)task - offsetof(process_t, task));
}

static inline const process_t *task_to_process_const(const task_t *task)
{
	return (const process_t *)((const char *)task - offsetof(process_t, task));
}

static inline uint64_t process_mm_root(const process_t *p)
{
	return p ? task_mm_root(&p->task) : 0;
}

static inline uint64_t *process_pgd(const process_t *p)
{
	return (p && p->mm) ? p->mm->page_directory : NULL;
}

static inline void process_set_pgd(process_t *p, uint64_t *pgd)
{
	if (!p || !p->mm)
		return;
	p->mm->page_directory = pgd;
	if (!pgd)
		p->mm->owns_tables = 0;
}

static inline struct mmap_region *process_mmap_list(const process_t *p)
{
	return (p && p->mm) ? p->mm->mmap_list : NULL;
}

static inline struct mmap_region **process_mmap_list_p(process_t *p)
{
	return (p && p->mm) ? &p->mm->mmap_list : NULL;
}

static inline uint64_t process_mmap_base(const process_t *p)
{
	return (p && p->mm) ? p->mm->mmap_base : 0;
}

static inline void process_set_mmap_base(process_t *p, uint64_t base)
{
	if (p && p->mm)
		p->mm->mmap_base = base;
}

static inline uint64_t process_heap_start(const process_t *p)
{
	return (p && p->mm) ? p->mm->heap_start : 0;
}

static inline uint64_t process_heap_end(const process_t *p)
{
	return (p && p->mm) ? p->mm->heap_end : 0;
}

static inline void process_set_heap_start(process_t *p, uint64_t v)
{
	if (p && p->mm)
		p->mm->heap_start = v;
}

static inline void process_set_heap_end(process_t *p, uint64_t v)
{
	if (p && p->mm)
		p->mm->heap_end = v;
}

static inline uint64_t process_stack_start(const process_t *p)
{
	return (p && p->mm) ? p->mm->stack_start : 0;
}

static inline uint64_t process_stack_size(const process_t *p)
{
	return (p && p->mm) ? p->mm->stack_size : 0;
}

static inline void process_set_stack_layout(process_t *p, uint64_t start,
					   uint64_t size)
{
	if (!p || !p->mm)
		return;
	p->mm->stack_start = start;
	p->mm->stack_size = size;
}

static inline void process_set_mm_root(process_t *p, uint64_t root)
{
	if (p)
		task_set_mm_root(&p->task, root);
}

static inline pid_t process_pid(const process_t *p)
{
	return p ? p->task.pid : 0;
}

static inline uint64_t process_tls_get(const process_t *p)
{
	return p ? p->arch_thread.tls_base : 0;
}

static inline void process_tls_set(process_t *p, uint64_t tls)
{
	if (p)
		p->arch_thread.tls_base = tls;
}

/* Opaque syscall-frame accessors (layout is ISA-shaped; do not open-code fields). */
static inline uint64_t process_syscall_ip(const process_t *p)
{
	return p ? p->syscall_frame.rip : 0;
}

static inline uint64_t process_syscall_sp(const process_t *p)
{
	return p ? p->syscall_frame.rsp : 0;
}

static inline uint64_t process_syscall_flags(const process_t *p)
{
	return p ? p->syscall_frame.rflags : 0;
}

static inline void process_syscall_set_ip(process_t *p, uint64_t ip)
{
	if (p)
		p->syscall_frame.rip = ip;
}

static inline void process_syscall_set_sp(process_t *p, uint64_t sp)
{
	if (p)
		p->syscall_frame.rsp = sp;
}

static inline void process_syscall_set_flags(process_t *p, uint64_t flags)
{
	if (p)
		p->syscall_frame.rflags = flags;
}

/* Linux x86-64 ABI arg slots: 0=rdi … 5=r9 (ARM64 maps later). */
static inline uint64_t process_syscall_arg(const process_t *p, unsigned n)
{
	if (!p)
		return 0;
	switch (n)
	{
	case 0:
		return p->syscall_frame.rdi;
	case 1:
		return p->syscall_frame.rsi;
	case 2:
		return p->syscall_frame.rdx;
	case 3:
		return p->syscall_frame.r10;
	case 4:
		return p->syscall_frame.r8;
	case 5:
		return p->syscall_frame.r9;
	default:
		return 0;
	}
}

static inline void process_syscall_set_arg(process_t *p, unsigned n, uint64_t v)
{
	if (!p)
		return;
	switch (n)
	{
	case 0:
		p->syscall_frame.rdi = v;
		break;
	case 1:
		p->syscall_frame.rsi = v;
		break;
	case 2:
		p->syscall_frame.rdx = v;
		break;
	case 3:
		p->syscall_frame.r10 = v;
		break;
	case 4:
		p->syscall_frame.r8 = v;
		break;
	case 5:
		p->syscall_frame.r9 = v;
		break;
	default:
		break;
	}
}

void process_capture_syscall_frame(process_t *p);
void process_capture_syscall_frame_at_entry(uint64_t *frame_base, uint64_t rip_hw);
void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax);
/* Soft pt_regs→task mirror; no-op if KERNEL CS or want_kernel_ret (Class B). */
void process_sync_task_user_ip_from_syscall_frame(process_t *p);

void process_restore_user_task_segments(process_t *p);

void process_save_user_context_from_irq_frame(uint64_t *gpr_stack);

void process_arm_blocked_syscall_resume(process_t *p, uint64_t rax);
void process_arm_coop_resched_resume(process_t *p, uint64_t rax);
void process_clear_in_thread_syscall_block(process_t *p);
void process_reset_blocked_syscall_state(process_t *p);
void process_arm_kernel_syscall_sleep(process_t *p);
/* After switch_context saved prev: honour want_kernel_ret (Class B close). */
void process_after_task_save(task_t *prev);

/* Class B: KERNEL CS + userspace RIP unsafe for kernel_ret (see process_ctx_invariant.h). */
int process_task_kernel_ret_rip_bad(const task_t *t);
void fork_ret_emit_pre_return(void);
void fork_restore_emit_pre_iretq(void);
void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw);
int fork_flow_note_debug_exception(uint64_t *stack);
void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall);
__attribute__((noreturn)) void process_exit(int code);
int process_wait(pid_t pid, int *status, int options);

/*
 * Single writers for sched vs lifecycle (§4A). Prefer these over raw
 * p->state / p->task.state assignments so the two views cannot diverge.
 */
static inline void process_set_sched_state(process_t *p, process_state_t st)
{
	if (!p || st == PROCESS_ZOMBIE)
		return;

	p->state = st;
	switch (st)
	{
	case PROCESS_READY:
		p->task.state = TASK_SCHED_RUNNABLE;
		break;
	case PROCESS_RUNNING:
		p->task.state = TASK_SCHED_RUNNING;
		break;
	case PROCESS_BLOCKED:
		p->task.state = TASK_SCHED_SLEEPING;
		break;
	default:
		break;
	}
}

static inline void process_mark_zombie(process_t *p)
{
	if (!p)
		return;
	p->lifecycle = PROCESS_LIFECYCLE_ZOMBIE;
	p->state = PROCESS_ZOMBIE;
	p->task.state = TASK_SCHED_TERMINATED;
}

static inline void process_mark_dead(process_t *p)
{
	if (!p)
		return;
	p->lifecycle = PROCESS_LIFECYCLE_DEAD;
	p->state = PROCESS_ZOMBIE;
	p->task.state = TASK_SCHED_TERMINATED;
}

static inline int process_is_zombie(const process_t *p)
{
	return p && (p->lifecycle == PROCESS_LIFECYCLE_ZOMBIE ||
		     p->state == PROCESS_ZOMBIE);
}

static inline int process_is_alive(const process_t *p)
{
	return p && p->lifecycle == PROCESS_LIFECYCLE_ALIVE &&
	       p->state != PROCESS_ZOMBIE;
}

/*
 * True when parent is blocked in wait4 and child_pid satisfies the wait target.
 * Exported for ktests; used by wake/resume/reap paths.
 */
int process_wait_child_matches_blocked_target(const process_t *parent,
					      pid_t child_pid);

/* Linux wait status word for a zombie (exit << 8 or WTERMSIG). */
int process_child_wait_status_word(const process_t *child);

/*
 * Fatal default action for kill(2): SIGKILL always; SIGTERM/SIGHUP when not
 * caught or ignored. Returns 1 if @target is now a zombie (or self-exited).
 */
int process_signal_is_default_fatal(process_t *p, int sig);
int process_signal_default_kill(process_t *target, int signal);

/*
 * Reap a zombie child when resuming a blocked wait4 syscall.
 * Must run after the child has finished process_exit(), before returning to user.
 */
void process_reap_zombie_on_wait_resume(process_t *parent, pid_t child_pid);

/* IR0 PHILOSOPHY: Only spawn() creates processes - total simplicity
 * Mode must be explicitly specified - no magic address detection */
pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode);

/* Convenience wrappers for explicit mode specification */
pid_t spawn_user(void (*entry)(void), const char *name);
pid_t spawn_kernel(void (*entry)(void), const char *name);

/* Fork exists only for POSIX syscall compatibility - uses spawn() internally */
pid_t fork(void);
pid_t clone_thread(unsigned long flags, void *stack, int *parent_tid,
		   int *child_tid, unsigned long tls);

/*
 * Enqueue fork_pending_child after parent syscall retval is committed.
 * Called from syscall_dispatch on fork/clone/vfork exit only.
 */
void process_fork_wake_pending(process_t *parent);
void process_syscall_restore_exit_regs(uint64_t *stack_r9_slot);


pid_t process_get_pid(void);
pid_t process_get_ppid(void);
process_t *process_get_current(void);
void irq_save_user_frame(uint64_t *frame);
process_t *get_process_list(void);
void process_itimer_tick(uint64_t now_ms);
pid_t process_get_next_pid(void);
pid_t process_last_assigned_pid(void);
void process_prepare_pid1_for_init(void);
process_t *process_find_by_pid(pid_t pid);  /* Find process by PID */



uint64_t create_process_page_directory(void);
void process_init_fd_table(process_t *process);

/* Process lifecycle management */
void process_reap_zombies(process_t *parent); /* Reap zombie children (used by init) */
void process_reap_zombie_child(process_t *child);
void process_destroy(process_t *p);

/*
 * Per-process kernel stack lifecycle (IR0_PROC_KSTACK_SIZE). alloc returns
 * 0 on success or a negative errno; free is idempotent (NULL-safe).
 */
int process_kernel_stack_alloc(process_t *p);
void process_kernel_stack_free(process_t *p);
void process_unmap_user_address_space(process_t *p);
int process_remove_from_list(process_t *target);

uint64_t *process_pt_child(uint64_t *table, size_t index);
void process_fase50_trace_proc(const char *stage, process_t *p);

#include "debug/fase_audit.h"

int64_t process_close_fd(process_t *proc, int fd);
void process_exec_close_cloexec(process_t *p);
bool process_user_va_range_overlaps(process_t *proc, uintptr_t addr, size_t length);

uint64_t process_list_count(void);
uint64_t process_list_count_user(void);

extern process_t *current_process;
extern process_t *process_list;
