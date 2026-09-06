/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ioctl_tty_probe.c
 * Description: Console TTY ioctl(TCGETS/TIOCGWINSZ/TIOCGPGRP) ABI audit probe
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void audit_io(unsigned step, const char *op, long ret, int err)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][ioctl] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	struct termios t;
	struct winsize ws;
	pid_t pgrp;
	int ret;

	memset(&t, 0, sizeof(t));
	ret = ioctl(0, TCGETS, &t);
	audit_io(0, "tcgets", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 1;

	memset(&ws, 0, sizeof(ws));
	ret = ioctl(0, TIOCGWINSZ, &ws);
	audit_io(1, "tiocgwinsz", ret, ret != 0 ? errno : 0);
	if (ret != 0)
		return 1;
	if (ws.ws_row == 0 || ws.ws_col == 0)
		return 1;

	ret = ioctl(0, TIOCGPGRP, &pgrp);
	audit_io(2, "tiocgpgrp", ret, ret != 0 ? errno : 0);
	if (ret != 0 || pgrp <= 0)
		return 1;

	(void)write(1, "[IOCTLTTYOK]\n", 13);
	return 0;
}
