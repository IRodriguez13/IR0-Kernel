/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58K — compact ash interactive smoke tags.
 */

#include <ir0/ash_smoke.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#define ASH_READ_RETURN_CAP 8

static int ash_smoke_active;
static int tty_line_ready_count;

#define ASH_TTY_LINE_CAP 4
static int read_return_count;
static int echo_ok_once;
static int exec_ok_once;

static int ash_memmem(const char *hay, size_t hlen, const char *needle, size_t nlen)
{
	size_t i;

	if (!hay || !needle || nlen == 0 || hlen < nlen)
		return 0;

	for (i = 0; i + nlen <= hlen; i++)
	{
		if (memcmp(hay + i, needle, nlen) == 0)
			return 1;
	}
	return 0;
}

static void ash_smoke_arm(void)
{
	if (ash_smoke_active)
		return;
	ash_smoke_active = 1;
}

int ir0_ash_smoke_active(void)
{
	return ash_smoke_active;
}

void ir0_ash_smoke_scan_write(const char *buf, size_t count)
{
	static const char marker[] = "BusyBox v";
	size_t i;

	if (ash_smoke_active || !buf || count < sizeof(marker) - 1)
		return;

	for (i = 0; i + sizeof(marker) - 1 <= count; i++)
	{
		if (memcmp(buf + i, marker, sizeof(marker) - 1) == 0)
		{
			ash_smoke_arm();
			return;
		}
	}
}

void ir0_ash_smoke_tty_line_ready(void)
{
	if (!ash_smoke_active || tty_line_ready_count >= ASH_TTY_LINE_CAP)
		return;
	tty_line_ready_count++;
	klog_smoke("TTY_CANON_LINE_READY");
}

void ir0_ash_smoke_read_return(int fd, int64_t ret)
{
	if (!ash_smoke_active || fd != 0 || ret <= 0)
		return;
	if (read_return_count >= ASH_READ_RETURN_CAP)
		return;
	read_return_count++;
	klog_smoke("SYS_READ_RETURN_OK");
}

void ir0_ash_smoke_scan_stdout(const char *buf, size_t count)
{
	size_t i;

	if (!ash_smoke_active || !buf || count == 0)
		return;

	if (!echo_ok_once &&
	    (ash_memmem(buf, count, "hi\n", 3) ||
	     ash_memmem(buf, count, "hi\r\n", 4)))
	{
		echo_ok_once = 1;
		klog_smoke("ASH_COMMAND_ECHO_OK");
	}

	if (exec_ok_once)
		return;

	for (i = 0; i + 1 < count; i++)
	{
		if (buf[i] != '/')
			continue;
		if (buf[i + 1] == '\n')
		{
			exec_ok_once = 1;
			klog_smoke("ASH_COMMAND_EXEC_OK");
			return;
		}
		if (i + 2 < count && buf[i + 1] == '\r' && buf[i + 2] == '\n')
		{
			exec_ok_once = 1;
			klog_smoke("ASH_COMMAND_EXEC_OK");
			return;
		}
	}
}
