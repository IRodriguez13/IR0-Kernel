/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fs_syscalls.c
 * Description: file I/O syscalls (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/syscalls.h>
#include <kernel/process.h>
#include "fs_syscalls.h"
#include "fs_path_syscalls.h"
#include "syscalls_glue.h"
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/console_backend.h>
#include <ir0/devfs.h>
#include <ir0/named_devnode.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/flock.h>
#include <ir0/open_flags.h>
#include <ir0/path.h>
#include <ir0/path_routed.h>
#include <ir0/stat_user.h>
#include <ir0/named_fifo.h>
#include <ir0/named_symlink.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <ir0/posix_shm.h>
#include <ir0/fd_dispatch.h>
#include <ir0/fd_get.h>
#include <ir0/supervise_path.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/pipe.h>
#include <ir0/sock_stream.h>
#include <ir0/signals.h>
#include <ir0/procfs.h>
#include "io_syscalls.h"
#include <ir0/pseudo_fs.h>
#include <ir0/heartfs.h>
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <ir0/ash_smoke.h>
#include <d1_12_read_diag.h>
#include <ir0/sysfs.h>
#include <ir0/uio.h>
#include <ir0/validation.h>
#include <ir0/vfs.h>
#include <ir0/elf_loader.h>
#include <ir0/process.h>
#include <ir0/utimens.h>
#include <ir0/paging.h>
#include <stddef.h>
#include <string.h>

#define AT_REMOVEDIR 0x200

static int mknod_prepare_fifo_path(char *resolved, size_t resolved_sz)
{
  stat_t st;
  stat_t vst;
  int rc;
  int have_named;

  rc = ir0_follow_named_symlinks(resolved, resolved_sz, IR0_SYMLINK_FOLLOW_MAX);
  if (rc != 0)
    return rc;

  {
    char norm[256];

    if (normalize_path(resolved, norm, sizeof(norm)) != 0)
      return -ENAMETOOLONG;
    strncpy(resolved, norm, resolved_sz - 1);
    resolved[resolved_sz - 1] = '\0';
  }

  have_named = (named_fifo_stat(resolved, &st) == 0);
  if (have_named && vfs_stat(resolved, &vst) != 0)
    return 0;

  rc = vfs_clear_stale_for_regular_file(resolved);
  if (rc != 0)
    return rc;

  if (vfs_stat(resolved, &st) == 0)
  {
    if (S_ISDIR(st.st_mode))
      rc = vfs_rmdir_recursive(resolved);
    else
      rc = vfs_unlink(resolved);
    if (rc != 0)
      return rc;
  }

  return 0;
}

static void mknod_purge_vfs_shadow(const char *path)
{
  stat_t st;

  while (vfs_stat(path, &st) == 0)
  {
    if (S_ISDIR(st.st_mode))
    {
      if (vfs_rmdir_recursive(path) != 0)
        break;
    }
    else if (vfs_unlink(path) != 0)
      break;
  }
}

static int64_t mknod_create_named_fifo(const char *resolved, mode_t mode)
{
  int rc;

  rc = named_fifo_create(resolved, mode);
  if (rc != 0)
    return rc;

  mknod_purge_vfs_shadow(resolved);
  return 0;
}

static int fs_dirfd_base_path(int dirfd, char *base, size_t base_sz)
{
  fd_entry_t *fdt;
  stat_t st;
  const char *cwd;

  if (!current_process)
    return -ESRCH;

  if (dirfd == IR0_AT_FDCWD)
  {
    cwd = current_process->cwd;
    if (!cwd || cwd[0] != '/')
      cwd = "/";
    strncpy(base, cwd, base_sz - 1);
    base[base_sz - 1] = '\0';
    return 0;
  }

  fdt = get_process_fd_table();
  if (!fdt || dirfd < 0 || dirfd >= MAX_FDS_PER_PROCESS || !fdt[dirfd].in_use)
    return -EBADF;
  if (fdt[dirfd].path[0] != '/')
    return -EBADF;
  if (ir0_stat_path_routed(fdt[dirfd].path, &st) != 0)
    return -ENOTDIR;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  strncpy(base, fdt[dirfd].path, base_sz - 1);
  base[base_sz - 1] = '\0';
  return 0;
}

int ir0_resolve_path_at(int dirfd, const char *user_path, char *resolved,
                        size_t resolved_sz)
{
  char path_copy[256];
  char dirpath[256];
  char joined[256];
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!user_path || !resolved || resolved_sz == 0)
    return -EFAULT;
  if (copy_from_user_cstring(path_copy, sizeof(path_copy), user_path) != 0)
    return -EFAULT;

  if (path_copy[0] == '/')
  {
    return ir0_resolve_kpath_at(IR0_AT_FDCWD, path_copy, resolved, resolved_sz,
                                current_process->cwd, current_process->root);
  }

  if (dirfd != IR0_AT_FDCWD)
  {
    rc = fs_dirfd_base_path(dirfd, dirpath, sizeof(dirpath));
    if (rc != 0)
      return rc;
    if (join_paths(dirpath, path_copy, joined, sizeof(joined)) != 0)
      return -ENAMETOOLONG;
    /*
     * dirfd path is already host-absolute (may be outside the jail).
     * Do not re-apply process root (Linux openat via outside fd).
     */
    if (normalize_path(joined, resolved, resolved_sz) != 0)
      return -ENAMETOOLONG;
    return 0;
  }

  return ir0_resolve_user_path_at(dirfd, user_path, resolved, resolved_sz,
                                  current_process->cwd, current_process->root);
}

static int64_t do_readlinkat(int dirfd, const char *pathname, char *buf,
                             size_t bufsiz)
{
  char resolved[256];
  char kbuf[256];
  const char *target;
  size_t len;
  size_t copy_len;
  int rc;

  if (!current_process || !pathname || !buf)
    return -EFAULT;
  if (bufsiz == 0)
    return -EINVAL;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;
  if (validate_userspace_buffer(buf, bufsiz) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if (is_proc_path(resolved))
  {
    rc = proc_readlink(resolved, kbuf, sizeof(kbuf));
    if (rc >= 0)
    {
      len = (size_t)rc;
      copy_len = len;
      if (copy_len > bufsiz)
        copy_len = bufsiz;
      if (copy_to_user(buf, kbuf, copy_len) != 0)
        return -EFAULT;
      return (int64_t)len;
    }
    if (rc != -ENOENT)
      return rc;
  }

  rc = vfs_readlink(resolved, kbuf, sizeof(kbuf));
  if (rc >= 0)
  {
    len = (size_t)rc;
    copy_len = len;
    if (copy_len > bufsiz)
      copy_len = bufsiz;
    if (copy_to_user(buf, kbuf, copy_len) != 0)
      return -EFAULT;
    return (int64_t)len;
  }
  if (rc != -ENOSYS && rc != -ENOENT && rc != -EINVAL)
    return rc;

  target = named_symlink_target(resolved);
  if (!target)
  {
    stat_t st;

    if (vfs_stat(resolved, &st) == 0)
      return -EINVAL;
    return -ENOENT;
  }

  len = strlen(target);
  copy_len = len;
  if (copy_len > bufsiz)
    copy_len = bufsiz;
  if (copy_to_user(buf, target, copy_len) != 0)
    return -EFAULT;
  return (int64_t)len;
}

static int64_t do_symlinkat(const char *target, int linkdirfd,
                            const char *linkpath)
{
  char link_resolved[256];
  char target_copy[256];
  stat_t st;
  int rc;

  if (!current_process || !target || !linkpath)
    return -EFAULT;
  if (copy_from_user_cstring(target_copy, sizeof(target_copy), target) != 0)
    return -EFAULT;
  if (validate_userspace_string(linkpath, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(linkdirfd, linkpath, link_resolved,
                           sizeof(link_resolved));
  if (rc != 0)
    return rc;

  if (named_symlink_stat(link_resolved, &st) == 0 ||
      named_fifo_stat(link_resolved, &st) == 0 ||
      vfs_stat(link_resolved, &st) == 0)
    return -EEXIST;

  rc = vfs_symlink(target_copy, link_resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOSYS)
    return rc;

  return named_symlink_create(link_resolved, target_copy);
}

static void fase50c_log_open_result(const char *path, int64_t ret, int stage)
{
  (void)path;
  (void)ret;
  (void)stage;
}

static void ash_smoke_read_trace(int fd, int64_t ret)
{
  if (fd == STDIN_FILENO)
    ir0_ash_smoke_read_return(fd, ret);
}

static void ash_smoke_write_trace(int fd, const void *data, size_t count)
{
  if ((fd != STDOUT_FILENO && fd != STDERR_FILENO) || !data || count == 0)
    return;

  ir0_ash_smoke_scan_write((const char *)data, count);
  ir0_ash_smoke_scan_stdout((const char *)data, count);
}

static int open_named_fifo_fd(const char *path, int ir0_flags)
{
  pipe_t *pipe;
  fd_entry_t *fd_table;
  int accmode;
  int end;
  int fd = -1;

  pipe = named_fifo_lookup(path);
  if (!pipe)
    return -ENOENT;

  accmode = ir0_flags & O_ACCMODE;
  if (accmode == O_WRONLY)
    end = 1;
  else if (accmode == O_RDONLY || accmode == O_RDWR)
    end = 0;
  else
    return -EINVAL;

  fd_table = get_process_fd_table();
  if (!fd_table)
    return -ESRCH;

  fd = fd_alloc_lowest(fd_table, 0);
  if (fd < 0)
    return -EMFILE;

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].offset = 0;
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = (ir0_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = (void *)pipe;
  fd_table[fd].is_pipe = true;
  fd_table[fd].pipe_end = end;
  fd_table[fd].is_devfs = false;
  fd_table[fd].is_socket = false;

  pipe_acquire_end(pipe, end);
  fd_slot_note_created();
  return fd;
}

static devfs_node_t *devfs_node_from_fd(int fd)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return NULL;
  if (!fd_table[fd].is_devfs)
    return NULL;
  ensure_devfs_init();
  return devfs_find_node_by_id(fd_table[fd].dev_device_id);
}

/*
 * devfs_resolve_read_fd - Bound fd_table slot with is_devfs (no virtual FD_DEV).
 * @offset_out: file offset from fd_table when bound.
 */
static devfs_node_t *devfs_resolve_read_fd(int fd, off_t *offset_out)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS && fd_table[fd].in_use &&
      fd_table[fd].is_devfs)
  {
    ensure_devfs_init();
    if (offset_out)
      *offset_out = fd_table[fd].offset;
    return devfs_find_node_by_id(fd_table[fd].dev_device_id);
  }

  return NULL;
}

static int devfs_bind_fd_slot(const char *path, devfs_node_t *node, int ir0_flags)
{
  fd_entry_t *fd_table = get_process_fd_table();
  int fd = -1;
  int i;

  if (!fd_table || !node || !path)
    return -EINVAL;

  for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }
  if (fd < 0)
    return -EMFILE;

  fd_table[fd].in_use = true;
  fd_table[fd].is_devfs = true;
  fd_table[fd].dev_device_id = node->entry.device_id;
  fd_table[fd].is_pipe = false;
  fd_table[fd].pipe_end = -1;
  fd_table[fd].vfs_file = NULL;
  fd_table[fd].offset = 0;
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = 0;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';

  /*
   * Finite text devices: capture a per-open snapshot so read() honors
   * offset and reaches EOF (BusyBox cat). Failure falls back to ops->read
   * bounce+slice.
   */
  if (devfs_node_wants_text_snap(node->entry.device_id))
    fd_table[fd].vfs_file = devfs_text_snap_capture(node->entry.device_id);

  if (DEBUG_VFS)
  {
    klog_debug_fmt("VFS", "[VFS][OPEN] path=%s fd=%llx dev_id=%x", path, (unsigned long long)((uint64_t)fd), (unsigned)(node->entry.device_id));
  }

  return fd;
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (count == 0)
    return 0;  /* POSIX: write with count 0 returns 0 */
  if (!buf)
    return -EFAULT;

  /* Validate user buffer for regular files */
  char kernel_buf[PAGE_SIZE_4KB];
  const char *str = NULL;
  size_t copy_size = (count < sizeof(kernel_buf)) ? count : sizeof(kernel_buf);

  if (fd >= 0 && fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fdt = get_process_fd_table();

    if (fd_entry_pseudo(fdt ? &fdt[fd] : NULL))
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fdt[fd].vfs_file;

      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
	return -EFAULT;
      return pseudo_fs_ops_write((const pseudo_fs_ops_t *)bind->ops, bind->ctx,
				kernel_buf, copy_size);
    }
  }

  {
    devfs_node_t *node = devfs_node_from_fd(fd);

    if (node)
    {
      if (!node->ops || !node->ops->write)
        return -EBADF;
      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
        return -EFAULT;
      ash_smoke_write_trace(fd, kernel_buf, copy_size);
      return node->ops->write(&node->entry, kernel_buf, copy_size, 0);
    }
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO && !stdio_is_redirected(fd_table, fd))
  {
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
      /* Unredirected stdio still goes to console backend. */
      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
        return -EFAULT;
      ash_smoke_write_trace(fd, kernel_buf, copy_size);
      str = kernel_buf;
      uint8_t color = (fd == STDERR_FILENO) ? 0x0C : 0x0F;
      {
	size_t done = 0;
	const size_t step = 64;

	/*
	 * Binary dumps (`cat /dev/hda`) can pin the CPU in putchar for
	 * minutes. Check SIGINT between chunks so Ctrl+C is not ignored.
	 */
	while (done < copy_size)
	{
	  size_t n = copy_size - done;

	  if (n > step)
	    n = step;
	  if (current_process &&
	      signals_pause_should_interrupt(current_process))
	  {
	    handle_signals();
	    if (done > 0)
	      return (int64_t)done;
	    return -EINTR;
	  }
	  console_backend_write(str + done, n, color);
	  done += n;
	}
      }
      if (current_process->task.pid == 1 && copy_size >= 10 &&
	  kernel_buf[0] == 'F' && !memcmp(kernel_buf, "FASE48_IPC", 10))
	process_fase48_ipc_summary("fase48-final");
      if (current_process->task.pid == 1 && copy_size >= 11 &&
	  kernel_buf[0] == 'F' && !memcmp(kernel_buf, "FASE49_PIPE", 11))
	pipe_ipc_lifecycle_audit();
      return (int64_t)copy_size;
    }
    return -EBADF;
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    return ir0_eventfd_write((struct ir0_eventfd *)fd_table[fd].vfs_file, buf, count,
			     (fd_table[fd].flags & O_NONBLOCK) != 0);

  /* AF_UNIX/TCP stream sockets: write(2) → sock_stream_send */
  if (fd_entry_socket(&fd_table[fd]) &&
      sock_stream_is(fd_table[fd].vfs_file))
  {
    struct sock_stream *ss = (struct sock_stream *)fd_table[fd].vfs_file;
    size_t total = 0;
    int nb = (fd_table[fd].flags & O_NONBLOCK) != 0;

    while (total < count)
    {
      size_t chunk = count - total;
      ssize_t n;

      if (chunk > sizeof(kernel_buf))
	chunk = sizeof(kernel_buf);
      if (copy_from_user(kernel_buf, (const char *)buf + total, chunk) != 0)
	return total > 0 ? (int64_t)total : -EFAULT;
      n = sock_stream_send(ss, kernel_buf, chunk);
      if (n < 0)
	return total > 0 ? (int64_t)total : n;
      if (n == 0)
      {
	if (nb)
	  return total > 0 ? (int64_t)total : -EAGAIN;
	if (signals_pause_should_interrupt(current_process))
	  return total > 0 ? (int64_t)total : -EINTR;
	{
	  int64_t sleep_ret = syscall_sleep_ms_locked(20);

	  if (sleep_ret < 0)
	    return total > 0 ? (int64_t)total : sleep_ret;
	}
	continue;
      }
      total += (size_t)n;
    }
    return (int64_t)total;
  }

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    size_t total = 0;
    int ret;
    int nb = (fd_table[fd].flags & O_NONBLOCK) != 0;

    if (!pipe)
      return -EBADF;

    if (fd_table[fd].pipe_end != 1)
      return -EBADF;

    /*
     * Linux pipe(7) / write(2): blocking write completes the full count
     * (atomic ≤ PIPE_BUF via pipe_write, else partial chunks + wait).
     * O_NONBLOCK may short-write after any progress.
     */
    while (total < count)
    {
      size_t chunk = count - total;
      size_t off = 0;

      if (chunk > sizeof(kernel_buf))
	chunk = sizeof(kernel_buf);
      if (copy_from_user(kernel_buf, (const char *)buf + total, chunk) != 0)
	return total > 0 ? (int64_t)total : -EFAULT;

      while (off < chunk)
      {
	ret = pipe_write(pipe, kernel_buf + off, chunk - off);
	if (ret > 0)
	{
	  off += (size_t)ret;
	  total += (size_t)ret;
	  pipe_wake_all(pipe);
	  continue;
	}
	if (ret == -EPIPE)
	{
	  /*
	   * Queue SIGPIPE unless SIG_IGN (Linux signal(7)). User handlers
	   * still get the signal; write returns -EPIPE if no bytes yet,
	   * else short success (Linux pipe_write keeps prior progress).
	   */
	  if (!(current_process->signal_ignored & SIGNAL_MASK(SIGPIPE)))
	    current_process->signal_pending |= SIGNAL_MASK(SIGPIPE);
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EPIPE;
	}
	if (ret != -EAGAIN)
	  return total > 0 ? (int64_t)total : ret;
	if (nb)
	  return total > 0 ? (int64_t)total : -EAGAIN;
	if (pipe->readers <= 0)
	{
	  if (!(current_process->signal_ignored & SIGNAL_MASK(SIGPIPE)))
	    current_process->signal_pending |= SIGNAL_MASK(SIGPIPE);
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EPIPE;
	}
	if (signals_pause_should_interrupt(current_process))
	{
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EINTR;
	}
	{
	  int wait_ret = pipe_wait(current_process, pipe, 0,
				     chunk - off);

	  if (wait_ret == -EINTR)
	  {
	    handle_signals();
	    return total > 0 ? (int64_t)total : -EINTR;
	  }
	  if (wait_ret != 0)
	    return total > 0 ? (int64_t)total : -EAGAIN;
	}
	if (signals_pause_should_interrupt(current_process))
	{
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EINTR;
	}
      }
    }
    process_clear_in_thread_syscall_block(current_process);
    return (int64_t)total;
  }

  /* Check write permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_WRITE, current_process))
    return -EACCES;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    size_t total = 0;
    int ret;

    while (total < count)
    {
      size_t chunk = count - total;

      if (chunk > sizeof(kernel_buf))
        chunk = sizeof(kernel_buf);
      if (copy_from_user(kernel_buf, (const char *)buf + total, chunk) != 0)
        return total > 0 ? (int64_t)total : -EFAULT;
      ret = vfs_write(vfs_file, kernel_buf, chunk);
      if (ret < 0)
        return total > 0 ? (int64_t)total : ret;
      if (ret == 0)
        break;
      total += (size_t)ret;
      if ((size_t)ret < chunk)
        break;
    }
    fd_table[fd].offset = vfs_file->pos;
    return (int64_t)total;
  }

  return -EBADF;
}

/*
 * sys_writev - POSIX writev(2) via repeated sys_write (musl stdio flush path).
 */
int64_t sys_writev(int fd, const struct iovec *iov, int iovcnt)
{
  int64_t total = 0;
  int i;

  if (!current_process)
    return -ESRCH;
  if (!iov)
    return -EFAULT;
  if (iovcnt <= 0)
    return -EINVAL;
  if (iovcnt > IR0_UIO_MAXIOV)
    return -EINVAL;
  if (validate_userspace_buffer(iov,
                                (size_t)iovcnt * sizeof(struct iovec)) != 0)
    return -EFAULT;

  for (i = 0; i < iovcnt; i++)
  {
    struct iovec kiov;
    int64_t ret;

    if (copy_from_user(&kiov, &iov[i], sizeof(kiov)) != 0)
      return total > 0 ? total : -EFAULT;
    if (kiov.iov_len == 0)
      continue;
    ret = sys_write(fd, kiov.iov_base, kiov.iov_len);
    if (ret < 0)
      return total > 0 ? total : ret;
    total += ret;
    if ((size_t)ret < kiov.iov_len)
      break;
  }

  return total;
}

int64_t sys_read(int fd, void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (count == 0)
    return 0;  /* POSIX: read with count 0 returns 0 */
  if (!buf)
    return -EFAULT;

  /* Process-local pseudo_fs binds (/proc|/sys|/heart) — real fd_table slots. */
  if (fd >= 0 && fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fdt = get_process_fd_table();

    if (fdt && fdt[fd].in_use && (fdt[fd].flags & O_DIRECTORY) &&
	!fdt[fd].is_pipe && !fdt[fd].is_socket)
    {
      /* Directory fds (incl. O_DIRECTORY opens) are not readable via read(2). */
      return -EISDIR;
    }

    if (fdt && fdt[fd].in_use && fdt[fd].is_pseudo && fdt[fd].vfs_file)
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fdt[fd].vfs_file;
      char kernel_read_buf[PAGE_SIZE_4KB];
      size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
      off_t off = (off_t)fdt[fd].offset;
      int64_t ret;

      ret = pseudo_fs_ops_read((const pseudo_fs_ops_t *)bind->ops, bind->ctx,
			       kernel_read_buf, read_size, &off);
      if (ret > 0)
      {
	if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
	  return -EFAULT;
	fdt[fd].offset = (uint64_t)off;
      }
      return ret;
    }
  }

  /* /dev: bound fd_table slot (is_devfs) */
  {
    off_t read_off = 0;
    devfs_node_t *node = devfs_resolve_read_fd(fd, &read_off);
    fd_entry_t *fd_table = get_process_fd_table();

    if (node)
    {
      char kernel_read_buf[PAGE_SIZE_4KB];
      size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
      int ret;

      if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
	  fd_table[fd].in_use && fd_table[fd].vfs_file &&
	  devfs_node_wants_text_snap(fd_table[fd].dev_device_id))
      {
	ret = (int)devfs_text_snap_read(
		(const devfs_text_snap_t *)fd_table[fd].vfs_file,
		kernel_read_buf, read_size, read_off);
      }
      else
      {
	if (!node->ops || !node->ops->read)
	  return -EBADF;
	{
	  int nb = (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
		    fd_table[fd].in_use &&
		    (fd_table[fd].flags & IR0_O_NONBLOCK)) ? 1 : 0;

	  devfs_set_read_nonblock(nb);
	  ret = node->ops->read(&node->entry, kernel_read_buf, read_size, read_off);
	  devfs_set_read_nonblock(0);
	}
      }
      if (ret > 0)
      {
        if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
          return -EFAULT;
        if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS && fd_table[fd].in_use)
          fd_table[fd].offset += ret;
        if (fd == STDIN_FILENO)
        {
          d1_12_read_diag_kcopy(ret, count, kernel_read_buf, (size_t)ret);
          ash_smoke_read_trace(fd, ret);
        }
      }
      else if (fd == STDIN_FILENO)
      {
        ash_smoke_read_trace(fd, ret);
      }
      return ret;
    }
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO && !stdio_is_redirected(fd_table, fd))
  {
    if (fd != STDIN_FILENO)
      return -EBADF;
    {
      int64_t ret = syscalls_read_stdio_stdin(buf, count);

      ash_smoke_read_trace(STDIN_FILENO, ret);
      return ret;
    }
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    return ir0_eventfd_read((struct ir0_eventfd *)fd_table[fd].vfs_file, buf, count,
			    (fd_table[fd].flags & O_NONBLOCK) != 0);
  if (fd_table[fd].is_timerfd && fd_table[fd].vfs_file)
    return ir0_timerfd_read((struct ir0_timerfd *)fd_table[fd].vfs_file, buf, count,
			    (fd_table[fd].flags & O_NONBLOCK) != 0);

  /* AF_UNIX/TCP stream sockets: read(2) → sock_stream_recv (blocking). */
  if (fd_table[fd].is_socket && fd_table[fd].vfs_file &&
      sock_stream_is(fd_table[fd].vfs_file))
  {
    struct sock_stream *ss = (struct sock_stream *)fd_table[fd].vfs_file;
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    int nb = (fd_table[fd].flags & O_NONBLOCK) != 0;
    ssize_t n;

    for (;;)
    {
      n = sock_stream_recv_flags(ss, kernel_read_buf, read_size, 0);
      if (n != -EAGAIN)
	break;
      if (nb)
	return -EAGAIN;
      if (signals_pause_should_interrupt(current_process))
	return -EINTR;
      {
	int64_t sleep_ret = syscall_sleep_ms_locked(20);

	if (sleep_ret < 0)
	  return sleep_ret;
      }
    }
    if (n < 0)
      return n;
    if (n > 0 && copy_to_user(buf, kernel_read_buf, (size_t)n) != 0)
      return -EFAULT;
    return n;
  }

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    int ret;

    if (!pipe)
      return -EBADF;

    if (fd_table[fd].pipe_end != 0)
      return -EBADF;

    for (;;)
    {
      ret = pipe_read(pipe, kernel_read_buf, read_size);
      if (ret >= 0)
      {
        pipe_wake_all(pipe);
        if (ret == 0)
        {
          return 0;
        }
        if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
          return -EFAULT;
        process_clear_in_thread_syscall_block(current_process);
        return ret;
      }
      if (ret != -EAGAIN)
        return ret;
      if (fd_table[fd].flags & O_NONBLOCK)
        return -EAGAIN;
      /*
       * No "writers are gone, report EOF" shortcut here. This runs after
       * pipe_read() already released the pipe, so the task can be preempted
       * in between: the writer then fills the buffer and exits, and the
       * shortcut sees writers==0 and returns EOF while the bytes it just
       * wrote are still queued. Looping back into pipe_read() decides both
       * cases together, as Linux pipe_read does under the pipe mutex —
       * buffered data first, EOF only on an empty pipe with no writers.
       */
      if (signals_pause_should_interrupt(current_process))
      {
	handle_signals();
	return -EINTR;
      }
      {
	int wait_ret = pipe_wait(current_process, pipe, 1, 0);

	if (wait_ret == -EINTR)
	{
	  handle_signals();
	  return -EINTR;
	}
	if (wait_ret != 0)
	  return -EAGAIN;
      }
      if (signals_pause_should_interrupt(current_process))
      {
	handle_signals();
	return -EINTR;
      }
    }
  }

  /* Check read permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_READ, current_process))
    return -EACCES;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    char kernel_read_buf[PAGE_SIZE_4KB];
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    size_t total = 0;
    int ret;

    while (total < count)
    {
      size_t chunk = count - total;

      if (chunk > sizeof(kernel_read_buf))
        chunk = sizeof(kernel_read_buf);
      ret = vfs_read(vfs_file, kernel_read_buf, chunk);
      if (ret < 0)
        return total > 0 ? (int64_t)total : ret;
      if (ret == 0)
        break;
      if (copy_to_user((char *)buf + total, kernel_read_buf, (size_t)ret) != 0)
        return total > 0 ? (int64_t)total : -EFAULT;
      total += (size_t)ret;
      if ((size_t)ret < chunk)
        break;
    }
    fd_table[fd].offset = vfs_file->pos;
    return (int64_t)total;
  }

  return -EBADF;
}

/*
 * sys_readv - POSIX readv(2) via repeated sys_read (mirror of sys_writev).
 */
int64_t sys_readv(int fd, const struct iovec *iov, int iovcnt)
{
  int64_t total = 0;
  int i;

  if (!current_process)
    return -ESRCH;
  if (!iov)
    return -EFAULT;
  if (iovcnt <= 0)
    return -EINVAL;
  if (iovcnt > IR0_UIO_MAXIOV)
    return -EINVAL;
  if (validate_userspace_buffer(iov,
                                (size_t)iovcnt * sizeof(struct iovec)) != 0)
    return -EFAULT;

  for (i = 0; i < iovcnt; i++)
  {
    struct iovec kiov;
    int64_t ret;

    if (copy_from_user(&kiov, &iov[i], sizeof(kiov)) != 0)
      return total > 0 ? total : -EFAULT;
    if (kiov.iov_len == 0)
      continue;
    ret = sys_read(fd, kiov.iov_base, kiov.iov_len);
    if (ret < 0)
      return total > 0 ? total : ret;
    total += ret;
    if ((size_t)ret < kiov.iov_len)
      break;
  }

  return total;
}

int64_t sys_fstat(int fd, stat_t *buf)
{
  stat_t kst;
  int64_t rc;

  if (!current_process || !buf)
    return -EFAULT;

  if (validate_userspace_buffer(buf, IR0_USER_STAT_SIZE) != 0)
    return -EFAULT;

  if (fd < 0)
    return -EBADF;

  if (fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fdt = get_process_fd_table();

    if (fdt && fdt[fd].in_use && fdt[fd].is_pseudo && fdt[fd].vfs_file)
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fdt[fd].vfs_file;

      rc = pseudo_fs_ops_stat((const pseudo_fs_ops_t *)bind->ops, bind->ctx, &kst);
      if (rc != 0)
	return rc;
      if (ir0_copy_stat_to_user(buf, &kst) != 0)
	return -EFAULT;
      return 0;
    }
  }

  if (fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd <= 2)
  {
    memset(&kst, 0, sizeof(kst));
    kst.st_dev = 0;
    kst.st_ino = (ino_t)fd;
    kst.st_mode = S_IFCHR | S_IRUSR | S_IWUSR;
    kst.st_nlink = 1;
    kst.st_uid = 0;
    kst.st_gid = 0;
    kst.st_size = 0;
    rc = 0;
  }
  else
  {
    rc = ir0_stat_path_routed(fd_table[fd].path, &kst);
    if (rc != 0)
      return rc;
  }

  if (ir0_copy_stat_to_user(buf, &kst) != 0)
    return -EFAULT;
  return 0;
}

static int64_t sys_open_vfs_resolved(char *path_to_use, int ir0_flags,
                                     int linux_open_flags, mode_t mode,
                                     int dirfd_dbg)
{
  int flags;
  int64_t open_ret;
  int path_rc;
  fd_entry_t *fd_table;
  int fd;
  struct vfs_file *vfs_file;
  int ret;

  (void)linux_open_flags;
  (void)dirfd_dbg;

  if (!path_to_use || path_to_use[0] != '/')
    return -EINVAL;

  flags = ir0_flags;

  if (!(ir0_flags & O_DIRECTORY))
  {
    path_rc = ir0_follow_named_symlinks(path_to_use, 256,
                                        IR0_SYMLINK_FOLLOW_MAX);
    if (path_rc != 0)
      return path_rc;
  }

  {
    int prep_rc = ir0_supervise_prepare_open(path_to_use, ir0_flags);

    if (prep_rc != 0)
      return prep_rc;
  }

  if (named_fifo_lookup(path_to_use))
  {
    open_ret = open_named_fifo_fd(path_to_use, ir0_flags);
    fase50c_log_open_result(path_to_use, open_ret, 13);
    return open_ret;
  }

  if (posix_shm_path_is(path_to_use))
  {
    open_ret = posix_shm_try_open(path_to_use, ir0_flags, 0600);
    fase50c_log_open_result(path_to_use, open_ret, 14);
    return open_ret;
  }

  /*
   * A device node created by mknod outside /dev: hand the open to the devfs
   * node owning that (major,minor), so the fd behaves exactly like the one
   * from /dev — same ops, same refcount, no second I/O path to maintain.
   */
  {
    mode_t dn_mode = 0;
    dev_t dn_rdev = 0;

    if (named_devnode_lookup(path_to_use, &dn_mode, &dn_rdev) == 0)
    {
      devfs_node_t *dn = devfs_find_node_by_rdev((uint32_t)dn_rdev);

      int64_t drc;

      if (!dn)
        return -ENXIO;
      ensure_devfs_init();
      drc = devfs_open_node(dn, ir0_flags);
      if (drc < 0)
        return drc;
      open_ret = devfs_bind_fd_slot(path_to_use, dn, ir0_flags);
      if (open_ret < 0)
        devfs_close_node(dn);
      return open_ret;
    }
  }

  if (flags & O_DIRECTORY)
  {
    if (!check_file_access(path_to_use, ACCESS_EXEC, current_process))
    {
      fase50c_log_open_result(path_to_use, -EACCES, 12);
      return -EACCES;
    }
  }
  else if (flags & O_CREAT)
  {
    stat_t st;

    if (vfs_stat(path_to_use, &st) != 0)
    {
      char parent[256];
      stat_t pst;

      if (get_parent_path(path_to_use, parent, sizeof(parent)) != 0)
        return -ENAMETOOLONG;
      /*
       * Missing parent is ENOENT (Linux). check_file_access() returns false
       * when stat fails — do not mask that as EACCES (runsv pid.new noise).
       */
      if (vfs_stat(parent, &pst) != 0)
      {
        fase50c_log_open_result(path_to_use, -ENOENT, 9);
        return -ENOENT;
      }
      if (!check_file_access(parent, ACCESS_EXEC | ACCESS_WRITE, current_process))
      {
        fase50c_log_open_result(path_to_use, -EACCES, 9);
        return -EACCES;
      }
    }
    else
    {
      int access_mode = 0;
      int accmode = flags & O_ACCMODE;

      if (accmode == O_RDONLY || accmode == O_RDWR)
        access_mode |= ACCESS_READ;
      if (accmode == O_WRONLY || accmode == O_RDWR)
        access_mode |= ACCESS_WRITE;
      if (access_mode &&
          !check_file_access(path_to_use, access_mode, current_process))
      {
        fase50c_log_open_result(path_to_use, -EACCES, 10);
        return -EACCES;
      }
    }
  }
  else
  {
    int access_mode = 0;
    int accmode = flags & O_ACCMODE;
    stat_t st;

    if (accmode == O_RDONLY || accmode == O_RDWR)
      access_mode |= ACCESS_READ;
    if (accmode == O_WRONLY || accmode == O_RDWR)
      access_mode |= ACCESS_WRITE;
    /*
     * Linux: missing path → ENOENT from lookup, not EACCES from a prior
     * permission probe on a non-existent inode.
     */
    if (access_mode && vfs_stat(path_to_use, &st) == 0 &&
        !check_file_access(path_to_use, access_mode, current_process))
    {
      fase50c_log_open_result(path_to_use, -EACCES, 11);
      return -EACCES;
    }
  }

  fd_table = get_process_fd_table();
  fd = -1;
  fd = fd_alloc_lowest(fd_table, 0);

  if (fd == -1)
  {
    fase50c_log_open_result(path_to_use, -EMFILE, 6);
    return -EMFILE;
  }

  vfs_file = NULL;
  ret = vfs_open(path_to_use, flags, mode, &vfs_file);
  if (ret != 0)
  {
    fase50c_log_open_result(path_to_use, (int64_t)ret, 7);
    return ret;
  }

  if (flags & O_DIRECTORY)
  {
    stat_t st;

    if (ir0_stat_path_routed(path_to_use, &st) < 0)
    {
      if (vfs_file)
        vfs_close(vfs_file);
      return -ENOTDIR;
    }
    if (!S_ISDIR(st.st_mode))
    {
      if (vfs_file)
        vfs_close(vfs_file);
      return -ENOTDIR;
    }
  }

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path_to_use, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = flags;
  fd_table[fd].fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = vfs_file;
  if (flags & O_DIRECTORY)
    fd_table[fd].offset = 0;
  else
    fd_table[fd].offset = vfs_file ? vfs_file->pos : 0;
  fd_table[fd].is_pipe = false;
  fd_table[fd].is_socket = false;
  fd_table[fd].is_devfs = false;
  fd_table[fd].is_pseudo = false;
  fd_table[fd].is_epoll = false;
  fd_table[fd].pipe_end = -1;
  fd_table[fd].dev_device_id = 0;
  fd_slot_note_created();

  fase50c_log_open_result(path_to_use, (int64_t)fd, 8);
  return fd;
}

static int64_t pseudo_bind_dir_fd(const char *path, int ir0_flags)
{
  fd_entry_t *fd_table;
  stat_t st;
  int fd;

  if (!path || !current_process)
    return -EINVAL;
  if (!(ir0_flags & O_DIRECTORY))
    return -EISDIR;

  if (ir0_stat_path_routed(path, &st) != 0)
    return -ENOENT;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;
  if (!check_file_access(path, ACCESS_EXEC, current_process))
    return -EACCES;

  fd_table = get_process_fd_table();
  fd = -1;
  for (int i = 0; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }
  if (fd < 0)
    return -EMFILE;

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = (ir0_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = NULL;
  fd_table[fd].offset = 0;
  fd_table[fd].is_pipe = false;
  fd_table[fd].is_socket = false;
  fd_table[fd].is_devfs = false;
  fd_table[fd].is_pseudo = false;
  fd_table[fd].pipe_end = -1;
  fd_table[fd].dev_device_id = 0;
  fd_slot_note_created();
  fase50c_log_open_result(path, (int64_t)fd, 5);
  return fd;
}

/*
 * Install a registry /proc|/sys|/heart file into the process fd_table.
 * Returns a real slot index (not PSEUDO_FS_*_FD_BASE).
 */
static int64_t pseudo_bind_file_fd(const char *path, int ir0_flags)
{
  fd_entry_t *fd_table;
  const pseudo_fs_ops_t *ops = NULL;
  void *ctx = NULL;
  pseudo_fd_bind_t *bind;
  int dynamic = 0;
  int fd;
  int64_t rc;

  if (!path || !current_process)
    return -EINVAL;

  pseudo_fs_nodes_register_all();
  rc = pseudo_fs_acquire_path(path, ir0_flags, &ops, &ctx, &dynamic);
  if (rc != 0)
    return rc;

  fd_table = get_process_fd_table();
  fd = -1;
  fd = fd_alloc_lowest(fd_table, 0);
  if (fd < 0)
  {
    (void)pseudo_fs_release_ops(ops, ctx, dynamic);
    return -EMFILE;
  }

  bind = (pseudo_fd_bind_t *)kmalloc_try(sizeof(*bind));
  if (!bind)
  {
    (void)pseudo_fs_release_ops(ops, ctx, dynamic);
    return -ENOMEM;
  }
  bind->ops = ops;
  bind->ctx = ctx;
  bind->refs = 1;
  bind->dynamic = dynamic;

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = (ir0_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = bind;
  fd_table[fd].offset = 0;
  fd_table[fd].is_pipe = false;
  fd_table[fd].is_socket = false;
  fd_table[fd].is_devfs = false;
  fd_table[fd].is_pseudo = true;
  fd_table[fd].pipe_end = -1;
  fd_table[fd].dev_device_id = 0;
  fd_slot_note_created();
  fase50c_log_open_result(path, (int64_t)fd, 1);
  return fd;
}

static int64_t devfs_open_resolved_node(const char *path, int ir0_flags)
{
  devfs_node_t *node;
  int64_t drc;
  int64_t open_ret;

  ensure_devfs_init();
  node = devfs_find_node(path);
  if (!node)
  {
    fase50c_log_open_result(path, -ENOENT, 3);
    return -ENOENT;
  }
  drc = devfs_open_node(node, ir0_flags);
  if (drc < 0)
  {
    fase50c_log_open_result(path, drc, 4);
    return drc;
  }
  open_ret = devfs_bind_fd_slot(path, node, ir0_flags);
  if (open_ret < 0)
  {
    devfs_close_node(node);
    fase50c_log_open_result(path, open_ret, 5);
    return open_ret;
  }
  fase50c_log_open_result(path, open_ret, 5);
  return open_ret;
}

/*
 * Route open(2) after path resolution so cwd-relative paths like ./stdin
 * from /dev reach devfs (same order as openat/stat).
 */
static int64_t sys_open_routed_resolved(char *resolved, int ir0_flags,
                                        int linux_open_flags, mode_t mode,
                                        int dirfd_dbg)
{
  int64_t open_ret;

  if (is_proc_path(resolved))
  {
    if (strcmp(resolved, "/proc") == 0 || strcmp(resolved, "/proc/") == 0 ||
        proc_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    open_ret = pseudo_bind_file_fd(resolved, ir0_flags);
    fase50c_log_open_result(resolved, open_ret, 1);
    return open_ret;
  }

  if (is_sys_path(resolved))
  {
    if (strcmp(resolved, "/sys") == 0 || strcmp(resolved, "/sys/") == 0 ||
        sysfs_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    open_ret = pseudo_bind_file_fd(resolved, ir0_flags);
    fase50c_log_open_result(resolved, open_ret, 2);
    return open_ret;
  }

  if (is_heart_path(resolved) && !heart_is_dennis_vfs_path(resolved))
  {
    char canon[256];

    heart_nodes_register();
    if (heart_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    if (heart_alias_canonical(resolved, canon, sizeof(canon)))
    {
      open_ret = pseudo_bind_file_fd(canon, ir0_flags);
      fase50c_log_open_result(resolved, open_ret, 6);
      return open_ret;
    }
    open_ret = pseudo_bind_file_fd(resolved, ir0_flags);
    fase50c_log_open_result(resolved, open_ret, 6);
    return open_ret;
  }

  if (ir0_is_dev_path(resolved))
  {
    if (posix_shm_path_is(resolved))
    {
      open_ret = posix_shm_try_open(resolved, ir0_flags, mode);
      fase50c_log_open_result(resolved, open_ret, 14);
      return open_ret;
    }
    if (strcmp(resolved, "/dev") == 0 || strcmp(resolved, "/dev/") == 0 ||
        devfs_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      ensure_devfs_init();
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    if (strncmp(resolved, "/dev/", 5) == 0)
    {
      ensure_devfs_init();
      return devfs_open_resolved_node(resolved, ir0_flags);
    }
    return -ENOENT;
  }

  return sys_open_vfs_resolved(resolved, ir0_flags, linux_open_flags, mode,
                               dirfd_dbg);
}

/* Open file and return file descriptor */
int64_t sys_open(const char *pathname, int flags, mode_t mode)
{
  char path_copy[256];
  char resolved_path[256];
  int ir0_flags;
  int linux_open_flags = flags;
  size_t plen;
  int path_rc;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  if (copy_from_user_cstring(path_copy, sizeof(path_copy), pathname) != 0)
    return -EFAULT;
  path_copy[sizeof(path_copy) - 1] = '\0';

  plen = 0;
  while (path_copy[plen] && plen < sizeof(path_copy))
    plen++;
  if (plen == 0 || plen >= sizeof(path_copy))
    return -EINVAL;

  ir0_flags = linux_open_flags_to_ir0(flags);
  ir0_open_flags_log_translation(flags, ir0_flags);
  if (!ir0_open_flags_ok_for_vfs(ir0_flags))
  {
    fase50c_log_open_result(path_copy, -EINVAL, 0);
    return -EINVAL;
  }


  path_rc = ir0_resolve_kpath_at(IR0_AT_FDCWD, path_copy, resolved_path,
                                 sizeof(resolved_path), current_process->cwd, current_process->root);
  if (path_rc != 0)
    return path_rc;

  return sys_open_routed_resolved(resolved_path, ir0_flags, linux_open_flags,
                                  mode, IR0_AT_FDCWD);
}

int64_t sys_stat(const char *pathname, stat_t *buf)
{
  char resolved[256];
  stat_t kst;
  int64_t rc;

  if (!current_process || !pathname || !buf)
    return -EFAULT;

  if (validate_userspace_buffer(buf, IR0_USER_STAT_SIZE) != 0)
    return -EFAULT;

  memset(&kst, 0, sizeof(kst));
  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  rc = ir0_stat_path_routed_follow(resolved, &kst);

  if (rc != 0)
    return rc;
  if (ir0_copy_stat_to_user(buf, &kst) != 0)
    return -EFAULT;
  return 0;
}

/*
 * sys_openat - Linux openat(2) subset (AT_FDCWD + directory fd).
 */
int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
  char resolved[256];
  char path_copy[256];
  int ir0_flags;
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  strncpy(path_copy, resolved, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  ir0_flags = linux_open_flags_to_ir0(flags);
  ir0_open_flags_log_translation(flags, ir0_flags);
  if (!ir0_open_flags_ok_for_vfs(ir0_flags))
  {
    fase50c_log_open_result(path_copy, -EINVAL, 0);
    return -EINVAL;
  }

  return sys_open_routed_resolved(resolved, ir0_flags, flags, mode, dirfd);
}

/*
 * sys_newfstatat - Linux newfstatat(2) subset.
 */
int64_t sys_newfstatat(int dirfd, const char *pathname, stat_t *buf, int flags)
{
  char resolved[256];
  stat_t kst;
  int64_t rc;

  if (!current_process || !buf)
    return -EFAULT;

  if (validate_userspace_buffer(buf, IR0_USER_STAT_SIZE) != 0)
    return -EFAULT;

  memset(&kst, 0, sizeof(kst));
  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if (!(flags & IR0_AT_SYMLINK_NOFOLLOW))
  {
    rc = ir0_follow_named_symlinks(resolved, sizeof(resolved),
                                   IR0_SYMLINK_FOLLOW_MAX);
    if (rc != 0)
      return rc;
  }

  rc = ir0_stat_path_routed(resolved, &kst);

  if (rc != 0)
    return rc;
  if (ir0_copy_stat_to_user(buf, &kst) != 0)
    return -EFAULT;
  return 0;
}

int64_t sys_unlinkat(int dirfd, const char *pathname, int flags)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;
  if (flags & ~AT_REMOVEDIR)
    return -EINVAL;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if (flags & AT_REMOVEDIR)
    return vfs_rmdir_recursive(resolved);

  rc = named_symlink_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  rc = named_fifo_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  rc = named_devnode_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  if (posix_shm_path_is(resolved))
    return posix_shm_try_unlink(resolved);

  rc = vfs_unlink(resolved);
  if (rc == -EISDIR)
    return vfs_rmdir_recursive(resolved);
  return rc;
}

int64_t sys_renameat(int olddirfd, const char *oldpath,
                     int newdirfd, const char *newpath)
{
  char old_resolved[256];
  char new_resolved[256];
  int rc;

  if (!current_process || !oldpath || !newpath)
    return -EFAULT;
  if (validate_userspace_string(oldpath, 256) != 0)
    return -EFAULT;
  if (validate_userspace_string(newpath, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(olddirfd, oldpath, old_resolved, sizeof(old_resolved));
  if (rc != 0)
    return rc;
  rc = ir0_resolve_path_at(newdirfd, newpath, new_resolved, sizeof(new_resolved));
  if (rc != 0)
    return rc;

  (void)named_fifo_unlink(new_resolved);
  (void)named_symlink_unlink(new_resolved);

  rc = ir0_supervise_prepare_rename(old_resolved);
  if (rc != 0)
    return rc;
  rc = ir0_supervise_prepare_rename(new_resolved);
  if (rc != 0)
    return rc;

  return vfs_rename(old_resolved, new_resolved);
}

int64_t sys_readlink(const char *pathname, char *buf, size_t bufsiz)
{
  return do_readlinkat(IR0_AT_FDCWD, pathname, buf, bufsiz);
}

int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
  if (dirfd != IR0_AT_FDCWD)
    return -ENOSYS;
  return do_readlinkat(dirfd, pathname, buf, bufsiz);
}

int64_t sys_symlink(const char *target, const char *linkpath)
{
  return do_symlinkat(target, IR0_AT_FDCWD, linkpath);
}

int64_t sys_symlinkat(const char *target, int dirfd, const char *linkpath)
{
  if (dirfd != IR0_AT_FDCWD)
    return -ENOSYS;
  return do_symlinkat(target, dirfd, linkpath);
}

int64_t sys_fchmod(int fd, mode_t mode)
{
  (void)fd;
  (void)mode;
  return -ENOSYS;
}

int64_t sys_fchown(int fd, uid_t owner, gid_t group)
{
  (void)fd;
  (void)owner;
  (void)group;
  return -ENOSYS;
}

int64_t sys_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags)
{
  (void)dirfd;
  (void)pathname;
  (void)mode;
  (void)flags;
  return -ENOSYS;
}

int64_t sys_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group,
		     int flags)
{
  (void)dirfd;
  (void)pathname;
  (void)owner;
  (void)group;
  (void)flags;
  return -ENOSYS;
}

/*
 * mknod for character/block nodes. Linux gates these behind CAP_MKNOD; IR0
 * has no capabilities yet, so root is the equivalent gate. The node records
 * only the (major,minor): the behaviour comes from the devfs node claiming
 * that number, so an unclaimed one is -ENXIO rather than a file that opens
 * into nothing.
 */
static int64_t mknod_create_device(const char *resolved, unsigned int mode,
                                   unsigned int dev)
{
  if (current_process->euid != ROOT_UID)
    return -EPERM;

  if (!devfs_find_node_by_rdev((uint32_t)dev))
    return -ENXIO;

  return named_devnode_create(resolved,
                              (mode_t)((mode & S_IFMT) | (mode & 07777)),
                              (dev_t)dev);
}

int64_t sys_mknod(const char *pathname, unsigned int mode, unsigned int dev)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  if ((mode & S_IFMT) != S_IFIFO &&
      (mode & S_IFMT) != S_IFCHR &&
      (mode & S_IFMT) != S_IFBLK)
    return -ENOSYS;

  if ((mode & S_IFMT) == S_IFIFO && dev != 0)
    return -EINVAL;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  if ((mode & S_IFMT) != S_IFIFO)
    return mknod_create_device(resolved, mode, dev);

  rc = mknod_prepare_fifo_path(resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  return mknod_create_named_fifo(resolved, (mode_t)(mode & 07777));
}

int64_t sys_mknodat(int dirfd, const char *pathname, unsigned int mode,
		    unsigned int dev)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  if ((mode & S_IFMT) != S_IFIFO &&
      (mode & S_IFMT) != S_IFCHR &&
      (mode & S_IFMT) != S_IFBLK)
    return -ENOSYS;

  if ((mode & S_IFMT) == S_IFIFO && dev != 0)
    return -EINVAL;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if ((mode & S_IFMT) != S_IFIFO)
    return mknod_create_device(resolved, mode, dev);

  rc = mknod_prepare_fifo_path(resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  return mknod_create_named_fifo(resolved, (mode_t)(mode & 07777));
}

int64_t sys_flock(int fd, int operation)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use)
    return -EBADF;

  /* Pipes and pseudo fds have no open file description to lock. */
  if (fd_table[fd].is_pipe || !fd_table[fd].vfs_file)
    return -EINVAL;

  return ir0_flock_apply((struct vfs_file *)fd_table[fd].vfs_file, operation);
}

int64_t sys_fchdir(int fd)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_pipe || fd_table[fd].is_socket)
    return -EBADF;

  if (fd_table[fd].path[0] == '\0')
    return -EBADF;

  /*
   * fd_table[].path is a kernel string. sys_chdir() rejects non-userspace
   * pointers with -EFAULT, which broke runsvdir's fchdir(curdir) loop and
   * left services with a wrong cwd (./run ENOENT, supervise create fails).
   */
  return ir0_chdir_resolved(fd_table[fd].path);
}

/* Linux getdents (NR 78) — legacy filldir layout: d_type at reclen-1. */
struct linux_dirent {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  char d_name[];
};

#define LINUX_DIRENT_NAME_OFF ((size_t)offsetof(struct linux_dirent, d_name))

/* Linux getdents64 / musl x86_64 struct dirent (NR 217, also IR0 NR 78 for musl). */
struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

#define LINUX_DIRENT64_NAME_OFF ((size_t)offsetof(struct linux_dirent64, d_name))

/*
 * Keep batch modest: each vfs_dirent is large (path-sized name). 64 × that
 * plus a 4K bounce buffer blew most of IR0_PROC_KSTACK_SIZE and hung/paused
 * guests on ls /proc under GTK (-no-shutdown looked like a freeze).
 */
#define GETDENTS_BATCH_MAX 24

/* Directory entry types (Linux DT_* subset) */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

static int getdents_skip_name(const char *name)
{
  size_t name_len;

  if (!name || name[0] == '\0')
    return 1;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 1;
  if ((unsigned char)name[0] <= ' ')
    return 1;
  if (strchr(name, '/'))
    return 1;

  name_len = strlen(name) + 1;
  if (name_len <= 1 || name_len >= 256)
    return 1;

  return 0;
}

static int getdents_trust_readdir_type(const char *dir_path)
{
  if (!dir_path)
    return 0;

  if (strcmp(dir_path, "/proc") == 0 || strcmp(dir_path, "/proc/") == 0)
    return 1;
  if (is_proc_path(dir_path) && proc_is_virtual_subdir(dir_path))
    return 1;

  if (strcmp(dir_path, "/sys") == 0 || strcmp(dir_path, "/sys/") == 0)
    return 1;
  if (is_sys_path(dir_path) && sysfs_is_virtual_subdir(dir_path))
    return 1;

  if (is_heart_path(dir_path) && heart_is_virtual_subdir(dir_path))
    return 1;

  if (strcmp(dir_path, "/dev") == 0 || strcmp(dir_path, "/dev/") == 0)
    return 1;
  if (ir0_is_dev_path(dir_path) && devfs_is_virtual_subdir(dir_path))
    return 1;

  return 0;
}

static void getdents_entry_meta(const char *dir_path, const struct vfs_dirent *ent,
                                int seq, uint64_t *ino, uint8_t *dtype)
{
  char full_path[512];
  size_t path_len;

  *ino = (uint64_t)(seq + 1);
  *dtype = ent->type ? ent->type : DT_REG;

  if (ent->type != 0 || getdents_trust_readdir_type(dir_path))
    return;

  path_len = strlen(dir_path);
  if (path_len > 0 && dir_path[path_len - 1] == '/')
    snprintf(full_path, sizeof(full_path), "%s%s", dir_path, ent->name);
  else
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->name);

  {
    stat_t entry_st;

    if (ir0_stat_path_routed(full_path, &entry_st) >= 0)
    {
      if (entry_st.st_ino)
        *ino = (uint64_t)entry_st.st_ino;
      if (S_ISDIR(entry_st.st_mode))
        *dtype = DT_DIR;
      else if (S_ISREG(entry_st.st_mode))
        *dtype = DT_REG;
      else if (S_ISCHR(entry_st.st_mode))
        *dtype = DT_CHR;
      else if (S_ISBLK(entry_st.st_mode))
        *dtype = DT_BLK;
      else if (S_ISLNK(entry_st.st_mode))
        *dtype = DT_LNK;
      else if (S_ISFIFO(entry_st.st_mode))
        *dtype = DT_FIFO;
    }
  }
}

static int getdents_visible_count(const struct vfs_dirent *entries, int entry_count)
{
  int visible = 0;

  for (int i = 0; i < entry_count; i++)
  {
    if (getdents_skip_name(entries[i].name))
      continue;
    visible++;
  }

  return visible;
}

/* Linux filldir: ALIGN(name_off + strlen(name) + 2, 8) — +2 = NUL + d_type byte. */
static size_t linux_dirent_reclen(size_t name_len_with_nul)
{
  size_t namlen = name_len_with_nul - 1U;

  return (LINUX_DIRENT_NAME_OFF + namlen + 2U + 7U) & ~((size_t)7U);
}

/* Linux filldir64: ALIGN(name_off + strlen(name) + 1, 8). */
static size_t linux_dirent64_reclen(size_t name_len_with_nul)
{
  size_t namlen = name_len_with_nul - 1U;

  return (LINUX_DIRENT64_NAME_OFF + namlen + 1U + 7U) & ~((size_t)7U);
}

static const char *fs_fd_resolved_dir_path(int fd, char *buf, size_t bufsz)
{
  fd_entry_t *fd_table;
  const char *path;

  fd_table = get_process_fd_table();
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS ||
      !fd_table[fd].in_use)
    return NULL;

  path = fd_table[fd].path;
  if (!path || path[0] == '\0')
  {
    struct vfs_file *vf = (struct vfs_file *)fd_table[fd].vfs_file;

    if (vf && vf->path[0] != '\0')
      path = vf->path;
    else
      return NULL;
  }

  if (is_absolute_path(path))
  {
    if (normalize_path(path, buf, bufsz) != 0)
      return NULL;
    return buf;
  }

  {
    const char *cwd = current_process->cwd;

    if (!cwd || cwd[0] != '/')
      cwd = "/";
    if (strcmp(path, ".") == 0)
    {
      strncpy(buf, cwd, bufsz - 1);
      buf[bufsz - 1] = '\0';
    }
    else if (join_paths(cwd, path, buf, bufsz) != 0)
      return NULL;
  }

  return buf;
}

#define GETDENTS_RESOLVED_MAX 512
#define GETDENTS_KBUF_MAX 4096

/*
 * Directory listing scratch, on the heap rather than on the stack.
 *
 * These three buffers made sys_getdents_common's frame 11 KiB. A getdents
 * over virtio-9p reaches hs_readdir and virtio_9p_readdir, each with a 4 KiB
 * frame of its own, so a single `ls` on a 9p directory spent 20 KiB of a
 * 32 KiB kernel stack. An interrupt landing on top of that chain ran into
 * the guard page, and the resulting fault escalated to a double fault: that
 * is what a `find` over the host share hit. Off the stack the chain costs a
 * few hundred bytes and leaves the interrupt room to run.
 */
struct getdents_scratch
{
  char resolved_dir[GETDENTS_RESOLVED_MAX];
  struct vfs_dirent entries[GETDENTS_BATCH_MAX];
  char kernel_buf[GETDENTS_KBUF_MAX];
};

static int64_t sys_getdents_scratch(int fd, void *dirent, size_t count,
                                    int legacy_layout,
                                    struct getdents_scratch *scr);

static int64_t sys_getdents_common(int fd, void *dirent, size_t count, int legacy_layout)
{
  struct getdents_scratch *scr = kmalloc_try(sizeof(*scr));
  int64_t rc;

  if (!scr)
    return -ENOMEM;

  rc = sys_getdents_scratch(fd, dirent, count, legacy_layout, scr);
  kfree(scr);
  return rc;
}

static int64_t sys_getdents_scratch(int fd, void *dirent, size_t count,
                                    int legacy_layout,
                                    struct getdents_scratch *scr)
{
  fd_entry_t *fd_table;
  char *const resolved_dir = scr->resolved_dir;
  const char *dir_path;
  stat_t st;
  struct vfs_dirent *const entries = scr->entries;
  int entry_count;
  int visible_count;
  size_t start_cookie;
  size_t returned;
  size_t user_budget;
  size_t user_off;
  char *const kernel_buf = scr->kernel_buf;
  size_t buf_offset;
  int64_t copy_size;

  if (!current_process)
    return -ESRCH;
  if (!dirent || count == 0)
    return -EINVAL;

  if (validate_userspace_buffer(dirent, count) != 0)
    return -EFAULT;

  fd_table = get_process_fd_table();
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS ||
      !fd_table[fd].in_use)
    return -EBADF;

  dir_path = fs_fd_resolved_dir_path(fd, resolved_dir, GETDENTS_RESOLVED_MAX);
  if (!dir_path)
    return -EBADF;

  if (ir0_stat_path_routed(dir_path, &st) < 0)
    return -ENOTDIR;

  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  entry_count = ir0_getdents_path_routed(dir_path, entries, GETDENTS_BATCH_MAX);
  if (entry_count < 0)
    return entry_count;

  visible_count = getdents_visible_count(entries, entry_count);

  start_cookie = (size_t)fd_table[fd].offset;
  if (start_cookie >= (size_t)visible_count)
    return 0;

  returned = 0;
  buf_offset = 0;

  {
    int visible_idx = 0;

    for (int i = 0; i < entry_count; i++)
    {
      size_t name_len;
      size_t reclen;
      uint64_t ino;
      uint8_t dtype;

      if (getdents_skip_name(entries[i].name))
        continue;

      if ((size_t)visible_idx < start_cookie)
      {
        visible_idx++;
        continue;
      }

      name_len = strlen(entries[i].name) + 1;
      getdents_entry_meta(dir_path, &entries[i], visible_idx, &ino, &dtype);

      if (legacy_layout)
        reclen = linux_dirent_reclen(name_len);
      else
        reclen = linux_dirent64_reclen(name_len);

      if (buf_offset + reclen > GETDENTS_KBUF_MAX)
        break;

      if (legacy_layout)
      {
        struct linux_dirent *dent =
            (struct linux_dirent *)(kernel_buf + buf_offset);

        memset(dent, 0, reclen);
        dent->d_ino = ino;
        dent->d_off = (int64_t)(start_cookie + returned + 1);
        dent->d_reclen = (unsigned short)reclen;
        memcpy(dent->d_name, entries[i].name, name_len);
        ((char *)dent)[reclen - 1U] = (char)dtype;
      }
      else
      {
        struct linux_dirent64 *dent =
            (struct linux_dirent64 *)(kernel_buf + buf_offset);

        memset(dent, 0, reclen);
        dent->d_ino = ino;
        dent->d_off = (int64_t)(start_cookie + returned + 1);
        dent->d_reclen = (unsigned short)reclen;
        dent->d_type = dtype;
        memcpy(dent->d_name, entries[i].name, name_len);
      }

      buf_offset += reclen;
      returned++;
      visible_idx++;
    }
  }

  if (buf_offset == 0)
    return 0;

  user_budget = count;
  user_off = 0;
  while (user_off < buf_offset)
  {
    unsigned short dent_reclen;
    size_t min_reclen;

    if (legacy_layout)
    {
      struct linux_dirent *dent =
          (struct linux_dirent *)(kernel_buf + user_off);

      dent_reclen = dent->d_reclen;
      min_reclen = LINUX_DIRENT_NAME_OFF + 2U;
    }
    else
    {
      struct linux_dirent64 *dent =
          (struct linux_dirent64 *)(kernel_buf + user_off);

      dent_reclen = dent->d_reclen;
      min_reclen = LINUX_DIRENT64_NAME_OFF + 2U;
    }

    if ((size_t)dent_reclen < min_reclen ||
        user_off + (size_t)dent_reclen > buf_offset)
      return -EIO;
    if ((size_t)dent_reclen > user_budget)
      break;
    user_off += (size_t)dent_reclen;
    user_budget -= (size_t)dent_reclen;
  }

  if (user_off == 0)
    return -EINVAL;

  copy_size = (int64_t)user_off;
  if (copy_to_user(dirent, kernel_buf, (size_t)copy_size) != 0)
    return -EFAULT;

  {
    size_t copied = 0;
    size_t pos = 0;

    while (pos < user_off)
    {
      unsigned short dent_reclen;
      size_t min_reclen;

      if (legacy_layout)
      {
        struct linux_dirent *dent =
            (struct linux_dirent *)(kernel_buf + pos);

        dent_reclen = dent->d_reclen;
        min_reclen = LINUX_DIRENT_NAME_OFF + 2U;
      }
      else
      {
        struct linux_dirent64 *dent =
            (struct linux_dirent64 *)(kernel_buf + pos);

        dent_reclen = dent->d_reclen;
        min_reclen = LINUX_DIRENT64_NAME_OFF + 2U;
      }

      if ((size_t)dent_reclen < min_reclen ||
          pos + (size_t)dent_reclen > user_off)
        break;
      pos += (size_t)dent_reclen;
      copied++;
    }
    fd_table[fd].offset = start_cookie + copied;
  }

  return copy_size;
}

int64_t sys_getdents(int fd, void *dirent, size_t count)
{
  /*
   * musl x86_64 readdir(3) issues SYS_getdents (78) but struct dirent matches
   * linux_dirent64 (d_type before d_name). Emit dirent64 records on NR 78.
   * Legacy filldir layout is reserved for strict Linux NR 78 consumers via
   * getdents64-only paths or future compat toggle.
   */
  return sys_getdents_common(fd, dirent, count, 0);
}

int64_t sys_getdents64(int fd, void *dirent, size_t count)
{
  return sys_getdents_common(fd, dirent, count, 0);
}
