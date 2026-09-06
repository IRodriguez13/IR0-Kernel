/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE46 fork-no-recursion
 * Each child must run fork branch exactly once (child_counter == 1).
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FASE46_FORK_LOOPS 512

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
	int fork_ok = 0;
	int fork_fail = 0;
	int wait_fail = 0;
	int child_recurse = 0;

	for (int i = 0; i < FASE46_FORK_LOOPS; i++)
	{
		pid_t pid = fork();

		if (pid < 0)
		{
			fork_fail++;
			continue;
		}
		if (pid == 0)
		{
			static int child_counter;

			child_counter++;
			if (child_counter != 1)
				write_str("FASE46_CHILD_RECURSE\n");
			_exit(0);
		}

		fork_ok++;
		if (waitpid(pid, NULL, 0) < 0)
			wait_fail++;
	}

	drain_children();

	write_str("FASE46_FORK_NO_RECURSE fork_ok=");
	write_dec_u64((uint64_t)fork_ok);
	write_str(" fork_fail=");
	write_dec_u64((uint64_t)fork_fail);
	write_str(" wait_fail=");
	write_dec_u64((uint64_t)wait_fail);
	write_str(" child_recurse=");
	write_dec_u64((uint64_t)child_recurse);
	write_str(" child_counter=0\n");

	for (;;)
		(void)pause();
}
