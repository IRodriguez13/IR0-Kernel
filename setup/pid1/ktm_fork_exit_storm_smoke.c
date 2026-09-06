/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0_MM smoke-fork-exit-storm
 * 128 children -> immediate exit -> wait all
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define IR0_MM_FORK_STORM 128

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void write_dec_u64(uint64_t v)
{
	char buf[32];
	int n = 0;

	if (v == 0)
	{
		(void)write(1, "0", 1);
		return;
	}
	while (v > 0 && n < (int)sizeof(buf))
	{
		buf[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	}
	while (n-- > 0)
		(void)write(1, &buf[n], 1);
}

static long read_used_kb(void)
{
	char buf[128];
	int fd;
	ssize_t n;
	long vals[3] = {0, 0, 0};
	int vi = 0;
	long cur = 0;
	int in_num = 0;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	for (ssize_t i = 0; i < n && vi < 3; i++)
	{
		char c = buf[i];
		if (c >= '0' && c <= '9')
		{
			in_num = 1;
			cur = cur * 10 + (long)(c - '0');
		}
		else if (in_num)
		{
			vals[vi++] = cur;
			cur = 0;
			in_num = 0;
		}
	}
	if (in_num && vi < 3)
		vals[vi++] = cur;
	if (vi < 3)
		return -1;
	return vals[2];
}

int main(void)
{
	int started = 0;
	int wait_fail = 0;
	long used_before = read_used_kb();
	long used_after;

	for (int i = 0; i < IR0_MM_FORK_STORM; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
			_exit(0);
		if (pid < 0)
			continue;
		started++;
		if (wait4(-1, NULL, 0, NULL) < 0)
			wait_fail++;
	}

	used_after = read_used_kb();
	write_str("IR0_MM_FORK_EXIT_STORM children=");
	write_dec_u64((uint64_t)started);
	write_str(" wait_fail=");
	write_dec_u64((uint64_t)wait_fail);
	write_str(" pages_before=");
	write_dec_u64((used_before > 0) ? (uint64_t)(used_before / 4) : 0);
	write_str(" pages_after=");
	write_dec_u64((used_after > 0) ? (uint64_t)(used_after / 4) : 0);
	write_str("\n");
	return 0;
}
