// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — userspace path copy + resolve for syscall facades.
 */

#include <ir0/path_user.h>
#include <ir0/copy_user.h>
#include <ir0/path.h>
#include <ir0/errno.h>
#include <ir0/utimens.h>

int ir0_resolve_kpath_at(int dirfd, const char *path, char *resolved,
                         size_t resolved_sz, const char *cwd,
                         const char *root)
{
    if (!path || !resolved || resolved_sz == 0)
        return -EINVAL;

    if (path[0] == '\0')
        return -EINVAL;

    if (path[0] == '/')
    {
        if (ir0_path_apply_root(root, path, resolved, resolved_sz) != 0)
            return -ENAMETOOLONG;
        return 0;
    }

    if (dirfd != IR0_AT_FDCWD)
        return -EBADF;

    if (!cwd || cwd[0] != '/')
        cwd = "/";

    if (join_paths(cwd, path, resolved, resolved_sz) != 0)
        return -ENAMETOOLONG;

    return 0;
}

int ir0_resolve_user_path_at(int dirfd, const char *user_path, char *resolved,
                             size_t resolved_sz, const char *cwd,
                             const char *root)
{
    char path_copy[256];

    if (!user_path || !resolved || resolved_sz == 0)
        return -EFAULT;

    if (copy_from_user_cstring(path_copy, sizeof(path_copy), user_path) != 0)
        return -EFAULT;

    return ir0_resolve_kpath_at(dirfd, path_copy, resolved, resolved_sz, cwd,
                                root);
}

int ir0_resolve_user_path(const char *user_path, char *resolved,
                          size_t resolved_sz, const char *cwd,
                          const char *root)
{
    return ir0_resolve_user_path_at(IR0_AT_FDCWD, user_path, resolved,
                                    resolved_sz, cwd, root);
}
