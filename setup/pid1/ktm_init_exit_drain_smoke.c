/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE44 init-exit-drain
 * Drain all children, then exit PID1; kernel reports remaining processes.
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
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
	/* Spawn a few short-lived children to exercise drain path. */
	for (int i = 0; i < 8; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
			_exit(0);
		if (pid > 0)
			(void)wait4(pid, NULL, 0, NULL);
	}

	drain_children();

	write_str("FASE44_INIT_EXIT_DRAIN\n");
	_exit(0);
}
