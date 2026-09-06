/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE45 fork without exec
 * fork → touch stack + heap → exit → wait (512 iterations).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FASE45_FORK_TOUCH 512

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

static void drain_children(void)
{
	pid_t r;

	while ((r = wait4(-1, NULL, WNOHANG, NULL)) > 0)
		(void)r;
	for (;;)
	{
		r = wait4(-1, NULL, 0, NULL);
		if (r < 0)
			break;
	}
}

int main(void)
{
	long frames_before = read_used_kb();
	long frames_after;
	int fork_ok = 0;
	int fork_fail = 0;
	int wait_fail = 0;

	for (int i = 0; i < FASE45_FORK_TOUCH; i++)
	{
		pid_t pid = fork();

		if (pid < 0)
		{
			fork_fail++;
			continue;
		}
		if (pid == 0)
		{
			volatile char stack_buf[4096];
			void *heap_page;
			int idx;

			for (idx = 0; idx < (int)sizeof(stack_buf); idx++)
				stack_buf[idx] = (char)(idx ^ (i & 0xFF));

			heap_page = sbrk(4096);
			if (heap_page != (void *)-1)
				memset(heap_page, 0xAB, 4096);

			_exit(0);
		}

		fork_ok++;
		if (wait4(-1, NULL, 0, NULL) < 0)
			wait_fail++;
	}

	drain_children();
	frames_after = read_used_kb();

	write_str("FASE45_FORK_MEM_TOUCH loops=");
	write_dec_u64((uint64_t)FASE45_FORK_TOUCH);
	write_str(" fork_ok=");
	write_dec_u64((uint64_t)fork_ok);
	write_str(" fork_fail=");
	write_dec_u64((uint64_t)fork_fail);
	write_str(" wait_fail=");
	write_dec_u64((uint64_t)wait_fail);
	write_str(" frames_before=");
	write_dec_u64((frames_before > 0) ? (uint64_t)(frames_before / 4) : 0);
	write_str(" frames_after=");
	write_dec_u64((frames_after > 0) ? (uint64_t)(frames_after / 4) : 0);
	write_str("\n");

	for (;;)
		(void)pause();
}
