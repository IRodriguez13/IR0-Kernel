/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE43 fork-wait-storm
 * 256 iterations: fork child that sleeps briefly, parent wait4.
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FASE43_FORK_WAIT 256

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

int main(void)
{
	int started = 0;
	int fork_fail = 0;
	int wait_fail = 0;

	for (int i = 0; i < FASE43_FORK_WAIT; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
			_exit(0);
		if (pid < 0)
		{
			fork_fail++;
			continue;
		}
		started++;
		if (wait4(-1, NULL, 0, NULL) < 0)
			wait_fail++;
	}

	write_str("FASE43_FORK_WAIT_STORM children=");
	write_dec_u64((uint64_t)started);
	write_str(" fork_fail=");
	write_dec_u64((uint64_t)fork_fail);
	write_str(" wait_fail=");
	write_dec_u64((uint64_t)wait_fail);
	write_str("\n");
	return 0;
}
