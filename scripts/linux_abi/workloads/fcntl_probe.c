/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fcntl_probe.c
 * Description: fcntl(F_GETFD/F_SETFD) ABI audit probe
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static void audit_fc(unsigned step, const char *op, long ret, int err)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][fcntl] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	int fd;
	long flags;

	fd = open("/proc/uptime", O_RDONLY);
	audit_fc(0, "open", (long)fd, fd < 0 ? errno : 0);
	if (fd < 0)
		return 1;

	flags = (long)fcntl(fd, F_GETFD);
	audit_fc(1, "fcntl_getfd", flags, flags < 0 ? errno : 0);
	if (flags < 0)
		return 1;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0)
	{
		audit_fc(2, "fcntl_setfd", -1L, errno);
		return 1;
	}
	audit_fc(2, "fcntl_setfd", 0L, 0);

	flags = (long)fcntl(fd, F_GETFD);
	audit_fc(3, "fcntl_getfd_cloexec", flags, flags < 0 ? errno : 0);
	if (flags < 0 || !(flags & FD_CLOEXEC))
		return 1;

	if (close(fd) != 0)
		return 1;

	(void)write(1, "[FCNTLOK]\n", 10);
	return 0;
}
