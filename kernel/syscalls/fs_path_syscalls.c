/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fs_path_syscalls.c
 * Description: path-based filesystem syscalls (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/syscalls.h>
#include <kernel/process.h>
#include "fs_path_syscalls.h"
#include "syscalls_glue.h"
#include <config.h>
#include <ir0/chmod.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/named_fifo.h>
#include <ir0/named_symlink.h>
#include <ir0/path.h>
#include <ir0/path_routed.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/credentials.h>
#include <ir0/ktm/klog.h>
#include <ir0/stat.h>
#include <ir0/utimens.h>
#include <ir0/arch_cpu.h>
#include <ir0/utsname.h>
#include <ir0/utsname_info.h>
#include <ir0/version.h>
#include <ir0/vfs.h>
#include <ir0/statfs.h>
#include <ir0/memfd.h>
#include <ir0/posix_shm.h>
#include <stddef.h>
#include <string.h>

static void process_cwd_ensure_absolute(process_t *proc)
{
  if (!proc)
    return;
  if (proc->cwd[0] == '/')
    return;
  strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
  proc->cwd[sizeof(proc->cwd) - 1] = '\0';
}

/*
 * Linux mount(2): mount(source, target, fstype, flags, data).
 * Remount: flags & MS_REMOUNT — source/fstype/data ignored (man7 mount.2).
 * Legacy IR0 recovery still accepts mount("remount", path, "ro"|"rw", 0, NULL).
 */
int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype,
		  unsigned long flags, const void *data)
{
  char mountpoint_resolved[256];
  char dev_copy[256];
  char dev_resolved[256];
  char fstype_buf[32];
  const char *mount_path;
  const char *dev_path;
  const char *mount_fstype;
  int dev_is_pseudo = 0;
  int rc;

  (void)data;

  if (!current_process)
    return -ESRCH;

  if (!mountpoint)
    return -EFAULT;

  if (validate_userspace_string(mountpoint, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(mountpoint, mountpoint_resolved,
                             sizeof(mountpoint_resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;
  mount_path = mountpoint_resolved;

  /* Linux remount via flags (BusyBox umount -r, mount -o remount,ro). */
  if (flags & IR0_MS_REMOUNT)
    return vfs_remount(mount_path, flags);

  if (!dev)
    return -EFAULT;

  if (validate_userspace_string(dev, 256) != 0)
    return -EFAULT;
  if (fstype && validate_userspace_string(fstype, 32) != 0)
    return -EFAULT;

  if (copy_from_user_cstring(dev_copy, sizeof(dev_copy), dev) != 0)
    return -EFAULT;

  if (fstype)
  {
    if (copy_from_user_cstring(fstype_buf, sizeof(fstype_buf), fstype) != 0)
      return -EFAULT;
    mount_fstype = fstype_buf[0] ? fstype_buf : CONFIG_ROOT_FILESYSTEM;
  }
  else
  {
    mount_fstype = CONFIG_ROOT_FILESYSTEM;
  }

  /* tmpfs / 9p accept pseudo device strings (none, mount_tag, …). */
  if (strcmp(mount_fstype, "tmpfs") == 0 &&
      (strcmp(dev_copy, "none") == 0 || strcmp(dev_copy, "tmpfs") == 0))
  {
    dev_is_pseudo = 1;
    dev_path = dev_copy;
  }
  else if (strcmp(mount_fstype, "9p") == 0 &&
           dev_copy[0] != '\0' && strchr(dev_copy, '/') == NULL)
  {
    /* QEMU virtio-9p mount_tag (e.g. ir0share, dennis). */
    dev_is_pseudo = 1;
    dev_path = dev_copy;
  }
  else
  {
    rc = ir0_resolve_kpath_at(IR0_AT_FDCWD, dev_copy, dev_resolved,
                              sizeof(dev_resolved), current_process->cwd, current_process->root);
    if (rc != 0)
      return rc;
    dev_path = dev_resolved;
  }

  /* Legacy string remount for IR0 recovery helpers. */
  if (strcmp(dev_copy, "remount") == 0)
  {
    unsigned long rflags = IR0_MS_REMOUNT;

    if (strcmp(mount_fstype, "ro") == 0 ||
        strcmp(mount_fstype, "remount,ro") == 0)
      rflags |= IR0_MS_RDONLY;
    else if (strcmp(mount_fstype, "rw") != 0 &&
             strcmp(mount_fstype, "remount,rw") != 0)
      return -EINVAL;
    return vfs_remount(mount_path, rflags);
  }

  if (flags & IR0_MS_UNSUPPORTED)
    return -EINVAL;

  /* Validate device path unless pseudo device was allowed. */
  if (!dev_is_pseudo && (dev_path[0] != '/' || strlen(dev_path) >= 256))
  {
    sys_write(STDERR_FILENO, "mount: invalid device path\n", 27);
    return -EINVAL;
  }

  /* Validate mountpoint path */
  if (mount_path[0] != '/' || strlen(mount_path) >= 256)
  {
    sys_write(STDERR_FILENO, "mount: invalid mount point\n", 27);
    return -EINVAL;
  }

  /* Check if mountpoint exists and is a directory */
  stat_t st;
  if (vfs_stat(mount_path, &st) < 0)
  {
    sys_write(STDERR_FILENO, "mount: mount point does not exist\n", 34);
    return -ENOENT;
  }
  if (!S_ISDIR(st.st_mode))
  {
    sys_write(STDERR_FILENO, "mount: mount point is not a directory\n", 38);
    return -ENOTDIR;
  }
  rc = vfs_mount(dev_path, mount_path, mount_fstype);
  if (rc < 0)
  {
    /* Report specific error (skip noise for expected EBUSY remount attempts). */
    if (rc != -EBUSY)
    {
      sys_write(STDERR_FILENO, "mount: failed to mount ", 22);
      sys_write(STDERR_FILENO, mount_fstype, strlen(mount_fstype));
      sys_write(STDERR_FILENO, " filesystem\n", 12);
    }
    return rc;
  }

  if (flags & IR0_MS_RDONLY)
  {
    rc = vfs_remount(mount_path, IR0_MS_REMOUNT | IR0_MS_RDONLY);
    if (rc < 0)
      return rc;
  }

  return 0;
}

int64_t sys_umount(const char *target, int flags)
{
  char target_resolved[256];
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!target)
    return -EFAULT;

  if (validate_userspace_string(target, 256) != 0)
    return -EFAULT;

  if (flags != 0)
    return -EINVAL;

  rc = ir0_resolve_user_path(target, target_resolved, sizeof(target_resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  if (target_resolved[0] != '/' || strlen(target_resolved) >= 256)
  {
    sys_write(STDERR_FILENO, "umount: invalid target path\n", 28);
    return -EINVAL;
  }

  stat_t st;
  if (vfs_stat(target_resolved, &st) < 0)
  {
    sys_write(STDERR_FILENO, "umount: target path does not exist\n", 35);
    return -ENOENT;
  }

  return vfs_umount(target_resolved);
}

int64_t sys_sync(void)
{
  klog_smoke("SYSTEM_SYNC_OK");
  return vfs_sync();
}

/*
 * fsync(2)/fdatasync(2).
 *
 * IR0 tracks no per-file dirty state, so the flush is the global vfs_sync():
 * flushing more than the requested file still satisfies the guarantee that
 * this file's data reached the backing store. Descriptors with no backing
 * store (pseudo-fs, devfs, memfd) have nothing to write back and return 0;
 * pipes and sockets return -EINVAL, as Linux does for special files that do
 * not support synchronization (fsync(2) ERRORS).
 */
int64_t sys_fsync(int fd)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_pipe || fd_table[fd].is_socket)
    return -EINVAL;

  if (fd_table[fd].is_pseudo || fd_table[fd].is_devfs ||
      fd_table[fd].is_memfd)
    return 0;

  return vfs_sync();
}

int64_t sys_fdatasync(int fd)
{
  return sys_fsync(fd);
}

int64_t sys_mkdir(const char *pathname, mode_t mode)
{
  char resolved[256];
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  return vfs_mkdir(resolved, (int)mode);
}

int64_t sys_chmod(const char *path, mode_t mode)
{
  if (!current_process || !path)
    return -EFAULT;

  /* Resolve relative paths against cwd; absolute under chroot. */
  char resolved[256];
  const char *path_to_use = path;
  int rc;

  if (validate_userspace_string(path, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(path, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;
  path_to_use = resolved;

  stat_t st;
  int sret = vfs_stat(path_to_use, &st);
  if (sret != 0)
    return sret;

  if (current_process->euid != ROOT_UID && current_process->euid != st.st_uid)
    return -EPERM;

  return chmod(path_to_use, mode);
}

int64_t sys_chown(const char *path, uid_t owner, gid_t group)
{
  char resolved[256];
  int rc;

  if (!current_process || !path)
    return -EFAULT;
  if (validate_userspace_string(path, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(path, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  if (current_process->euid != ROOT_UID)
    return -EPERM;

  return vfs_chown(resolved, owner, group);
}

int64_t sys_link(const char *oldpath, const char *newpath)
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

  rc = ir0_resolve_user_path(oldpath, old_resolved, sizeof(old_resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  rc = ir0_resolve_user_path(newpath, new_resolved, sizeof(new_resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  return vfs_link(old_resolved, new_resolved);
}

/**
 * sys_rename - Rename/move file (POSIX)
 * @oldpath: Source path
 * @newpath: Destination path
 *
 * Returns: 0 on success, negative error code on failure
 */
int64_t sys_rename(const char *oldpath, const char *newpath)
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

  rc = ir0_resolve_user_path(oldpath, old_resolved, sizeof(old_resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  rc = ir0_resolve_user_path(newpath, new_resolved, sizeof(new_resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  (void)named_fifo_unlink(new_resolved);
  (void)named_symlink_unlink(new_resolved);

  return vfs_rename(old_resolved, new_resolved);
}

/**
 * sys_uname - Get system information (POSIX/Linux)
 * @buf: struct utsname buffer
 *
 * Returns: 0 on success, negative error code on failure
 */
int64_t sys_uname(struct utsname *buf)
{
  char version[64];
  char nodename[64];
  struct utsname kbuf;

  if (!current_process || !buf)
    return -EFAULT;

  if (validate_userspace_buffer(buf, sizeof(struct utsname)) != 0)
    return -EFAULT;

  memset(&kbuf, 0, sizeof(kbuf));
  ir0_utsname_fill_version(version, sizeof(version));
  ir0_utsname_fill_nodename(nodename, sizeof(nodename));
  /* sysname nodename release version machine — version/nodename from live state. */
  strncpy(kbuf.sysname, "IR0", _UTSNAME_LENGTH - 1);
  strncpy(kbuf.nodename, nodename, _UTSNAME_LENGTH - 1);
  strncpy(kbuf.release, IR0_VERSION_STRING, _UTSNAME_LENGTH - 1);
  strncpy(kbuf.version, version, _UTSNAME_LENGTH - 1);
  /*
   * PER_LINUX32 (set by linux32(1) via personality(2)) makes uname report a
   * 32-bit machine on the same 64-bit kernel, as Linux does in
   * arch/x86/kernel/sys_x86_64.c.
   */
  if (current_process && current_process->personality == 0x0008)
    strncpy(kbuf.machine, "i686", _UTSNAME_LENGTH - 1);
  else
    strncpy(kbuf.machine, get_arch_uname_machine(), _UTSNAME_LENGTH - 1);

  if (copy_to_user(buf, &kbuf, sizeof(kbuf)) != 0)
    return -EFAULT;
  return 0;
}

/**
 * sys_access - Check file access (POSIX)
 * @pathname: Path to check
 * @mode: F_OK, R_OK, W_OK, X_OK
 *
 * Returns: 0 if accessible, negative error code on failure
 */
int64_t sys_access(const char *pathname, int mode)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  return ir0_access_path_routed(resolved, mode,
                                (uid_t)current_process->euid,
                                (gid_t)current_process->egid);
}

int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags)
{
  char resolved[256];
  int rc;
  int masked_flags;

  if (!current_process || !pathname)
    return -EFAULT;
  masked_flags = flags & ~(IR0_AT_EACCESS | IR0_AT_SYMLINK_NOFOLLOW);
  if (masked_flags != 0)
    return -EINVAL;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  /*
   * IR0 currently evaluates access permissions against effective IDs.
   * AT_EACCESS is accepted and maps to this same behavior.
   * AT_SYMLINK_NOFOLLOW is accepted as a no-op because symlinks are not implemented.
   */
  return ir0_access_path_routed(resolved, mode,
                                (uid_t)current_process->euid,
                                (gid_t)current_process->egid);
}

/**
 * sys_rmdir - Remove directory (POSIX)
 * @pathname: Path to directory to remove
 *
 * POSIX: only removes empty directories (ENOTEMPTY if non-empty).
 * Avoids vfs_rmdir_recursive to prevent VM hang on disk I/O.
 *
 * Returns: 0 on success, negative error code on failure
 */
int64_t sys_rmdir(const char *pathname)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  return vfs_rmdir(resolved);
}

int64_t ir0_chdir_resolved(const char *pathname)
{
  int64_t ret;
  size_t len;
  char new_path[256];
  stat_t st;

  if (!current_process || !pathname)
    return -EFAULT;

  len = strlen(pathname);
  if (len == 0 || len >= 256)
    return -EINVAL;

  /*
   * pathname is a kernel/host path (already chroot-mapped by sys_chdir,
   * or an open directory path from fchdir).
   */
  if (is_absolute_path(pathname))
  {
    if (normalize_path(pathname, new_path, sizeof(new_path)) != 0)
      return -ENAMETOOLONG;
  }
  else
  {
    if (join_paths(current_process->cwd, pathname, new_path, sizeof(new_path)) != 0)
      return -ENAMETOOLONG;
  }

  ret = ir0_stat_path_routed(new_path, &st);
  if (ret < 0)
    return ret;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  /* In Unix, entering a directory requires execute permission on that path. */
  ret = ir0_access_path_routed(new_path, 1,
                               (uid_t)current_process->euid,
                               (gid_t)current_process->egid);
  if (ret != 0)
    return ret;

  strncpy(current_process->cwd, new_path, sizeof(current_process->cwd) - 1);
  current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';
  process_cwd_ensure_absolute(current_process);

  return 0;
}

int64_t sys_chdir(const char *pathname)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  return ir0_chdir_resolved(resolved);
}

int64_t sys_getcwd(char *buf, size_t size)
{
  char visible[256];
  size_t len;

  if (!current_process || !buf || size == 0)
    return -EFAULT;

  if (validate_userspace_buffer(buf, size) != 0)
    return -EFAULT;

  process_cwd_ensure_absolute(current_process);
  if (current_process->root[0] != '/')
  {
    strncpy(current_process->root, "/", sizeof(current_process->root) - 1);
    current_process->root[sizeof(current_process->root) - 1] = '\0';
  }

  if (ir0_path_getcwd_visible(current_process->root, current_process->cwd,
                              visible, sizeof(visible)) != 0)
    return -ENAMETOOLONG;

  len = strlen(visible);
  if (len >= size)
    return -ERANGE;

  if (copy_to_user(buf, visible, len + 1) != 0)
    return -EFAULT;

  return (int64_t)len;
}

int64_t sys_chroot(const char *path)
{
  char resolved[256];
  stat_t st;
  int rc;
  int64_t ret;

  if (!current_process || !path)
    return -EFAULT;

  if (!ir0_cred_is_root())
    return -EPERM;

  if (validate_userspace_string(path, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(path, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  ret = ir0_stat_path_routed(resolved, &st);
  if (ret < 0)
    return ret;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  /*
   * Linux: does not change cwd. Absolute paths after this use the new root.
   */
  strncpy(current_process->root, resolved, sizeof(current_process->root) - 1);
  current_process->root[sizeof(current_process->root) - 1] = '\0';
  return 0;
}

int64_t sys_utimensat(int dirfd, const char *pathname,
                      const struct timespec *times, int flags)
{
  char resolved[256];
  struct timespec ktimes[2];
  int rc;

  if (!current_process)
    return -EFAULT;

  /* futimens(fd, times): utimensat(fd, NULL, times, 0) on the open file. */
  if (!pathname)
  {
    fd_entry_t *slot;

    if (dirfd < 0 || dirfd >= MAX_FDS_PER_PROCESS)
      return -EBADF;
    slot = process_fd_table(current_process);
    if (!slot)
      return -EBADF;
    slot = &slot[dirfd];
    if (!slot->in_use || slot->path[0] == '\0')
      return -EBADF;
    if (strlen(slot->path) >= sizeof(resolved))
      return -ENAMETOOLONG;
    strcpy(resolved, slot->path);
  }
  else
  {
    if (dirfd != IR0_AT_FDCWD)
      return -ENOSYS;

    if (validate_userspace_string(pathname, 256) != 0)
      return -EFAULT;

    rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                              current_process->cwd, current_process->root);
    if (rc != 0)
      return rc;
  }

  if (times)
  {
    if (validate_userspace_buffer(times, sizeof(ktimes)) != 0)
      return -EFAULT;
    if (copy_from_user(ktimes, times, sizeof(ktimes)) != 0)
      return -EFAULT;
    return ir0_utimensat_path(resolved, ktimes, flags);
  }

  return ir0_utimensat_path(resolved, NULL, flags);
}

int64_t sys_unlink(const char *pathname)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  if (posix_shm_path_is(resolved))
    return posix_shm_try_unlink(resolved);

  return vfs_unlink(resolved);
}

int64_t sys_truncate(const char *pathname, off_t length)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;
  if (length < 0)
    return -EINVAL;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  return vfs_truncate(resolved, (size_t)length);
}

int64_t sys_ftruncate(int fd, off_t length)
{
  fd_entry_t *fd_table;
  const char *path;
  stat_t st;

  if (!current_process)
    return -ESRCH;
  if (length < 0)
    return -EINVAL;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  /* Pseudo /proc|/sys|/heart binds are not truncatable regular files. */
  if (fd_table[fd].is_pseudo)
    return -EBADF;

  if (fd_table[fd].is_memfd && fd_table[fd].vfs_file)
    return ir0_memfd_ftruncate((struct ir0_memfd *)fd_table[fd].vfs_file,
			       (size_t)length);

  if (fd_table[fd].is_pipe || fd_table[fd].is_socket)
    return -EBADF;

  if (fd <= 2 && !fd_table[fd].vfs_file && !fd_table[fd].is_devfs)
    return -EBADF;

  path = fd_table[fd].path;
  if (!path || path[0] != '/')
    return -EBADF;

  if (vfs_stat(path, &st) != 0)
    return -EBADF;

  if (S_ISDIR(st.st_mode))
    return -EINVAL;

  return vfs_truncate(path, (size_t)length);
}

int64_t sys_statfs(const char *pathname, void *buf)
{
	char resolved[256];
	struct ir0_statfs kst;
	int64_t rc;

	if (!current_process || !pathname || !buf)
		return -EFAULT;
	if (validate_userspace_buffer(buf, sizeof(kst)) != 0)
		return -EFAULT;

	rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
				   current_process->cwd, current_process->root);
	if (rc != 0)
		return rc;

	rc = vfs_statfs(resolved, &kst);
	if (rc != 0)
		return rc;
	if (copy_to_user(buf, &kst, sizeof(kst)) != 0)
		return -EFAULT;
	return 0;
}

int64_t sys_fstatfs(int fd, void *buf)
{
	fd_entry_t *fd_table;
	struct ir0_statfs kst;
	const char *path;
	int rc;

	if (!current_process || !buf)
		return -EFAULT;
	if (validate_userspace_buffer(buf, sizeof(kst)) != 0)
		return -EFAULT;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;

	fd_table = get_process_fd_table();
	if (!fd_table || !fd_table[fd].in_use)
		return -EBADF;

	path = fd_table[fd].path;
	if (!path || path[0] != '/')
		return -EBADF;

	rc = vfs_statfs(path, &kst);
	if (rc != 0)
		return rc;
	if (copy_to_user(buf, &kst, sizeof(kst)) != 0)
		return -EFAULT;
	return 0;
}
