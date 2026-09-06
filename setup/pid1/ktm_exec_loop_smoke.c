/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE43 exec-loop
 * 1024x: fork -> exec("/bin/f41true") -> wait
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FASE43_EXEC_LOOPS 1024

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
	long used_before = read_used_kb();
	long used_after;
	int wait_fail = 0;
	int fork_fail = 0;
	char *argv[] = { "/bin/f41true", NULL };

	for (int i = 0; i < FASE43_EXEC_LOOPS; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			execve("/bin/f41true", argv, NULL);
			_exit(127);
		}
		if (pid < 0)
		{
			fork_fail++;
			continue;
		}
		if (wait4(-1, NULL, 0, NULL) < 0)
			wait_fail++;
	}

	used_after = read_used_kb();
	write_str("FASE43_EXEC_LOOP loops=");
	write_dec_u64((uint64_t)FASE43_EXEC_LOOPS);
	write_str(" fork_fail=");
	write_dec_u64((uint64_t)fork_fail);
	write_str(" wait_fail=");
	write_dec_u64((uint64_t)wait_fail);
	write_str(" pages_before=");
	write_dec_u64((used_before > 0) ? (uint64_t)(used_before / 4) : 0);
	write_str(" pages_after=");
	write_dec_u64((used_after > 0) ? (uint64_t)(used_after / 4) : 0);
	write_str("\n");
	return 0;
}
