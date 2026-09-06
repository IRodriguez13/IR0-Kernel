/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int child_loop(void)
{
	struct timespec ts;
	int i;

	for (i = 0; i < 6; i++)
	{
		if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		{
			printf("CLOCK_LOOP_FAIL iter=%d\n", i);
			return 1;
		}
		printf("CLOCK_LOOP_OK iter=%d\n", i);
		usleep(100000);
	}
	printf("CLOCK_LOOP_ALL_OK\n");
	return 0;
}

int main(void)
{
	pid_t pid;
	int st;

	write(1, "EXEC_LOOP_START\n", 16);
	pid = fork();
	if (pid < 0)
	{
		write(1, "FORK_FAIL\n", 10);
		return 1;
	}
	if (pid == 0)
		return child_loop();

	if (waitpid(pid, &st, 0) < 0)
	{
		write(1, "WAIT_FAIL\n", 10);
		return 2;
	}
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
	{
		write(1, "CHILD_BAD\n", 10);
		return 3;
	}
	write(1, "EXEC_LOOP_DONE\n", 15);
	return 0;
}
