/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: io_syscalls.c
 * Description: I/O wait syscalls (poll/select/pause/nanosleep)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "io_syscalls.h"
#include "syscalls_glue.h"
#include "epoll_syscalls.h"
#include <ir0/syscalls_kernel.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/poll.h>
#include <ir0/select.h>
#include <ir0/time.h>
#include <ir0/console.h>
#include <ir0/devfs.h>
#include <ir0/sched.h>
#include <ir0/ktm.h>
#include <ir0/fcntl.h>
#include <ir0/clock.h>
#include <ir0/pseudo_fs.h>
#include <ir0/clock_wait.h>
#include <ir0/signals.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/paging.h>
#include <config.h>

#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>

#include <ir0/devfs.h>
#include <ir0/pseudo_fs.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <ir0/sock_inet_ioctl.h>
#include <ir0/ktm/klog.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <string.h>

static int devfs_initialized;

static int64_t sys_pipe_install(int pipefd[2], int flags);


extern void kernel_idle_poll(void);
extern void kernel_idle_poll_nosched(void);

/* poll(2): esperar eventos en fd. Bloquea hasta que haya datos o timeout.      */

#define MAX_POLL_WAITERS  16
#define MAX_POLL_NFDS     32

struct poll_waiter {
  process_t *proc;
  struct pollfd *user_fds;
  unsigned int nfds;
  struct pollfd *kfds;
  uint64_t timeout_expire;
  int woken;
  int ready_count;
};

static struct poll_waiter poll_waiters[MAX_POLL_WAITERS];

static int poll_wake_do(void);

/*
 * syscall_sleep_ms_locked - Block current task for @ms (kernel-side, no user copy).
 */
int64_t syscall_sleep_ms_locked(uint64_t ms)
{
	uint64_t now;
	int ret;

	if (!current_process)
		return -ESRCH;
	if (ms == 0)
		return 0;

	now = clock_get_uptime_milliseconds();
	ret = ir0_clock_wait_block_until(now + ms);
	if (ret < 0)
		return ret;
	return 0;
}

/**
 * fd_can_read_for - Non-blocking read readiness for @proc's fd table.
 */
int fd_can_read_for(process_t *proc, int fd)
{
  fd_entry_t *fd_table = proc ? process_fd_table(proc) : get_process_fd_table();
  pid_t pid = proc ? proc->task.pid : 0;

  if (fd == STDIN_FILENO && !stdio_is_redirected(fd_table, fd))
    return ir0_console_input_ready() ? 1 : 0;
  if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !stdio_is_redirected(fd_table, fd))
    return 0;
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_pseudo)
    return 1;
  if (fd_table[fd].is_devfs)
    return devfs_fd_can_read(fd_table[fd].dev_device_id, pid) ? 1 : 0;
  if (fd_table[fd].is_pipe && fd_table[fd].pipe_end == 0) {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    return (pipe && (pipe->count > 0 || pipe->writers <= 0)) ? 1 : 0;
  }
  if (fd_table[fd].is_socket && fd_table[fd].vfs_file &&
      sock_stream_is(fd_table[fd].vfs_file))
    return sock_stream_poll_readable((struct sock_stream *)fd_table[fd].vfs_file);
  if (fd_table[fd].is_socket && fd_table[fd].vfs_file &&
      sock_icmp_is(fd_table[fd].vfs_file))
    return sock_icmp_poll_readable((struct sock_icmp *)fd_table[fd].vfs_file);
  if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    return ir0_eventfd_poll_readable((struct ir0_eventfd *)fd_table[fd].vfs_file);
  if (fd_table[fd].is_timerfd && fd_table[fd].vfs_file)
    return ir0_timerfd_poll_readable((struct ir0_timerfd *)fd_table[fd].vfs_file);
  /*
   * Regular VFS / redirected files: poll-readable unless write-only.
   * O_RDONLY is 0 — never use `flags & O_RDONLY` (always false).
   */
  if ((fd_table[fd].flags & O_ACCMODE) != O_WRONLY)
    return 1;
  return 0;
}

/**
 * fd_can_read - Comprueba si el fd tiene datos para leer (sin bloquear).
 */
static int fd_can_read(int fd)
{
  return fd_can_read_for(current_process, fd);
}

/**
 * fd_can_write - Comprueba si se puede escribir en el fd.
 */
static int fd_can_write(int fd)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (fd == STDIN_FILENO && !stdio_is_redirected(fd_table, fd))
    return 0;
  if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !stdio_is_redirected(fd_table, fd))
    return 1;
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_pseudo)
    return (fd_table[fd].flags & (O_WRONLY | O_RDWR)) ? 1 : 0;
  if (fd_table[fd].is_pipe && fd_table[fd].pipe_end == 1) {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    return (pipe && pipe->readers > 0 && pipe->count < PIPE_SIZE) ? 1 : 0;
  }
  if (fd_table[fd].is_socket && fd_table[fd].vfs_file &&
      sock_stream_is(fd_table[fd].vfs_file))
    return sock_stream_poll_writable((struct sock_stream *)fd_table[fd].vfs_file);
  if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    return ir0_eventfd_poll_writable((struct ir0_eventfd *)fd_table[fd].vfs_file);
  if (fd_table[fd].is_timerfd && fd_table[fd].vfs_file)
    return 1;
  if (fd_table[fd].flags & (O_WRONLY | O_RDWR))
    return 1;
  return 0;
}

int fd_can_write_for(process_t *proc, int fd)
{
	process_t *saved = current_process;
	int ret;

	if (!proc)
		return fd_can_write(fd);
	current_process = proc;
	ret = fd_can_write(fd);
	current_process = saved;
	return ret;
}

/**
 * poll_check_ready_for - poll(2) readiness scan for @proc's fd table.
 */
static int poll_check_ready_for(process_t *proc, struct pollfd *fds, unsigned int nfds)
{
  int count = 0;
  unsigned int i;

  for (i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0)
      continue;
    if ((fds[i].events & POLLIN) && fd_can_read_for(proc, fds[i].fd))
      fds[i].revents |= POLLIN;
    if ((fds[i].events & POLLOUT) && fd_can_write_for(proc, fds[i].fd))
      fds[i].revents |= POLLOUT;
    if (fds[i].revents)
      count++;
  }
  return count;
}

/**
 * poll_check_ready - Rellena revents y devuelve cuántos fd tienen eventos.
 */
static int poll_check_ready(struct pollfd *fds, unsigned int nfds)
{
  return poll_check_ready_for(current_process, fds, nfds);
}

int64_t sys_poll(struct pollfd *user_fds, unsigned int nfds, int timeout_ms)
{
  if (!current_process)
    return -ESRCH;
  if (nfds > MAX_POLL_NFDS)
    return -EINVAL;

  /*
   * Linux: poll(NULL, 0, timeout) is a millisecond sleep (runsvdir iopause).
   */
  if (nfds == 0)
  {
    if (timeout_ms == 0)
      return 0;
    if (timeout_ms < 0)
    {
      for (;;)
      {
        int64_t ret = syscall_sleep_ms_locked(1000);

        if (ret < 0)
          return ret;
        if (current_process->signal_pending != 0)
          return 0;
      }
    }
    return syscall_sleep_ms_locked((uint64_t)timeout_ms);
  }

  if (!user_fds)
    return -EFAULT;
  if (validate_userspace_buffer(user_fds, nfds * sizeof(struct pollfd)) != 0)
    return -EFAULT;

  struct pollfd *kfds = (struct pollfd *)kmalloc_try(nfds * sizeof(struct pollfd));
  if (!kfds)
    return -ENOMEM;
  if (copy_from_user(kfds, user_fds, nfds * sizeof(struct pollfd)) != 0) {
    kfree(kfds);
    return -EFAULT;
  }

  int ready = poll_check_ready(kfds, nfds);
  if (ready > 0 || timeout_ms == 0) {
    if (copy_to_user(user_fds, kfds, nfds * sizeof(struct pollfd)) != 0) {
      kfree(kfds);
      return -EFAULT;
    }
    kfree(kfds);
    return ready;
  }

  uint64_t now = clock_get_uptime_milliseconds();
  uint64_t expire = (timeout_ms < 0) ? (uint64_t)-1 : (now + (uint64_t)timeout_ms);

  struct poll_waiter *w = NULL;
  unsigned int i;
  for (i = 0; i < MAX_POLL_WAITERS; i++)
  {
    if (!poll_waiters[i].proc)
    {
      w = &poll_waiters[i];
      break;
    }
  }
  if (!w)
  {
    kfree(kfds);
    return -EAGAIN;
  }
  w->proc = current_process;
  w->user_fds = user_fds;
  w->nfds = nfds;
  w->kfds = kfds;
  w->timeout_expire = expire;
  w->woken = 0;
  w->ready_count = 0;
  current_process->poll_waiter = w;
  process_arm_kernel_syscall_sleep(current_process);
  process_set_sched_state(current_process, PROCESS_BLOCKED);

  while (current_process->state == PROCESS_BLOCKED)
  {
    ir0_clock_wait_service_runqueue();
    if (current_process->state != PROCESS_BLOCKED)
      break;
  }

  process_restore_user_task_segments(current_process);

  if (current_process->syscall_interrupted)
  {
    current_process->syscall_interrupted = 0;
    if (w)
    {
      kfree(w->kfds);
      w->proc = NULL;
      w->kfds = NULL;
    }
    current_process->poll_waiter = NULL;
    return -EINTR;
  }

  w = (struct poll_waiter *)current_process->poll_waiter;
  current_process->poll_waiter = NULL;
  if (w)
  {
    ready = w->ready_count;
    if (ready >= 0 && w->kfds &&
        copy_to_user(user_fds, w->kfds, nfds * sizeof(struct pollfd)) != 0)
      ready = -EFAULT;
    kfree(w->kfds);
    w->proc = NULL;
    w->kfds = NULL;
    w->user_fds = NULL;
    w->nfds = 0;
    w->woken = 0;
    w->ready_count = 0;
  }
  return (int64_t)ready;
}

/**
 * io_select_timeout_ms - select(2) core with kernel timeout_ms.
 * @has_timeout: 0 = infinite wait; 1 = use @timeout_ms (may be 0).
 */
int64_t io_select_timeout_ms(int nfds, fd_set *user_r, fd_set *user_w,
			     fd_set *user_e, int timeout_ms, int has_timeout)
{
  fd_set kr;
  fd_set kw;
  fd_set ke;
  struct pollfd pfds[MAX_POLL_NFDS];
  unsigned int npoll = 0;
  uint64_t expire;
  int ready;
  unsigned int i;
  int fd;

  if (!current_process)
    return -ESRCH;
  /*
   * nfds is max_fd+1 (Linux/X11 often MaxClients=256), not the count of
   * interesting fds. Cap to IR0_FD_SETSIZE; compact interest into pfds[].
   */
  if (nfds < 0 || nfds > IR0_FD_SETSIZE)
    return -EINVAL;
  if (!user_r && !user_w && !user_e && !has_timeout)
    return -EINVAL;
  if (!has_timeout)
    timeout_ms = -1;

  IR0_FD_ZERO(&kr);
  IR0_FD_ZERO(&kw);
  IR0_FD_ZERO(&ke);

  if (user_r)
  {
    if (validate_userspace_buffer(user_r, sizeof(fd_set)) != 0)
      return -EFAULT;
    if (copy_from_user(&kr, user_r, sizeof(fd_set)) != 0)
      return -EFAULT;
  }
  if (user_w)
  {
    if (validate_userspace_buffer(user_w, sizeof(fd_set)) != 0)
      return -EFAULT;
    if (copy_from_user(&kw, user_w, sizeof(fd_set)) != 0)
      return -EFAULT;
  }
  if (user_e)
  {
    if (validate_userspace_buffer(user_e, sizeof(fd_set)) != 0)
      return -EFAULT;
    if (copy_from_user(&ke, user_e, sizeof(fd_set)) != 0)
      return -EFAULT;
  }

  if (nfds == 0)
  {
    if (!has_timeout)
      return -EINVAL;
    if (timeout_ms == 0)
      return 0;
    if (timeout_ms < 0)
    {
      for (;;)
      {
        int64_t ret = syscall_sleep_ms_locked(1000);

        if (ret < 0)
          return ret;
        if (current_process->signal_pending != 0)
          return 0;
      }
    }
    return syscall_sleep_ms_locked((uint64_t)timeout_ms);
  }

  for (fd = 0; fd < nfds; fd++)
  {
    short ev = 0;

    if (user_r && IR0_FD_ISSET(fd, &kr))
      ev |= POLLIN;
    if (user_w && IR0_FD_ISSET(fd, &kw))
      ev |= POLLOUT;
    if (user_e && IR0_FD_ISSET(fd, &ke))
      ev |= POLLERR | POLLHUP;
    if (!ev)
      continue;
    if (npoll >= MAX_POLL_NFDS)
      return -EINVAL;
    pfds[npoll].fd = fd;
    pfds[npoll].events = ev;
    pfds[npoll].revents = 0;
    npoll++;
  }

  if (npoll == 0)
  {
    if (timeout_ms == 0)
      return 0;
    if (timeout_ms < 0)
    {
      for (;;)
      {
        int64_t ret = syscall_sleep_ms_locked(1000);

        if (ret < 0)
          return ret;
        if (current_process->signal_pending != 0)
          return 0;
      }
    }
    return syscall_sleep_ms_locked((uint64_t)timeout_ms);
  }

  expire = (timeout_ms < 0) ? (uint64_t)-1
                            : (clock_get_uptime_milliseconds() + (uint64_t)timeout_ms);
  ready = 0;
  for (;;)
  {
    ready = poll_check_ready(pfds, npoll);
    if (ready > 0 || timeout_ms == 0)
      break;
    if (timeout_ms >= 0 && expire != (uint64_t)-1 &&
        clock_get_uptime_milliseconds() >= expire)
      break;
    if (current_process->signal_pending != 0)
      break;

    {
      int64_t ret = syscall_sleep_ms_locked(50);

      if (ret < 0)
        return ret;
    }
  }

  IR0_FD_ZERO(&kr);
  IR0_FD_ZERO(&kw);
  IR0_FD_ZERO(&ke);
  for (i = 0; i < npoll; i++)
  {
    fd = pfds[i].fd;
    if (user_r && (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)))
      IR0_FD_SET(fd, &kr);
    if (user_w && (pfds[i].revents & POLLOUT))
      IR0_FD_SET(fd, &kw);
    if (user_e && (pfds[i].revents & (POLLERR | POLLHUP)))
      IR0_FD_SET(fd, &ke);
  }

  if (user_r && copy_to_user(user_r, &kr, sizeof(fd_set)) != 0)
    return -EFAULT;
  if (user_w && copy_to_user(user_w, &kw, sizeof(fd_set)) != 0)
    return -EFAULT;
  if (user_e && copy_to_user(user_e, &ke, sizeof(fd_set)) != 0)
    return -EFAULT;

  return (int64_t)ready;
}

/**
 * sys_select - POSIX select(2) via poll readiness checks (tier-1 / libc compat).
 */
int64_t sys_select(int nfds, fd_set *user_r, fd_set *user_w, fd_set *user_e,
                   struct timeval *user_tv)
{
  struct timeval tv;
  int timeout_ms = -1;
  int has_timeout = 0;

  if (user_tv)
  {
    if (validate_userspace_buffer(user_tv, sizeof(tv)) != 0)
      return -EFAULT;
    if (copy_from_user(&tv, user_tv, sizeof(tv)) != 0)
      return -EFAULT;
    if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000)
      return -EINVAL;
    timeout_ms = (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    has_timeout = 1;
  }

  return io_select_timeout_ms(nfds, user_r, user_w, user_e, timeout_ms,
			      has_timeout);
}

static int poll_wake_do(void)
{
  uint64_t now = clock_get_uptime_milliseconds();
  unsigned int i;
  int woke = 0;

  for (i = 0; i < MAX_POLL_WAITERS; i++) {
    struct poll_waiter *w = &poll_waiters[i];

    if (!w->proc)
      continue;
    int ready = poll_check_ready_for(w->proc, w->kfds, w->nfds);
    int timeout = (w->timeout_expire != (uint64_t)-1 && now >= w->timeout_expire);
    if (ready > 0 || timeout) {
      w->ready_count = ready;
      w->woken = 1;
      if (w->proc->state == PROCESS_BLOCKED)
      {
        process_set_sched_state(w->proc, PROCESS_READY);
        sched_add_process(w->proc);
        sched_promote_process(w->proc);
      }
      woke = 1;
    }
  }
  return woke;
}

int poll_wake_check_nosched(void)
{
	return poll_wake_do();
}

void poll_wake_check(void)
{
  if (poll_wake_do())
    sched_schedule_next();
}

void syscall_wake_blocked_on_child(process_t *parent)
{
	int was_blocked;
	int wait_armed;

	if (!parent)
		return;

	was_blocked = (parent->state == PROCESS_BLOCKED);
	wait_armed = (parent->irq_frame_saved && !parent->poll_waiter);
	if (!was_blocked && !wait_armed && !parent->poll_waiter)
		return;

	ir0_clock_wait_disarm(parent);

	if (parent->poll_waiter)
		(void)poll_wake_do();

	if (was_blocked || wait_armed)
	{
		process_set_sched_state(parent, PROCESS_READY);
		sched_add_process(parent);
		sched_promote_process(parent);
	}
}

/*
 * syscall_poll_finish_blocked_resume - Copy poll revents and set syscall retval.
 * Called from switch_to irq resume with parent CR3 loaded.
 */
void syscall_poll_finish_blocked_resume(process_t *proc)
{
	struct poll_waiter *w;
	int ready;

	if (!proc)
		return;

	w = (struct poll_waiter *)proc->poll_waiter;
	if (!w || !w->kfds)
		return;

	ready = w->ready_count;
	if (ready < 0)
		ready = poll_check_ready_for(proc, w->kfds, w->nfds);
	if (ready < 0)
		ready = 0;

	if (w->user_fds && process_pgd(proc))
	{
		if (copy_to_user_region_in_directory(process_pgd(proc),
						     (uintptr_t)w->user_fds,
						     w->kfds,
						     w->nfds * sizeof(struct pollfd)) != 0)
			ready = -EFAULT;
	}

	kfree(w->kfds);
	w->proc = NULL;
	w->kfds = NULL;
	w->user_fds = NULL;
	w->nfds = 0;
	w->woken = 0;
	w->ready_count = 0;
	proc->poll_waiter = NULL;
	proc->poll_resume_via_arch = 0;
}

/**
 * sys_pause - Wait until a signal arrives (POSIX pause(2)).
 */
int64_t sys_pause(void)
{
  if (!current_process)
    return -ESRCH;

  for (;;)
  {
    if (signals_pause_should_interrupt(current_process))
    {
      handle_signals();
      return -EINTR;
    }

    process_set_sched_state(current_process, PROCESS_BLOCKED);
    process_arm_kernel_syscall_sleep(current_process);
    while (current_process->state == PROCESS_BLOCKED)
    {
      ir0_clock_wait_service_runqueue();
      if (current_process->state != PROCESS_BLOCKED)
        break;
    }
  }
}

/**
 * sys_nanosleep - Sleep for specified time (POSIX, OSDev Time And Date)
 * @req: Requested sleep duration (seconds + nanoseconds)
 * @rem: Remaining time if interrupted (optional, can be NULL)
 *
 * Blocks the process until the requested time has elapsed.
 * Resolution is limited to clock tick (~1ms). EINTR not yet implemented.
 */
int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
  if (!current_process || !req)
    return -EFAULT;
  if (validate_userspace_buffer((void *)req, sizeof(struct timespec)) != 0)
    return -EFAULT;
  if (rem && validate_userspace_buffer(rem, sizeof(struct timespec)) != 0)
    return -EFAULT;

  /* Convert to milliseconds; clamp tv_nsec to 0-999999999 */
  int64_t sec = req->tv_sec;
  long nsec = req->tv_nsec;
  if (sec < 0 || nsec < 0 || nsec > 999999999)
    return -EINVAL;

  uint64_t ms = (uint64_t)sec * 1000UL + (uint64_t)(nsec / 1000000);
  int64_t ret = syscall_sleep_ms_locked(ms);

  if (ret < 0)
    return ret;
  if (rem)
  {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

#define MAX_PIPE_WAITERS 32

struct pipe_waiter
{
	process_t *proc;
	pipe_t *pipe;
	int waiting_read;
};

static struct pipe_waiter pipe_waiters[MAX_PIPE_WAITERS];
static uint64_t fase48_fd_created;
static uint64_t fase48_fd_destroyed;
static uint64_t fase48_blocked_readers;
static uint64_t fase48_blocked_writers;
void fase50b_dump_bytes(const char *label, const void *buf, size_t n)
{
#if CONFIG_DEBUG_FASE50
	const uint8_t *b = (const uint8_t *)buf;
	size_t i;

	klog_debug_fmt("KERN", "%s n=%llx hex=", label ? label : "[IR0DBG50B][BYTES]", (unsigned long long)((uint64_t)n));
	for (i = 0; i < n && i < 32; i++)
	{
		if (i > 0)
			klog_debug_fmt("KERN", " %llx", (unsigned long long)((uint64_t)b[i]));
	}
	klog_debug("KERN", " ascii=");
	for (i = 0; i < n && i < 32; i++)
	{
		char c = (char)b[i];

		if (c >= 32 && c <= 126)
			serial_putchar(c);
		else
			klog_debug("KERN", ".");
	}
#else
	(void)label;
	(void)buf;
	(void)n;
#endif
}

#if CONFIG_DEBUG_FASE50
static int fase50b_peek_user(uint64_t *pml4, uintptr_t user_addr,
			     uint8_t *scratch, size_t n)
{
	size_t i;

	if (!pml4 || !scratch || n == 0)
		return -1;

	for (i = 0; i < n; i++)
	{
		uintptr_t page = user_addr & (uintptr_t)PAGE_FRAME_MASK;
		uint64_t *pte = paging_get_pte(pml4, page);
		uintptr_t phys;
		uint8_t byte;

		if (!pte || !(*pte & PAGE_PRESENT))
			return -1;

		phys = (uintptr_t)(*pte & PAGE_PTE_PFN_MASK);
		byte = *(const uint8_t *)(phys + (user_addr & 0xFFFU));
		scratch[i] = byte;
		user_addr++;
	}
	return 0;
}
#else
/* Staged pipe wake removed; peek helper only used under DEBUG_FASE50. */
#endif
void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
			 uint64_t *blocked_readers, uint64_t *blocked_writers)
{
	if (created)
		*created = fase48_fd_created;
	if (destroyed)
		*destroyed = fase48_fd_destroyed;
	if (blocked_readers)
		*blocked_readers = fase48_blocked_readers;
	if (blocked_writers)
		*blocked_writers = fase48_blocked_writers;
}

void fase48_note_fd_created(void)
{
#if CONFIG_DEBUG_FASE50
	fase48_fd_created++;
#endif
}

void fase48_note_fd_destroyed(void)
{
#if CONFIG_DEBUG_FASE50
	fase48_fd_destroyed++;
#endif
}

int pipe_wait(process_t *proc, pipe_t *pipe, int waiting_read)
{
	unsigned int i;
	int slot = -1;

	if (!proc || !pipe)
		return -EINVAL;

	for (i = 0; i < MAX_PIPE_WAITERS; i++)
	{
		if (!pipe_waiters[i].proc)
		{
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return -EAGAIN;

	pipe_waiters[slot].proc = proc;
	pipe_waiters[slot].pipe = pipe;
	pipe_waiters[slot].waiting_read = waiting_read;
	if (waiting_read)
		fase48_blocked_readers++;
	else
		fase48_blocked_writers++;
	if (waiting_read)
		pipe_fase49_note_read_sleep(pipe);
#if CONFIG_DEBUG_FASE50
	if (proc->mode == USER_MODE && waiting_read)
		klog_debug_fmt("PIPE", "[IR0DBG50B][READ_BLOCK] pid=%x fd=%llx rsi=%llx rdx=%llx pipe_id=%llx", (unsigned)((uint32_t)proc->task.pid), (unsigned long long)(process_syscall_arg(proc, 0)), (unsigned long long)(process_syscall_arg(proc, 1)), (unsigned long long)(process_syscall_arg(proc, 2)), (unsigned long long)(pipe ? pipe->pipe_id : 0));
#endif

	/*
	 * Portable in-syscall sleep (same contract as tty/poll):
	 * process_arm_kernel_syscall_sleep only. Do NOT arm
	 * process_arm_blocked_syscall_resume(..., 0) — that is wait4/user-iret
	 * and triggers x86-only arch_switch rax==0 special-casing.
	 */
	for (;;)
	{
		if (waiting_read)
		{
			if (pipe->count > 0 || pipe->writers <= 0)
				break;
		}
		else
		{
			if (pipe->readers <= 0 || pipe->count < PIPE_SIZE)
				break;
		}

		if (proc->mode == USER_MODE)
			process_arm_kernel_syscall_sleep(proc);
		process_set_sched_state(proc, PROCESS_BLOCKED);
		enable_interrupts();
		sched_schedule_next();

		if (proc->state != PROCESS_BLOCKED)
			break;
	}

	if (proc->mode == USER_MODE)
		process_restore_user_task_segments(proc);

	if (waiting_read)
		pipe_fase49_note_read_wake(pipe);
	if (pipe_waiters[slot].proc == proc)
	{
		pipe_waiters[slot].proc = NULL;
		pipe_waiters[slot].pipe = NULL;
		pipe_waiters[slot].waiting_read = 0;
	}
	return 0;
}

void pipe_wake_all(pipe_t *pipe)
{
	unsigned int i;

	if (!pipe)
		return;

	for (i = 0; i < MAX_PIPE_WAITERS; i++)
	{
		struct pipe_waiter *w = &pipe_waiters[i];
		process_t *proc;

		if (!w->proc || w->pipe != pipe)
			continue;

		if (w->waiting_read)
		{
			if (pipe->count > 0 || pipe->writers <= 0)
			{
				pipe_fase49_note_read_wake(pipe);
				/*
				 * In-syscall resume only (matches tty wake). Do not
				 * stage a user read here — conflicts with kernel sleep.
				 */
				proc = w->proc;
				if (proc->mode == USER_MODE)
					proc->irq_frame_saved = 0;
				process_set_sched_state(proc, PROCESS_READY);
				sched_promote_process(proc);
				w->proc = NULL;
				w->pipe = NULL;
				w->waiting_read = 0;
			}
		}
		else
		{
			if (pipe->readers <= 0 || pipe->count < PIPE_SIZE)
			{
				pipe_fase49_note_write_wake(pipe);
				proc = w->proc;
				if (proc->mode == USER_MODE)
					proc->irq_frame_saved = 0;
				process_set_sched_state(proc, PROCESS_READY);
				sched_promote_process(proc);
				w->proc = NULL;
				w->pipe = NULL;
				w->waiting_read = 0;
			}
		}
	}
}

void pipe_wake_check(void)
{
	unsigned int i;

	for (i = 0; i < MAX_PIPE_WAITERS; i++)
	{
		struct pipe_waiter *w = &pipe_waiters[i];
		pipe_t *pipe;
		process_t *proc;

		if (!w->proc || !w->pipe)
			continue;

		pipe = w->pipe;
		if (w->waiting_read)
		{
			if (pipe->count > 0 || pipe->writers <= 0)
			{
				pipe_fase49_note_read_wake(pipe);
				proc = w->proc;
				if (proc->mode == USER_MODE)
					proc->irq_frame_saved = 0;
				process_set_sched_state(proc, PROCESS_READY);
				sched_promote_process(proc);
				w->proc = NULL;
				w->pipe = NULL;
				w->waiting_read = 0;
			}
		}
		else
		{
			if (pipe->readers <= 0 || pipe->count < PIPE_SIZE)
			{
				pipe_fase49_note_write_wake(pipe);
				proc = w->proc;
				if (proc->mode == USER_MODE)
					proc->irq_frame_saved = 0;
				process_set_sched_state(proc, PROCESS_READY);
				sched_promote_process(proc);
				w->proc = NULL;
				w->pipe = NULL;
				w->waiting_read = 0;
			}
		}
	}
}
void ensure_devfs_init(void)
{
  if (!devfs_initialized)
  {
    devfs_init();
    devfs_initialized = 1;
  }
}

int stdio_is_redirected(fd_entry_t *fd_table, int fd)
{
  if (!fd_table || fd < STDIN_FILENO || fd > STDERR_FILENO)
    return 0;
  if (!fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_devfs)
    return 1;
  if (fd_table[fd].is_pipe)
    return 1;
  if (fd_table[fd].vfs_file)
    return 1;
  return 0;
}
int64_t sys_dup(int oldfd)
{
  if (!current_process)
    return -ESRCH;

  if (oldfd < 0 || oldfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[oldfd].in_use)
    return -EBADF;

  for (int i = 0; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
      return sys_dup2(oldfd, i);
  }
  return -EMFILE;
}

fd_entry_t *get_process_fd_table(void)
{
  if (!current_process)
    return NULL;
  /*
   * La tabla se inicializa en spawn() / process create; no usar un
   * flag estático global (rompía si el primer syscall no era del proceso init).
   */
  return process_fd_table(current_process);
}
int64_t sys_ioctl(int fd, uint64_t request, void *arg)
{
  /* Linux ioctl cmd is 32-bit; musl may sign-extend into the register. */
  request = (uint32_t)request;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_devfs)
  {
    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node_by_id(fd_table[fd].dev_device_id);

    if (!node || !node->ops || !node->ops->ioctl)
      return -ENOTTY;
    /* Handlers perform sized copy_to/from_user; do not require 256B here
     * (stack ioctl args near page end would false-fail). */
    return node->ops->ioctl(&node->entry, request, arg);
  }

  /* AF_INET socket: Linux SIOCGIF* for BusyBox ifconfig (read-only). */
  if (fd_table[fd].is_socket)
  {
    int64_t rc = sock_inet_ioctl(request, arg);

    if (rc != -ENOTTY)
      return rc;
  }

  /* Unredirected stdio: console tty ioctls (TIOCGWINSZ / termios). */
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO &&
      !stdio_is_redirected(fd_table, fd))
  {
    if (request == IR0_CONSOLE_TIOCGWINSZ)
      return ir0_console_ioctl_winsize(arg);
    if (request == IR0_CONSOLE_TIOCSWINSZ)
      return ir0_console_ioctl_winsize_set(arg);
    if (request == IR0_TIOCSCTTY)
      return 0;
    if (request == IR0_TIOCSPGRP)
    {
      pid_t pg;

      if (!arg)
	return -EINVAL;
      if (copy_from_user(&pg, arg, sizeof(pg)) != 0)
	return -EFAULT;
      return ir0_console_set_fg_pgid((int32_t)pg);
    }
    if (request == IR0_TIOCGPGRP)
    {
      pid_t pg;

      if (!arg)
	return -EINVAL;
      pg = current_process->pgid > 0 ? current_process->pgid
				     : (pid_t)current_process->task.pid;
      if (copy_to_user(arg, &pg, sizeof(pg)) != 0)
	return -EFAULT;
      return 0;
    }
    if (request == IR0_CONSOLE_TCFLSH)
    {
      tty_flush_input();
      return 0;
    }
    if (request == IR0_CONSOLE_FIONREAD)
    {
      int avail;

      if (!arg)
	return -EINVAL;
      avail = tty_input_bytes_available();
      if (copy_to_user(arg, &avail, sizeof(avail)) != 0)
	return -EFAULT;
      return 0;
    }
    if (request == IR0_CONSOLE_TCGETS || request == IR0_CONSOLE_TCSETS ||
	request == IR0_CONSOLE_TCSETSW || request == IR0_CONSOLE_TCSETSF)
    {
      struct ir0_termios termios;
      int ret;

      if (request == IR0_CONSOLE_TCGETS)
      {
	if (!arg)
	  return -EINVAL;
	ret = tty_ioctl_termios_kernel(IR0_CONSOLE_TCGETS, &termios);
	if (ret != 0)
	  return ret;
	if (copy_to_user(arg, &termios, sizeof(termios)) != 0)
	  return -EFAULT;
	return 0;
      }
      if (!arg)
	return -EINVAL;
      if (copy_from_user(&termios, arg, sizeof(termios)) != 0)
	return -EFAULT;
      return tty_ioctl_termios_kernel(request, &termios);
    }
  }

  return -ENOTTY;
}

int64_t sys_close(int fd)
{
  if (!current_process)
    return -ESRCH;

  /* Prefer process fd_table (is_pseudo binds) over legacy global FD bases. */
  if (fd >= 0 && fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fd_table = get_process_fd_table();

    if (fd_table && fd_table[fd].in_use && fd_table[fd].is_pseudo &&
	fd_table[fd].vfs_file)
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fd_table[fd].vfs_file;

      if (bind->refs > 0)
	bind->refs--;
      if (bind->refs == 0)
      {
	(void)pseudo_fs_release_ops((const pseudo_fs_ops_t *)bind->ops,
				    bind->ctx, bind->dynamic);
	kfree(bind);
      }
      fd_table[fd].vfs_file = NULL;
      fd_table[fd].in_use = false;
      fd_table[fd].is_pseudo = false;
      fd_table[fd].path[0] = '\0';
      return 0;
    }
  }

  /* Legacy host/registry virtual fds (PSEUDO_FS_*_FD_BASE). */
  if (pseudo_fs_find_by_fd(fd))
    return pseudo_fs_close_fd(fd);

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  {
    int was_pipe = fd_table[fd].is_pipe ? 1 : 0;
    int was_devfs = fd_table[fd].is_devfs ? 1 : 0;

    /*
     * Linux allows close(0/1/2) on the console slots. BusyBox wget -O -
     * ends with xclose(1); refusing with EBADF prints "close failed".
     */
    if (fd <= 2 && !was_pipe && !was_devfs && !fd_table[fd].vfs_file)
    {
      fd_table[fd].in_use = false;
      fd_table[fd].flags = 0;
      return 0;
    }

    if (was_devfs)
    {
      devfs_node_t *node = devfs_find_node_by_id(fd_table[fd].dev_device_id);

      if (fd_table[fd].vfs_file &&
	  devfs_node_wants_text_snap(fd_table[fd].dev_device_id))
      {
	devfs_text_snap_release((devfs_text_snap_t *)fd_table[fd].vfs_file);
	fd_table[fd].vfs_file = NULL;
      }
      if (node)
        devfs_close_node(node);
    }
    /* Check if this is a pipe */
    else if (fd_table[fd].is_pipe && fd_table[fd].vfs_file)
    {
      pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;

      pipe_fase49_fd_trace((uint32_t)current_process->task.pid, fd, pipe,
			   fd_table[fd].pipe_end, pipe->fd_refs, "CLOSE");
      /* pipe_close_end wakes waiters then frees on last ref. */
      pipe_close_end(pipe, fd_table[fd].pipe_end);
      fd_table[fd].vfs_file = NULL;
    }
    else if (fd_table[fd].is_socket && fd_table[fd].vfs_file)
    {
      if (sock_stream_is(fd_table[fd].vfs_file))
        sock_stream_release((struct sock_stream *)fd_table[fd].vfs_file);
      else if (sock_icmp_is(fd_table[fd].vfs_file))
        sock_icmp_release((struct sock_icmp *)fd_table[fd].vfs_file);
      else if (!sock_stream_is_slot(fd_table[fd].vfs_file))
        sock_udp_release((struct sock_udp *)fd_table[fd].vfs_file);
      fd_table[fd].vfs_file = NULL;
    }
    else if (fd_table[fd].is_epoll && fd_table[fd].vfs_file)
    {
      epoll_release_fd(fd_table[fd].vfs_file);
      fd_table[fd].vfs_file = NULL;
      fd_table[fd].is_epoll = false;
    }
    else if (fd_table[fd].is_memfd && fd_table[fd].vfs_file)
    {
      ir0_memfd_release((struct ir0_memfd *)fd_table[fd].vfs_file);
      fd_table[fd].vfs_file = NULL;
    }
    else if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    {
      ir0_eventfd_release((struct ir0_eventfd *)fd_table[fd].vfs_file);
      fd_table[fd].vfs_file = NULL;
    }
    else if (fd_table[fd].is_timerfd && fd_table[fd].vfs_file)
    {
      ir0_timerfd_release((struct ir0_timerfd *)fd_table[fd].vfs_file);
      fd_table[fd].vfs_file = NULL;
    }
    else if (fd_table[fd].vfs_file)
    {
      struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;

      vfs_close(vfs_file);
      fd_table[fd].vfs_file = NULL;
    }

    fd_table[fd].in_use = false;
    fd_table[fd].is_pipe = false;
    fd_table[fd].is_socket = false;
    fd_table[fd].is_epoll = false;
    fd_table[fd].is_memfd = false;
    fd_table[fd].is_eventfd = false;
    fd_table[fd].is_timerfd = false;
    fd_table[fd].pipe_end = -1;
    fd_table[fd].is_devfs = false;
    fd_table[fd].is_pseudo = false;
    fd_table[fd].dev_device_id = 0;
    fd_table[fd].path[0] = '\0';
    fd_table[fd].flags = 0;
    fd_table[fd].fd_flags = 0;
    fd_table[fd].offset = 0;
    fase48_note_fd_destroyed();
  }

  return 0;
}

int64_t process_close_fd(process_t *proc, int fd)
{
  process_t *saved = current_process;
  int64_t ret;

  if (!proc)
    return -ESRCH;

  current_process = proc;
  ret = sys_close(fd);
  current_process = saved;
  return ret;
}

int64_t sys_lseek(int fd, off_t offset, int whence)
{
  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  /* Pseudo-fs binds use fd_table.offset via sys_read; allow SEEK_SET/CUR. */
  if (fd_table[fd].is_pseudo)
  {
    off_t new_offset;

    if (whence == SEEK_SET)
      new_offset = offset;
    else if (whence == SEEK_CUR)
      new_offset = (off_t)fd_table[fd].offset + offset;
    else
      return -ESPIPE;
    if (new_offset < 0)
      return -EINVAL;
    fd_table[fd].offset = (uint64_t)new_offset;
    return new_offset;
  }

  if (fd <= 2) {
    off_t new_offset;
    if (whence == SEEK_SET)
      new_offset = offset;
    else if (whence == SEEK_CUR)
      new_offset = (off_t)fd_table[fd].offset + offset;
    else
      return -ESPIPE;
    if (new_offset < 0)
      return -EINVAL;
    fd_table[fd].offset = (uint64_t)new_offset;
    return new_offset;
  }

  if (fd_table[fd].vfs_file) {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    off_t result = vfs_lseek(vfs_file, offset, whence);
    if (result < 0)
      return result;
    fd_table[fd].offset = (uint64_t)result;
    return result;
  }

  /* No VFS handle: stat-based fallback */
  stat_t st;
  if (vfs_stat(fd_table[fd].path, &st) != 0)
    return -EBADF;

  off_t new_offset;
  switch (whence) {
  case SEEK_SET: new_offset = offset; break;
  case SEEK_CUR: new_offset = (off_t)fd_table[fd].offset + offset; break;
  case SEEK_END: new_offset = st.st_size + offset; break;
  default: return -EINVAL;
  }
  if (new_offset < 0)
    return -EINVAL;
  fd_table[fd].offset = (uint64_t)new_offset;
  return new_offset;
}

int64_t sys_dup2(int oldfd, int newfd)
{
  if (!current_process)
    return -ESRCH;

  if (oldfd < 0 || oldfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;
  if (newfd < 0 || newfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  if (oldfd == newfd)
    return newfd;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[oldfd].in_use)
    return -EBADF;

  if (fd_table[newfd].in_use && newfd != oldfd)
  {
    /*
     * Each fd slot owns at most one kernel object (pipe or vfs_file). POSIX
     * dup2 closes the previous occupant of newfd before reassigning; skipping
     * that leaks struct vfs_file and breaks refcounting.
     */
    if (fd_table[newfd].is_pipe && fd_table[newfd].vfs_file)
    {
      pipe_t *p = (pipe_t *)fd_table[newfd].vfs_file;

      pipe_close_end(p, fd_table[newfd].pipe_end);
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].is_devfs)
    {
      devfs_node_t *node = devfs_find_node_by_id(fd_table[newfd].dev_device_id);

      if (fd_table[newfd].vfs_file &&
	  devfs_node_wants_text_snap(fd_table[newfd].dev_device_id))
      {
	devfs_text_snap_release((devfs_text_snap_t *)fd_table[newfd].vfs_file);
	fd_table[newfd].vfs_file = NULL;
      }
      if (node)
        devfs_close_node(node);
    }
    else if (fd_table[newfd].is_pseudo && fd_table[newfd].vfs_file)
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fd_table[newfd].vfs_file;

      if (bind->refs > 0)
	bind->refs--;
      if (bind->refs == 0)
      {
	(void)pseudo_fs_release_ops((const pseudo_fs_ops_t *)bind->ops,
				    bind->ctx, bind->dynamic);
	kfree(bind);
      }
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].is_memfd && fd_table[newfd].vfs_file)
    {
      ir0_memfd_release((struct ir0_memfd *)fd_table[newfd].vfs_file);
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].is_eventfd && fd_table[newfd].vfs_file)
    {
      ir0_eventfd_release((struct ir0_eventfd *)fd_table[newfd].vfs_file);
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].is_timerfd && fd_table[newfd].vfs_file)
    {
      ir0_timerfd_release((struct ir0_timerfd *)fd_table[newfd].vfs_file);
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].is_socket && fd_table[newfd].vfs_file)
    {
      if (sock_stream_is(fd_table[newfd].vfs_file))
	sock_stream_release((struct sock_stream *)fd_table[newfd].vfs_file);
      else if (sock_icmp_is(fd_table[newfd].vfs_file))
	sock_icmp_release((struct sock_icmp *)fd_table[newfd].vfs_file);
      else if (!sock_stream_is_slot(fd_table[newfd].vfs_file))
	sock_udp_release((struct sock_udp *)fd_table[newfd].vfs_file);
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].vfs_file)
    {
      vfs_close((struct vfs_file *)fd_table[newfd].vfs_file);
      fd_table[newfd].vfs_file = NULL;
    }
    fd_table[newfd].in_use = false;
    fd_table[newfd].path[0] = '\0';
    fd_table[newfd].flags = 0;
    fd_table[newfd].fd_flags = 0;
    fd_table[newfd].offset = 0;
    fd_table[newfd].is_pipe = false;
    fd_table[newfd].pipe_end = -1;
    fd_table[newfd].is_devfs = false;
    fd_table[newfd].is_pseudo = false;
    fd_table[newfd].is_socket = false;
    fd_table[newfd].is_memfd = false;
    fd_table[newfd].is_eventfd = false;
    fd_table[newfd].is_timerfd = false;
    fd_table[newfd].dev_device_id = 0;
    fase48_note_fd_destroyed();
  }

  fd_table[newfd].in_use = true;
  strncpy(fd_table[newfd].path, fd_table[oldfd].path, sizeof(fd_table[newfd].path) - 1);
  fd_table[newfd].path[sizeof(fd_table[newfd].path) - 1] = '\0';
  fd_table[newfd].flags = fd_table[oldfd].flags;
  /*
   * Linux dup(2)/dup2(2): the close-on-exec flag for the duplicate is always
   * cleared (man 2 dup). dup3() is the path that can set O_CLOEXEC atomically.
   */
  fd_table[newfd].fd_flags = 0;
  fd_table[newfd].offset = fd_table[oldfd].offset;
  fd_table[newfd].is_pipe = fd_table[oldfd].is_pipe;
  fd_table[newfd].pipe_end = fd_table[oldfd].pipe_end;
  fd_table[newfd].is_devfs = fd_table[oldfd].is_devfs;
  fd_table[newfd].dev_device_id = fd_table[oldfd].dev_device_id;
  fd_table[newfd].is_pseudo = fd_table[oldfd].is_pseudo;
  fd_table[newfd].is_socket = fd_table[oldfd].is_socket;
  fd_table[newfd].is_memfd = fd_table[oldfd].is_memfd;
  fd_table[newfd].is_eventfd = fd_table[oldfd].is_eventfd;
  fd_table[newfd].is_timerfd = fd_table[oldfd].is_timerfd;

  /*
   * Regular files now share one open file description (vfs_file with refcount),
   * matching Unix dup/dup2 offset-sharing semantics.
   * Pipes share one pipe_t and keep per-end counts.
   */
  if (fd_table[oldfd].is_devfs)
  {
    devfs_node_t *node = devfs_find_node_by_id(fd_table[oldfd].dev_device_id);

    if (node)
      node->ref_count++;
    if (fd_table[oldfd].vfs_file &&
	devfs_node_wants_text_snap(fd_table[oldfd].dev_device_id))
    {
      devfs_text_snap_acquire((devfs_text_snap_t *)fd_table[oldfd].vfs_file);
      fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
    }
    else
      fd_table[newfd].vfs_file = NULL;
  }
  else if (fd_table[oldfd].is_pipe)
  {
    pipe_acquire_end((pipe_t *)fd_table[oldfd].vfs_file, fd_table[oldfd].pipe_end);
    fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
  }
  else if (fd_table[oldfd].is_pseudo && fd_table[oldfd].vfs_file)
  {
    pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fd_table[oldfd].vfs_file;

    bind->refs++;
    fd_table[newfd].vfs_file = bind;
  }
  else if (fd_table[oldfd].is_memfd && fd_table[oldfd].vfs_file)
  {
    ir0_memfd_acquire((struct ir0_memfd *)fd_table[oldfd].vfs_file);
    fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
  }
  else if (fd_table[oldfd].is_eventfd && fd_table[oldfd].vfs_file)
  {
    ir0_eventfd_acquire((struct ir0_eventfd *)fd_table[oldfd].vfs_file);
    fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
  }
  else if (fd_table[oldfd].is_timerfd && fd_table[oldfd].vfs_file)
  {
    ir0_timerfd_acquire((struct ir0_timerfd *)fd_table[oldfd].vfs_file);
    fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
  }
  else if (fd_table[oldfd].is_socket && fd_table[oldfd].vfs_file)
  {
    /*
     * BusyBox ping does xmove_fd(raw_sock, 0). Must not vfs_file_acquire the
     * sock_* object — that corrupts magic and sendto falls through as UDP.
     */
    if (sock_stream_is(fd_table[oldfd].vfs_file))
      sock_stream_acquire((struct sock_stream *)fd_table[oldfd].vfs_file);
    else if (sock_icmp_is(fd_table[oldfd].vfs_file))
      sock_icmp_acquire((struct sock_icmp *)fd_table[oldfd].vfs_file);
    else if (!sock_stream_is_slot(fd_table[oldfd].vfs_file))
      sock_udp_acquire((struct sock_udp *)fd_table[oldfd].vfs_file);
    fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
  }
  else if (fd_table[oldfd].vfs_file)
  {
    struct vfs_file *shared = (struct vfs_file *)fd_table[oldfd].vfs_file;

    vfs_file_acquire(shared);
    fd_table[newfd].vfs_file = shared;
  }
  else
  {
    fd_table[newfd].vfs_file = NULL;
  }

  if (fd_table[newfd].is_pipe && fd_table[newfd].vfs_file)
  {
    pipe_t *pip = (pipe_t *)fd_table[newfd].vfs_file;

    pipe_fase49_fd_trace((uint32_t)current_process->task.pid, newfd, pip,
			 fd_table[newfd].pipe_end, pip->fd_refs, "DUP2");
  }

  fase48_note_fd_created();
  return newfd;
}
int64_t sys_fcntl(int fd, int cmd, unsigned long arg)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  switch (cmd)
  {
  case F_GETFD:
    return (fd_table[fd].fd_flags & FD_CLOEXEC) ? FD_CLOEXEC : 0;
  case F_SETFD:
    fd_table[fd].fd_flags = (uint8_t)(arg & FD_CLOEXEC);
    return 0;
  case F_GETFL:
    return fd_table[fd].flags;
  case F_SETFL:
    /*
     * Linux: preserve O_ACCMODE; only status flags are mutable.
     * Do not honor O_ASYNC/FASYNC (no SIGIO). Clobbering access mode
     * broke BusyBox less (ndelay_on → flags became O_NONBLOCK alone).
     */
    {
      int keep = fd_table[fd].flags & O_ACCMODE;
      int settable = (int)arg & (O_APPEND | O_NONBLOCK);

      fd_table[fd].flags = keep | settable;
    }
    return 0;
  case F_GETOWN:
    return current_process->task.pid;
  case F_SETOWN:
    /* Owner recorded as current pid only; no SIGIO wiring yet. */
    (void)arg;
    return 0;
  case F_DUPFD:
  {
    int start = (int)arg;
    int i;

    if (start < 0 || start >= MAX_FDS_PER_PROCESS)
      return -EINVAL;
    for (i = start; i < MAX_FDS_PER_PROCESS; i++)
    {
      if (!fd_table[i].in_use)
        return sys_dup2(fd, i);
    }
    return -EMFILE;
  }
  default:
    return -EINVAL;
  }
}
int64_t sys_pipe(int pipefd[2])
{
  return sys_pipe2(pipefd, 0);
}

static int64_t sys_pipe_install(int pipefd[2], int flags)
{
  pipe_t *pipe;
  fd_entry_t *fd_table;
  int read_fd = -1;
  int write_fd = -1;
  uint8_t cloexec = 0;
  int read_fl = O_RDONLY;
  int write_fl = O_WRONLY;

  if (!current_process)
    return -ESRCH;

  if (!pipefd)
    return -EFAULT;

  if (validate_userspace_buffer(pipefd, sizeof(int) * 2) != 0)
    return -EFAULT;

  if (flags & ~(O_NONBLOCK | O_CLOEXEC))
    return -EINVAL;

  if (flags & O_NONBLOCK)
  {
    read_fl |= O_NONBLOCK;
    write_fl |= O_NONBLOCK;
  }
  if (flags & O_CLOEXEC)
    cloexec = FD_CLOEXEC;

  if (current_process->task.pid == 1)
    process_fase48_capture_fd_baseline(current_process);

  pipe = pipe_create();
  if (!pipe)
    return -ENOMEM;

  fd_table = get_process_fd_table();
  for (int i = 3; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      if (read_fd == -1)
        read_fd = i;
      else if (write_fd == -1)
      {
        write_fd = i;
        break;
      }
    }
  }

  if (read_fd == -1 || write_fd == -1)
  {
    pipe_abort_unopened(pipe);
    return -EMFILE;
  }

  fd_table[read_fd].in_use = true;
  strncpy(fd_table[read_fd].path, "/dev/pipe", sizeof(fd_table[read_fd].path) - 1);
  fd_table[read_fd].path[sizeof(fd_table[read_fd].path) - 1] = '\0';
  fd_table[read_fd].offset = 0;
  fd_table[read_fd].flags = read_fl;
  fd_table[read_fd].fd_flags = cloexec;
  fd_table[read_fd].vfs_file = (void *)pipe;
  fd_table[read_fd].is_pipe = true;
  fd_table[read_fd].pipe_end = 0;

  fd_table[write_fd].in_use = true;
  strncpy(fd_table[write_fd].path, "/dev/pipe", sizeof(fd_table[write_fd].path) - 1);
  fd_table[write_fd].path[sizeof(fd_table[write_fd].path) - 1] = '\0';
  fd_table[write_fd].offset = 0;
  fd_table[write_fd].flags = write_fl;
  fd_table[write_fd].fd_flags = cloexec;
  fd_table[write_fd].vfs_file = (void *)pipe;
  fd_table[write_fd].is_pipe = true;
  fd_table[write_fd].pipe_end = 1;

  pipe_acquire_end(pipe, 0);
  pipe_acquire_end(pipe, 1);

  fase48_note_fd_created();
  fase48_note_fd_created();

  {
    int kfd[2];

    kfd[0] = read_fd;
    kfd[1] = write_fd;
    if (copy_to_user(pipefd, kfd, sizeof(kfd)) != 0)
    {
      pipe_t *pip = pipe;

      fd_table[read_fd].in_use = false;
      fd_table[read_fd].vfs_file = NULL;
      fd_table[read_fd].is_pipe = false;
      fd_table[read_fd].pipe_end = -1;
      fd_table[write_fd].in_use = false;
      fd_table[write_fd].vfs_file = NULL;
      fd_table[write_fd].is_pipe = false;
      fd_table[write_fd].pipe_end = -1;
      pipe_close_end(pip, 0);
      pipe_close_end(pip, 1);
      fase48_note_fd_destroyed();
      fase48_note_fd_destroyed();
      return -EFAULT;
    }
  }

  return 0;
}

int64_t sys_pipe2(int pipefd[2], int flags)
{
  return sys_pipe_install(pipefd, flags);
}

/*
 * Linux syslog(2) / klogctl — enough for BusyBox dmesg (SIZE_BUFFER + READ_ALL).
 * Ring content comes from klog_read_records (same as /proc/kmsg).
 */
#define IR0_SYSLOG_CLOSE          0
#define IR0_SYSLOG_OPEN           1
#define IR0_SYSLOG_READ           2
#define IR0_SYSLOG_READ_ALL       3
#define IR0_SYSLOG_READ_CLEAR     4
#define IR0_SYSLOG_CLEAR          5
#define IR0_SYSLOG_CONSOLE_OFF    6
#define IR0_SYSLOG_CONSOLE_ON     7
#define IR0_SYSLOG_CONSOLE_LEVEL  8
#define IR0_SYSLOG_SIZE_UNREAD    9
#define IR0_SYSLOG_SIZE_BUFFER    10

#define IR0_SYSLOG_BUF_CAP        16384

int64_t sys_syslog(int type, char *bufp, int len)
{
	char *kbuf;
	int n;

	switch (type)
	{
	case IR0_SYSLOG_CLOSE:
	case IR0_SYSLOG_OPEN:
	case IR0_SYSLOG_CLEAR:
	case IR0_SYSLOG_CONSOLE_OFF:
	case IR0_SYSLOG_CONSOLE_ON:
		return 0;
	case IR0_SYSLOG_CONSOLE_LEVEL:
		if (len < 1 || len > 8)
			return -EINVAL;
		return 0;
	case IR0_SYSLOG_SIZE_BUFFER:
	case IR0_SYSLOG_SIZE_UNREAD:
		return IR0_SYSLOG_BUF_CAP;
	case IR0_SYSLOG_READ:
	case IR0_SYSLOG_READ_ALL:
	case IR0_SYSLOG_READ_CLEAR:
		if (!bufp || len <= 0)
			return -EINVAL;
		if (len > IR0_SYSLOG_BUF_CAP)
			len = IR0_SYSLOG_BUF_CAP;
		kbuf = (char *)kmalloc_try((size_t)len);
		if (!kbuf)
			return -ENOMEM;
		n = klog_read_records(kbuf, (size_t)len);
		if (n < 0)
		{
			kfree(kbuf);
			return -EIO;
		}
		if (n > 0 && copy_to_user(bufp, kbuf, (size_t)n) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);
		return n;
	default:
		return -EINVAL;
	}
}
