// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — utimensat path facade (VFS backend, no BusyBox coupling).
 */

#include <ir0/utimens.h>
#include <fs/vfs.h>
#include <ir0/errno.h>
#include <ir0/stat.h>

int ir0_utimensat_path(const char *resolved, const struct timespec times[2],
                       int flags)
{
	stat_t st;

	if (!resolved || resolved[0] == '\0')
		return -EINVAL;

	if (flags & IR0_AT_SYMLINK_NOFOLLOW)
		return -ENOSYS;

	if (vfs_stat(resolved, &st) != 0)
		return -ENOENT;

	(void)times;
	return vfs_utimens(resolved, times);
}
