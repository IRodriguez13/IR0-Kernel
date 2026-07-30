/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_syscalls.c
 * Description: process/credential/signal syscall helpers
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/syscalls_kernel.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/sched.h>
#include <ir0/permissions.h>
#include <ir0/clock_wait.h>
#include <ir0/clock.h>
#include <ir0/debug_runtime.h>
#include <ir0/ktm/klog.h>
#include <ir0/arch_task_ops.h>
#include <string.h>

#include <ir0/oops.h>
#include <ir0/kernel.h>
#include <ir0/elf_loader.h>
#include <ir0/path.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/ktm/klog.h>
#include <ir0/kmem.h>
#include <ir0/validation.h>
#include <ir0/debug_runtime.h>
#include <ir0/sched.h>
#include <ktm_probe_diag.h>
#include <ir0/ktm/checkpoint.h>
#include <config.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/time.h>
#include <ir0/clock.h>
#include <ir0/credentials.h>
#include <ir0/power_manag.h>
#include <ir0/futex.h>
#include <ir0/kexec.h>
#include <ir0/acpi_pm.h>

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

/* prctl(2) options (Linux include/uapi/linux/prctl.h) */
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1


#define IR0_ROBUST_LIST_SIZE 24

void process_cred_init_groups(process_t *p)
{
	if (!p)
		return;
	if (p->ngroups == 0)
	{
		p->groups[0] = (gid_t)p->gid;
		p->ngroups = 1;
	}
}

int process_cred_in_group(const process_t *p, gid_t gid)
{
	uint8_t i;

	if (!p)
		return 0;
	if ((gid_t)p->egid == gid)
		return 1;
	for (i = 0; i < p->ngroups; i++)
	{
		if (p->groups[i] == gid)
			return 1;
	}
	return 0;
}

void process_reset_signals_on_exec(process_t *p)
{
	signals_reset_on_exec(p);
}

int64_t sys_getgroups(int size, gid_t *list)
{
	uint8_t n;
	int i;

	if (!current_process)
		return -ESRCH;

	process_cred_init_groups(current_process);
	n = current_process->ngroups;

	if (size == 0)
		return (int64_t)n;

	if (!list || size < 0)
		return -EINVAL;
	if (validate_userspace_buffer(list, (size_t)size * sizeof(gid_t)) != 0)
		return -EFAULT;
	if (size < (int)n)
		return -EINVAL;

	for (i = 0; i < (int)n; i++)
	{
		gid_t g = current_process->groups[i];
		if (copy_to_user(&list[i], &g, sizeof(g)) != 0)
			return -EFAULT;
	}
	return (int64_t)n;
}

int64_t sys_setgroups(size_t size, const gid_t *list)
{
	size_t i;

	if (!current_process)
		return -ESRCH;
	if (current_process->euid != ROOT_UID)
		return -EPERM;
	if (size > IR0_NGROUPS_MAX)
		return -EINVAL;
	if (size > 0 && !list)
		return -EFAULT;
	if (size > 0 &&
	    validate_userspace_buffer(list, size * sizeof(gid_t)) != 0)
		return -EFAULT;

	for (i = 0; i < size; i++)
	{
		gid_t g;
		if (copy_from_user(&g, &list[i], sizeof(g)) != 0)
			return -EFAULT;
		current_process->groups[i] = g;
	}
	current_process->ngroups = (uint8_t)size;
	if (size == 0)
	{
		current_process->groups[0] = (gid_t)current_process->gid;
		current_process->ngroups = 1;
	}
	return 0;
}

/*
 * Unprivileged setres*id: every requested value must already be one of the
 * caller's real, effective or saved ID (Linux setresuid(2)).
 */
static int cred_id_allowed(uint32_t want, uint32_t real, uint32_t eff,
			   uint32_t saved)
{
	return want == real || want == eff || want == saved;
}

int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	int priv;

	if (!current_process)
		return -ESRCH;

	priv = current_process->euid == ROOT_UID;

	if (!priv)
	{
		if ((int)ruid != (int)-1 &&
		    !cred_id_allowed((uint32_t)ruid, current_process->uid,
				     current_process->euid, current_process->suid))
			return -EPERM;
		if ((int)euid != (int)-1 &&
		    !cred_id_allowed((uint32_t)euid, current_process->uid,
				     current_process->euid, current_process->suid))
			return -EPERM;
		if ((int)suid != (int)-1 &&
		    !cred_id_allowed((uint32_t)suid, current_process->uid,
				     current_process->euid, current_process->suid))
			return -EPERM;
	}

	if ((int)ruid != (int)-1)
		current_process->uid = (uint32_t)ruid;
	if ((int)euid != (int)-1)
		current_process->euid = (uint32_t)euid;
	if ((int)suid != (int)-1)
		current_process->suid = (uint32_t)suid;
	return 0;
}

int64_t sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	uid_t u;
	uid_t eu;
	uid_t su;

	if (!current_process)
		return -ESRCH;

	u = (uid_t)current_process->uid;
	eu = (uid_t)current_process->euid;
	su = (uid_t)current_process->suid;

	if (ruid)
	{
		if (validate_userspace_buffer(ruid, sizeof(uid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(ruid, &u, sizeof(u)) != 0)
			return -EFAULT;
	}
	if (euid)
	{
		if (validate_userspace_buffer(euid, sizeof(uid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(euid, &eu, sizeof(eu)) != 0)
			return -EFAULT;
	}
	if (suid)
	{
		if (validate_userspace_buffer(suid, sizeof(uid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(suid, &su, sizeof(su)) != 0)
			return -EFAULT;
	}
	return 0;
}

int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	int priv;

	if (!current_process)
		return -ESRCH;

	priv = current_process->euid == ROOT_UID;

	if (!priv)
	{
		if ((int)rgid != (int)-1 &&
		    !cred_id_allowed((uint32_t)rgid, current_process->gid,
				     current_process->egid, current_process->sgid))
			return -EPERM;
		if ((int)egid != (int)-1 &&
		    !cred_id_allowed((uint32_t)egid, current_process->gid,
				     current_process->egid, current_process->sgid))
			return -EPERM;
		if ((int)sgid != (int)-1 &&
		    !cred_id_allowed((uint32_t)sgid, current_process->gid,
				     current_process->egid, current_process->sgid))
			return -EPERM;
	}

	if ((int)rgid != (int)-1)
		current_process->gid = (uint32_t)rgid;
	if ((int)egid != (int)-1)
		current_process->egid = (uint32_t)egid;
	if ((int)sgid != (int)-1)
		current_process->sgid = (uint32_t)sgid;
	process_cred_init_groups(current_process);
	return 0;
}

int64_t sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	gid_t g;
	gid_t eg;
	gid_t sg;

	if (!current_process)
		return -ESRCH;

	g = (gid_t)current_process->gid;
	eg = (gid_t)current_process->egid;
	sg = (gid_t)current_process->sgid;

	if (rgid)
	{
		if (validate_userspace_buffer(rgid, sizeof(gid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(rgid, &g, sizeof(g)) != 0)
			return -EFAULT;
	}
	if (egid)
	{
		if (validate_userspace_buffer(egid, sizeof(gid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(egid, &eg, sizeof(eg)) != 0)
			return -EFAULT;
	}
	if (sgid)
	{
		if (validate_userspace_buffer(sgid, sizeof(gid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(sgid, &sg, sizeof(sg)) != 0)
			return -EFAULT;
	}
	return 0;
}

static uint32_t rt_sigaction_mask_from_sigset(const sigset_t *set, size_t sigsetsize)
{
	uint64_t legacy64;

	if (sigsetsize == sizeof(uint64_t))
	{
		if (!set)
			return 0;
		memcpy(&legacy64, set, sizeof(legacy64));
		return (uint32_t)legacy64;
	}
	return ir0_sigset_low32(set);
}

static void rt_sigaction_mask_to_sigset(sigset_t *set, size_t sigsetsize, uint32_t mask)
{
	uint64_t legacy64;

	if (!set)
		return;

	if (sigsetsize == sizeof(uint64_t))
	{
		legacy64 = (uint64_t)mask;
		memcpy(set, &legacy64, sizeof(legacy64));
		return;
	}
	ir0_sigset_set_low32(set, mask);
}

/*
 * Linux/musl may pass sigsetsize=8; user buffers are sized for the uapi layout,
 * not for IR0 internal struct sigaction.  Copy handler + flags + restorer +
 * mask64 (32 bytes), never sizeof(struct sigaction).
 */
static size_t rt_sigaction_user_copy_size(size_t sigsetsize)
{
	if (sigsetsize == sizeof(uint64_t))
		return offsetof(struct sigaction, sa_mask) + sizeof(uint64_t);
	return sizeof(struct sigaction);
}

int64_t sys_rt_sigaction(int signum, const struct sigaction *act,
			 struct sigaction *oldact, size_t sigsetsize)
{
	struct sigaction kact;
	struct sigaction kold;
	size_t user_bytes;

	if (!current_process)
		return -ESRCH;
	if (signum < 1 || signum >= _NSIG)
		return -EINVAL;
	if (signum == SIGKILL || signum == SIGSTOP)
		return -EINVAL;
	/* sigsetsize=8 (uapi compact) or sizeof(sigset_t); anything else -EINVAL. */
	if (sigsetsize != sizeof(sigset_t) && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	user_bytes = rt_sigaction_user_copy_size(sigsetsize);

	if (oldact)
	{
		if (validate_userspace_buffer(oldact, user_bytes) != 0)
			return -EFAULT;
		memset(&kold, 0, sizeof(kold));
		kold.sa_handler = current_process->signal_handlers[signum];
		rt_sigaction_mask_to_sigset(&kold.sa_mask, sigsetsize,
					    current_process->signal_sa_mask[signum]);
		kold.sa_flags = (unsigned long)current_process->signal_sa_flags[signum];
		kold.sa_restorer = current_process->signal_restorer[signum];
		if (copy_to_user(oldact, &kold, user_bytes) != 0)
			return -EFAULT;
	}

	if (act)
	{
		if (validate_userspace_buffer(act, user_bytes) != 0)
			return -EFAULT;
		memset(&kact, 0, sizeof(kact));
		if (copy_from_user(&kact, act, user_bytes) != 0)
			return -EFAULT;
		if (register_signal_handler(signum, kact.sa_handler) != 0)
			return -EFAULT;
		current_process->signal_sa_mask[signum] =
			rt_sigaction_mask_from_sigset(&kact.sa_mask, sigsetsize);
		current_process->signal_sa_flags[signum] = (uint32_t)kact.sa_flags;
		current_process->signal_restorer[signum] =
			(kact.sa_flags & SA_RESTORER) ? kact.sa_restorer : NULL;
#if defined(SIGNAL_DELIVER_LOG) && SIGNAL_DELIVER_LOG
		if (signum == SIGSEGV)
		{
			klog_debug_fmt("SIGNAL", "[SIGNAL][REG] pid=%x sig=SIGSEGV handler=%llx flags=%x proc_mask=%x", (unsigned)((uint32_t)current_process->task.pid), (unsigned long long)((uint64_t)(uintptr_t)kact.sa_handler), (unsigned)((uint32_t)kact.sa_flags), (unsigned)(current_process->signal_mask));
		}
#endif
	}

	return 0;
}

int64_t sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset,
			   size_t sigsetsize)
{
	sigset_t kset;
	uint32_t newmask;
	uint64_t legacy64;

	if (!current_process)
		return -ESRCH;
	if (sigsetsize != sizeof(sigset_t) && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	if (oldset)
	{
		sigset_t kold;

		if (sigsetsize == sizeof(uint64_t))
		{
			if (validate_userspace_buffer(oldset, sizeof(uint64_t)) != 0)
				return -EFAULT;
			legacy64 = (uint64_t)current_process->signal_mask;
			if (copy_to_user(oldset, &legacy64, sizeof(legacy64)) != 0)
				return -EFAULT;
		}
		else
		{
			if (validate_userspace_buffer(oldset, sizeof(sigset_t)) != 0)
				return -EFAULT;
			ir0_sigset_set_low32(&kold, current_process->signal_mask);
			if (copy_to_user(oldset, &kold, sizeof(kold)) != 0)
				return -EFAULT;
		}
	}

	if (!set)
		return 0;

	if (sigsetsize == sizeof(uint64_t))
	{
		if (validate_userspace_buffer(set, sizeof(uint64_t)) != 0)
			return -EFAULT;
		if (copy_from_user(&legacy64, set, sizeof(legacy64)) != 0)
			return -EFAULT;
		newmask = (uint32_t)legacy64;
	}
	else
	{
		if (validate_userspace_buffer(set, sizeof(sigset_t)) != 0)
			return -EFAULT;
		if (copy_from_user(&kset, set, sizeof(kset)) != 0)
			return -EFAULT;
		newmask = ir0_sigset_low32(&kset);
	}
	switch (how)
	{
	case SIG_BLOCK:
		current_process->signal_mask |= newmask;
		break;
	case SIG_UNBLOCK:
		current_process->signal_mask &= ~newmask;
		break;
	case SIG_SETMASK:
		current_process->signal_mask = newmask;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * sys_rt_sigsuspend - Linux rt_sigsuspend(2): replace mask and wait for signal.
 * Always returns -EINTR when a non-blocked signal is pending (musl sigsuspend).
 */
int64_t sys_rt_sigsuspend(const sigset_t *mask, size_t sigsetsize)
{
	sigset_t kset;
	uint32_t saved_mask;

	if (!current_process)
		return -ESRCH;
	if (!mask || sigsetsize != sizeof(sigset_t))
		return -EINVAL;
	if (validate_userspace_buffer((void *)mask, sizeof(sigset_t)) != 0)
		return -EFAULT;
	if (copy_from_user(&kset, mask, sizeof(kset)) != 0)
		return -EFAULT;

	saved_mask = current_process->signal_mask;
	current_process->signal_mask = ir0_sigset_low32(&kset);

	for (;;)
	{
		if (current_process->signal_pending &
		    ~current_process->signal_mask)
			break;

		process_set_sched_state(current_process, PROCESS_BLOCKED);
		process_arm_kernel_syscall_sleep(current_process);
		while (current_process->state == PROCESS_BLOCKED)
		{
			ir0_clock_wait_service_runqueue();
			if (current_process->state != PROCESS_BLOCKED)
				break;
		}
	}

	current_process->signal_mask = saved_mask;
	return -EINTR;
}

int64_t sys_tgkill(pid_t tgid, pid_t tid, int sig)
{
	process_t *p;

	if (!current_process)
		return -ESRCH;
	if (sig < 0 || sig >= _NSIG)
		return -EINVAL;

	p = process_find_by_pid(tid);
	if (!p || p->tgid != tgid)
		return -ESRCH;
	if (sig == 0)
		return 0;
	if (send_signal(tid, sig) != 0)
		return -ESRCH;
	return 0;
}

struct robust_list_head
{
	void *list;
	long futex_offset;
	void *list_op_pending;
};

int64_t sys_set_robust_list(struct robust_list_head *head, size_t len)
{
	if (!current_process)
		return -ESRCH;

	if (len != sizeof(struct robust_list_head))
		return -EINVAL;

	if (head && current_process->mode == USER_MODE)
	{
		if (validate_userspace_buffer(head, sizeof(struct robust_list_head)) != 0)
			return -EFAULT;
	}

	current_process->robust_list = head;
	return 0;
}

int64_t sys_get_robust_list(int pid, struct robust_list_head **head_ptr,
			    size_t *len_ptr)
{
	process_t *target = current_process;
	struct robust_list_head *head;
	size_t len = sizeof(struct robust_list_head);

	if (!current_process)
		return -ESRCH;
	if (pid != 0 && pid != (int)current_process->task.pid)
		return -ESRCH;
	if (!head_ptr || !len_ptr)
		return -EFAULT;
	if (validate_userspace_buffer(head_ptr, sizeof(*head_ptr)) != 0)
		return -EFAULT;
	if (validate_userspace_buffer(len_ptr, sizeof(*len_ptr)) != 0)
		return -EFAULT;

	head = target->robust_list;
	if (copy_to_user(head_ptr, &head, sizeof(head)) != 0)
		return -EFAULT;
	if (copy_to_user(len_ptr, &len, sizeof(len)) != 0)
		return -EFAULT;
	return 0;
}

/**
 * Best-effort robust futex exit: wake one waiter on the pending/list head
 * word if present (Linux FUTEX_OWNER_DIED subset deferred to full walk).
 */
void process_exit_robust_list(process_t *p)
{
	struct robust_list_head kh;
	int *uaddr;

	if (!p || !p->robust_list)
		return;

	if (p->mode == USER_MODE)
	{
		if (validate_userspace_buffer(p->robust_list, sizeof(kh)) != 0)
		{
			p->robust_list = NULL;
			return;
		}
		if (copy_from_user(&kh, p->robust_list, sizeof(kh)) != 0)
		{
			p->robust_list = NULL;
			return;
		}
	}
	else
	{
		kh = *p->robust_list;
	}

	uaddr = (int *)kh.list_op_pending;
	if (!uaddr)
		uaddr = (int *)kh.list;
	if (uaddr && p->mode == USER_MODE &&
	    validate_userspace_buffer(uaddr, sizeof(int)) == 0)
		(void)ir0_futex_wake(uaddr, 1);

	p->robust_list = NULL;
	klog_smoke("ROBUST_LIST_EXIT_OK");
}

int64_t sys_setsid(void)
{
	pid_t pid;

	if (!current_process)
		return -ESRCH;

	pid = current_process->task.pid;
	/* Already a session leader. */
	if (current_process->sid == pid)
		return -EPERM;

	current_process->sid = pid;
	current_process->pgid = pid;
	return (int64_t)pid;
}

int64_t sys_getsid(pid_t pid)
{
	process_t *target;

	if (!current_process)
		return -ESRCH;

	target = (pid == 0) ? current_process : process_find_by_pid(pid);
	if (!target)
		return -ESRCH;

	return (int64_t)target->sid;
}

int64_t sys_getpgid(pid_t pid)
{
	process_t *target;

	if (!current_process)
		return -ESRCH;

	target = (pid == 0) ? current_process : process_find_by_pid(pid);
	if (!target)
		return -ESRCH;

	return (int64_t)target->pgid;
}

int64_t sys_setpgid(pid_t pid, pid_t pgid)
{
	process_t *target;
	pid_t self;

	if (!current_process)
		return -ESRCH;

	self = current_process->task.pid;
	if (pid == 0)
		pid = self;
	if (pgid < 0)
		return -EINVAL;

	target = process_find_by_pid(pid);
	if (!target)
		return -ESRCH;

	/* Only self or a direct child in the same session. */
	if (target != current_process)
	{
		if (target->ppid != self)
			return -ESRCH;
		if (target->sid != current_process->sid)
			return -EPERM;
	}

	if (pgid == 0)
		pgid = target->task.pid;

	/* Session leaders cannot leave their process group. */
	if (target->sid == target->task.pid && pgid != target->task.pid)
		return -EPERM;

	/* pgid must be an existing group in the same session, or create own. */
	if (pgid != target->task.pid)
	{
		process_t *scan;
		int found = 0;

		for (scan = process_list; scan; scan = scan->next)
		{
			if (scan->pgid == pgid && scan->sid == target->sid)
			{
				found = 1;
				break;
			}
		}
		if (!found)
			return -EPERM;
	}

	target->pgid = pgid;
	return 0;
}

static uint64_t fase50_count_open_fds_local(process_t *p)
{
	(void)p;
	return 0;
}
static void fase50_trace_syscall_proc(const char *stage, process_t *p)
{
	(void)stage;
	(void)p;
}
int64_t sys_exit(int exit_code)
{
  if (!current_process)
    return -ESRCH;

  /* Use process_exit() for complete cleanup:
   * - Reparents children to init
   * - Reaps zombie children
   * - Sends SIGCHLD to parent
   * - Removes from scheduler
   * - Switches to another process
   * This function never returns */
  process_exit(exit_code);

  /* Should never reach here - process_exit() switches context */
  panicex("sys_exit: process_exit() returned (should not happen)", RUNNING_OUT_PROCESS, __FILE__, __LINE__, __func__);
  return 0;
}

int64_t sys_exit_group(int exit_code)
{
  return sys_exit(exit_code);
}

int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg)
{
	(void)arg;

	if (!ir0_cred_is_root())
		return -EPERM;

	if (magic1 != (int)LINUX_REBOOT_MAGIC1 ||
	    (magic2 != (int)LINUX_REBOOT_MAGIC2 &&
	     magic2 != (int)LINUX_REBOOT_MAGIC2A &&
	     magic2 != (int)LINUX_REBOOT_MAGIC2B &&
	     magic2 != (int)LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	switch (cmd)
	{
	case LINUX_REBOOT_CMD_CAD_ON:
		klog_smoke("REBOOT_CAD_ON");
		return 0;
	case LINUX_REBOOT_CMD_CAD_OFF:
		klog_smoke("REBOOT_CAD_OFF");
		return 0;
	case LINUX_REBOOT_CMD_RESTART:
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_RESTART2:
		klog_smoke("REBOOT_RESTART2");
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_HALT:
		kernel_system_shutdown(IR0_SYSTEM_HALT);
		break;
	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_system_shutdown(IR0_SYSTEM_POWEROFF);
		break;
	case LINUX_REBOOT_CMD_KEXEC:
		if (ir0_kexec_image_loaded())
		{
			ir0_kexec_execute();
			/* Magic payload returns here; soft reboot. */
			kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		}
		klog_smoke("REBOOT_KEXEC_STUB");
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_SW_SUSPEND:
		(void)ir0_acpi_pm_try_suspend();
		return 0;
	default:
		return -EINVAL;
	}

	/* noreturn paths above */
	return 0;
}

int64_t sys_getpid(void)
{
  if (!current_process)
    return -ESRCH;
  return process_pid(current_process);
}

int64_t sys_gettid(void)
{
  pid_t tid;

  if (!current_process)
    return -ESRCH;

  tid = process_pid(current_process);

  return (int64_t)tid;
}

int64_t sys_getppid(void)
{
  if (!current_process)
    return -ESRCH;
  /* Parent pid recorded at fork/exec */
  return (int64_t)current_process->ppid;
}

int64_t sys_getuid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->uid;
}

int64_t sys_geteuid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->euid;
}

int64_t sys_getgid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->gid;
}

int64_t sys_getegid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->egid;
}

int64_t sys_setuid(uid_t uid)
{
  if (!current_process)
    return -ESRCH;

  /* Privileged: real, effective and saved UID all become @uid (no way back). */
  if (current_process->euid == ROOT_UID)
  {
    current_process->uid = (uint32_t)uid;
    current_process->euid = (uint32_t)uid;
    current_process->suid = (uint32_t)uid;
    return 0;
  }

  /* Unprivileged: only the effective UID moves, and only to real or saved. */
  if ((uint32_t)uid == current_process->uid || (uint32_t)uid == current_process->suid)
  {
    current_process->euid = (uint32_t)uid;
    return 0;
  }

  return -EPERM;
}

int64_t sys_setgid(gid_t gid)
{
  if (!current_process)
    return -ESRCH;

  if (current_process->euid == ROOT_UID)
  {
    current_process->gid = (uint32_t)gid;
    current_process->egid = (uint32_t)gid;
    current_process->sgid = (uint32_t)gid;
    return 0;
  }

  if ((uint32_t)gid == current_process->gid || (uint32_t)gid == current_process->sgid)
  {
    current_process->egid = (uint32_t)gid;
    return 0;
  }

  return -EPERM;
}

int64_t sys_umask(mode_t mask)
{
  if (!current_process)
    return -ESRCH;

  mode_t old = (mode_t)(current_process->umask & 0777U);
  current_process->umask = (uint32_t)(mask & 0777U);
  return (int64_t)old;
}

int64_t sys_exec(const char *pathname,
                 char *const argv[],
                 char *const envp[])
{
  int exec_argv_slots_seen = 0;
#if CONFIG_DEBUG_FASE50
  klog_debug("KERN", "SERIAL: sys_exec called\n");
#endif
  fase50_trace_syscall_proc("sys_exec-entry", current_process);

  if (!current_process || !pathname)
  {
    return -EFAULT;
  }

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Unix-style: resolve relative against cwd; absolute under chroot. */
  char resolved_path[256];
  const char *path_to_use = pathname;
  int path_rc;

  path_rc = ir0_resolve_user_path(pathname, resolved_path, sizeof(resolved_path),
                                  current_process->cwd, current_process->root);
  if (path_rc != 0)
    return path_rc;
  path_to_use = resolved_path;

  /* argv/envp are NULL-terminated vectors; only validate the first slot here.
   * Per-entry mapped checks happen inside the iteration below.
   */
  if (argv && !is_user_address(argv, sizeof(char *)))
    return -EFAULT;
  if (envp && !is_user_address(envp, sizeof(char *)))
    return -EFAULT;

  /* Copy argv and envp from userspace if provided */
  char *kernel_argv[256] = {NULL};
  char *kernel_envp[256] = {NULL};
  int argc_kept = 0;
  int envc_kept = 0;
  int rc_vec = 0;

  if (argv)
  {
    int i;
    for (i = 0; i < 256; i++)
    {
      const void *slot = (const char *)argv + i * sizeof(char *);
      char *user_ptr = NULL;

      /* Each slot is 8 bytes — within a single page; check mapping for that page. */
      if (!is_user_address_checked(slot, sizeof(char *), 1))
      {
        rc_vec = -EFAULT;
        break;
      }
      if (copy_from_user(&user_ptr, slot, sizeof(char *)) != 0)
      {
        rc_vec = -EFAULT;
        break;
      }
      if (user_ptr == NULL)
        break;  /* end of vector */
      exec_argv_slots_seen = i + 1;

      /* Validate string start is mapped (first byte covers its starting page). */
      if (!is_user_address_checked(user_ptr, 1, 1))
      {
        rc_vec = -EFAULT;
        break;
      }

      char *arg_str = (char *)kmalloc_try(256);
      if (!arg_str)
      {
        rc_vec = -ENOMEM;
        break;
      }
      /* Stop at NUL — fixed 256-byte copies EFAULT when heap strings sit
       * near an unmapped page (doas prepenv/asprintf). */
      if (copy_from_user_cstring(arg_str, 256, user_ptr) != 0)
      {
        kfree(arg_str);
        rc_vec = -EFAULT;
        break;
      }
      kernel_argv[i] = arg_str;
      argc_kept = i + 1;
    }
    if (rc_vec)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      return rc_vec;
    }
    if (i == 256)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      return -E2BIG;
    }
  }

  if (envp)
  {
    int i;
    for (i = 0; i < 256; i++)
    {
      const void *slot = (const char *)envp + i * sizeof(char *);
      char *user_ptr = NULL;

      if (!is_user_address_checked(slot, sizeof(char *), 1))
      {
        rc_vec = -EFAULT;
        break;
      }
      if (copy_from_user(&user_ptr, slot, sizeof(char *)) != 0)
      {
        rc_vec = -EFAULT;
        break;
      }
      if (user_ptr == NULL)
        break;

      if (!is_user_address_checked(user_ptr, 1, 1))
      {
        rc_vec = -EFAULT;
        break;
      }

      char *env_str = (char *)kmalloc_try(256);
      if (!env_str)
      {
        rc_vec = -ENOMEM;
        break;
      }
      if (copy_from_user_cstring(env_str, 256, user_ptr) != 0)
      {
        kfree(env_str);
        rc_vec = -EFAULT;
        break;
      }
      kernel_envp[i] = env_str;
      envc_kept = i + 1;
    }
    if (rc_vec)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      for (int j = 0; j < envc_kept; j++)
        kfree(kernel_envp[j]);
      return rc_vec;
    }
    if (i == 256)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      for (int j = 0; j < envc_kept; j++)
        kfree(kernel_envp[j]);
      return -E2BIG;
    }
  }

  int64_t result;

  if (current_process->mode == USER_MODE)
  {
    ktm_probe_diag_execve(current_process, path_to_use);
    fase50_trace_syscall_proc("sys_exec-before-exec_replace_current", current_process);
    KTM_CHECKPOINT(KTM_CP_PROCESS_EXEC);
    result = exec_replace_current(path_to_use,
                                  argv ? (char *const *)kernel_argv : NULL,
                                  envp ? (char *const *)kernel_envp : NULL);
  }
  else
  {
    result = kexecve(path_to_use,
                     argv ? (char *const *)kernel_argv : NULL,
                     envp ? (char *const *)kernel_envp : NULL);
  }

  /* Clean up copied strings */
  for (int i = 0; i < 256 && kernel_argv[i]; i++)
    kfree(kernel_argv[i]);
  for (int i = 0; i < 256 && kernel_envp[i]; i++)
    kfree(kernel_envp[i]);

  fase50_trace_syscall_proc("sys_exec-return", current_process);
  return result;
}
int64_t sys_fork(void)
{
  int64_t r;
  if (!current_process)
    return -ESRCH;

  r = fork();
  return r;
}

/*
 * sys_clone — Linux __NR_clone (56).
 * CLONE_THREAD|CLONE_VM: lightweight thread sharing the caller's mm.
 * Otherwise duplicates the address space like fork().
 */
int64_t sys_clone(unsigned long flags, void *stack, int *parent_tid,
                  int *child_tid, unsigned long tls)
{
  if (!current_process)
    return -ESRCH;

  if (flags & 0x00010000UL) /* CLONE_THREAD */
    return (int64_t)clone_thread(flags, stack, parent_tid, child_tid, tls);

  (void)stack;
  (void)parent_tid;
  (void)child_tid;
  (void)tls;
  return fork();
}


int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  if (current_process->mode == USER_MODE)
  {
    pid_t wait_pid = pid;
    int wait_opts = options;

    /*
     * Drop stale pipe/poll irq_frame resume state and read wait args from the
     * pt_regs snapshot (captured before any nested schedule on the shared
     * syscall stack can clobber the C ABI parameter slots).
     */
    process_clear_in_thread_syscall_block(current_process);
    wait_pid = (pid_t)process_syscall_arg(current_process, 0);
    wait_opts = (int)process_syscall_arg(current_process, 2);
    current_process->wait_options = wait_opts;
    pid = wait_pid;
    options = wait_opts;
  }

  if (IR0_DEBUG_WAIT)
  {
    klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] wait_begin parent=%x target=%x options=%x wait_target_pid=%x wait_options=%x wait_resume_child_pid=%x syscall_resume_rax=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid), (unsigned)((uint32_t)options), (unsigned)((uint32_t)current_process->wait_target_pid), (unsigned)((uint32_t)current_process->wait_options), (unsigned)((uint32_t)current_process->wait_resume_child_pid), (unsigned long long)(current_process->syscall_resume_rax));
    klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][sys_wait4] entry parent_pid=%x wait_pid=%x status_ptr=%llx options=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid), (unsigned long long)((uint64_t)(uintptr_t)status), (unsigned long long)((uint64_t)(unsigned int)options));
  }

  fase50_trace_syscall_proc("sys_wait4-entry", current_process);
#if IR0_DEBUG_PROC
  process_fase46_note_wait(current_process);
#endif
  {
    int64_t ret = process_wait(pid, status, options);

    if (current_process->state != PROCESS_BLOCKED)
      process_reset_blocked_syscall_state(current_process);

    if (IR0_DEBUG_WAIT)
    {
      klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] wait_return parent=%x ret=%llx wait_resume_child_pid=%x syscall_resume_rax=%llx status_write=%s", (unsigned)((uint32_t)current_process->task.pid), (unsigned long long)((uint64_t)ret), (unsigned)((uint32_t)current_process->wait_resume_child_pid), (unsigned long long)(current_process->syscall_resume_rax), ret > 0 ? "yes" : "no");
      klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][sys_wait4] return parent_pid=%x ret=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned long long)((uint64_t)ret));
    }
    fase50_trace_syscall_proc("sys_wait4-return", current_process);
    return ret;
  }
}

int64_t sys_waitpid(pid_t pid, int *status, int options)
{
  return sys_wait4(pid, status, options, NULL);
}

/**
 * sys_kill - Send a signal to a process
 * @pid: Target process ID
 * @signal: Signal number to send
 *
 * Returns: 0 on success, -1 on error
 */
int64_t sys_kill(pid_t pid, int signal)
{
  if (!current_process)
    return -ESRCH;

  /* Validate signal number */
  if (signal < 0 || signal >= _NSIG)
    return -EINVAL;

  /* Can't send signal to PID 0 or negative */
  if (pid <= 0)
    return -EINVAL;

  /* send_signal handles pending, wake, and default-fatal teardown. */
  if (send_signal(pid, signal) != 0)
    return -ESRCH; /* Process not found */

#if IR0_DEBUG_PROC
  {
    process_t *target = process_find_by_pid(pid);

    if (target)
    {
      klog_debug_fmt("SIGNAL", "[SIGTERM_AUDIT] sys_kill pid=%x sig=%x pending=%x state=%x", (unsigned)((uint32_t)pid), (unsigned)((uint32_t)signal), (unsigned)(target->signal_pending), (unsigned)((uint32_t)target->state));
    }
  }
#endif

  return 0;
}

/**
 * sys_sigaction - Set signal action (POSIX sigaction)
 * @signum: Signal number
 * @act: New signal action (can be NULL)
 * @oldact: Old signal action (can be NULL, output)
 *
 * Returns: 0 on success, -1 on error
 */
int64_t sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
  if (!current_process)
    return -ESRCH;

  /* Validate signal number */
  if (signum < 1 || signum >= _NSIG)
    return -EINVAL;

  /* Signals that cannot be caught */
  if (signum == SIGKILL || signum == SIGSTOP)
    return -EINVAL;

  /* Save old action if requested */
  if (oldact)
  {
    /* Validate oldact is in userspace (for USER_MODE processes) */
    if (validate_userspace_buffer(oldact, sizeof(struct sigaction)) != 0)
      return -EFAULT;

    oldact->sa_handler = current_process->signal_handlers[signum];
    ir0_sigset_set_low32(&oldact->sa_mask, current_process->signal_mask);
    oldact->sa_flags = 0;
    oldact->sa_restorer = NULL;
  }

  /* Set new action if provided */
  if (act)
  {
    /* Validate act is in userspace (for USER_MODE processes) */
    if (validate_userspace_buffer((const void *)act, sizeof(struct sigaction)) != 0)
      return -EFAULT;

    /* Register new handler */
    if (register_signal_handler(signum, act->sa_handler) != 0)
      return -EFAULT;

    current_process->signal_sa_mask[signum] = ir0_sigset_low32(&act->sa_mask);
  }

  return 0;
}
/*
 * sys_prctl — Linux prctl(2), no_new_privs subset.
 * PR_SET_NO_NEW_PRIVS is one-way: once set it cannot be cleared, and it makes
 * every later execve ignore set-user-ID / set-group-ID bits.
 */
int64_t sys_prctl(int option, unsigned long arg2, unsigned long arg3,
                  unsigned long arg4, unsigned long arg5)
{
  if (!current_process)
    return -ESRCH;

  switch (option)
  {
  case PR_SET_NO_NEW_PRIVS:
    if (arg2 != 1 || arg3 || arg4 || arg5)
      return -EINVAL;
    current_process->no_new_privs = 1;
    return 0;
  case PR_GET_NO_NEW_PRIVS:
    if (arg2 || arg3 || arg4 || arg5)
      return -EINVAL;
    return (int64_t)current_process->no_new_privs;
  default:
    return -EINVAL;
  }
}

int64_t sys_arch_prctl(int code, unsigned long addr)
{
  uint64_t fsbase;

  if (!current_process)
    return -ESRCH;

  if (code == ARCH_SET_FS)
  {
    process_tls_set(current_process, (uint64_t)addr);
    set_tls((uint64_t)addr);
    return 0;
  }

  if (code == ARCH_GET_FS)
  {
    if (addr == 0)
      return -EINVAL;
    if (validate_userspace_buffer((void *)(uintptr_t)addr, sizeof(uint64_t)) != 0)
      return -EFAULT;
    fsbase = process_tls_get(current_process);
    if (copy_to_user((void *)(uintptr_t)addr, &fsbase, sizeof(uint64_t)) != 0)
      return -EFAULT;
    return 0;
  }

  return -EINVAL;
}

/*
 * sys_set_tid_address - Linux set_tid_address(2) stub for musl.
 */
int64_t sys_set_tid_address(int *tidptr)
{
  if (!current_process)
    return -ESRCH;

  if (tidptr && validate_userspace_buffer(tidptr, sizeof(int)) != 0)
    return -EFAULT;

  current_process->set_tid_ptr = tidptr;
  return (int64_t)current_process->task.pid;
}
int64_t sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
                  int *uaddr2, int val3)
{
  int cur;
  int cmd;

  (void)timeout;
  (void)uaddr2;
  (void)val3;

  if (!current_process)
    return -ESRCH;

  if (uaddr && validate_userspace_buffer(uaddr, sizeof(int)) != 0)
    return -EFAULT;

  cmd = op & 0x7f; /* strip FUTEX_PRIVATE_FLAG / CLOCK_REALTIME bits */

  if (cmd == FUTEX_WAKE)
    return (int64_t)ir0_futex_wake(uaddr, val);

  if (cmd == FUTEX_WAIT)
  {
    if (copy_from_user(&cur, uaddr, sizeof(cur)) != 0)
      return -EFAULT;
    if (cur != val)
      return -EAGAIN;
    return (int64_t)ir0_futex_wait(current_process, uaddr, val);
  }

  return -ENOSYS;
}

/*
 * sys_getrandom - Non-cryptographic entropy for musl bring-up.
 */
int64_t sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
  uint8_t *p;
  size_t i;
  uint64_t seed;

  (void)flags;

  if (!current_process || !buf)
    return -EFAULT;

  if (buflen == 0)
    return 0;

  if (validate_userspace_buffer(buf, buflen) != 0)
    return -EFAULT;

  seed = clock_get_uptime_milliseconds() ^
         ((uint64_t)current_process->task.pid << 32);

  p = (uint8_t *)buf;
  for (i = 0; i < buflen; i++)
  {
    seed = seed * 6364136223846793005ULL + 1;
    p[i] = (uint8_t)(seed >> 33);
  }

  return (int64_t)buflen;
}
int64_t sys_prlimit64(pid_t pid, unsigned int resource, const void *new_limit,
                      void *old_limit)
{
  process_t *target = current_process;
  uint64_t lim[2];

  if (!current_process)
    return -ESRCH;

  if (pid != 0 && pid != (pid_t)current_process->task.pid)
    return -ESRCH;

  if (resource >= IR0_RLIM_NLIMITS)
    return -EINVAL;

  if (old_limit)
  {
    if (validate_userspace_buffer(old_limit, 16) != 0)
      return -EFAULT;
    lim[0] = target->rlimits[resource].rlim_cur;
    lim[1] = target->rlimits[resource].rlim_max;
    if (copy_to_user(old_limit, lim, 16) != 0)
      return -EFAULT;
  }

  if (new_limit)
  {
    if (validate_userspace_buffer((void *)new_limit, 16) != 0)
      return -EFAULT;
    if (copy_from_user(lim, new_limit, 16) != 0)
      return -EFAULT;
    if (lim[0] > lim[1])
      return -EINVAL;
    if (lim[1] > target->rlimits[resource].rlim_max &&
	!ir0_cred_is_root())
      return -EPERM;
    target->rlimits[resource].rlim_cur = lim[0];
    target->rlimits[resource].rlim_max = lim[1];
  }

  return 0;
}

int64_t sys_getrlimit(unsigned int resource, void *rlim)
{
  return sys_prlimit64(0, resource, NULL, rlim);
}

/**
 * sys_sigreturn - Return from signal handler (rt_sigreturn)
 * @ctx: Ignored on the musl/Linux x86-64 path (see below)
 *
 * Restores CPU state saved at signal delivery. Never returns on success.
 *
 * Linux x86-64: musl __restore_rt is just
 *   mov $__NR_rt_sigreturn, %eax; syscall
 * with no sigcontext* in rdi. Requiring a valid user pointer here made
 * rt_sigreturn return -EFAULT into the trampoline → SEGV (addr≈0x79) after
 * BusyBox ping's SIGALRM sendping handler.
 */
int64_t sys_sigreturn(struct sigcontext *ctx)
{
  struct sigcontext *restore_ctx;

  if (!current_process)
    return -ESRCH;

  if (current_process->mode != USER_MODE)
    return 0;

  /*
   * Prefer the kernel copy from handle_signals(). Only fall back to a
   * userspace pointer when that copy is missing (legacy/test callers).
   */
  restore_ctx = current_process->saved_context;
  if (!restore_ctx)
  {
    if (!ctx ||
        validate_userspace_buffer(ctx, sizeof(struct sigcontext)) != 0)
      return -EFAULT;
    restore_ctx = ctx;
  }

  arch_task_load_sigcontext(&current_process->task, restore_ctx);

  if (current_process->saved_context)
  {
    kfree(current_process->saved_context);
    current_process->saved_context = NULL;
  }

  /*
   * Must iretq into the restored frame. Sysret would return into the
   * restorer trampoline after the syscall insn, not to saved RIP.
   */
  process_restore_user_task_segments(current_process);
  current_process->irq_frame_saved = 0;
  current_process->coop_resched_resume = 0;
  current_process->want_kernel_ret = 0;
  current_process->signal_enter_pending = 0;
  arch_restore_user_fs_base();
  switch_to_user_task(&current_process->task);
  return 0;
}
