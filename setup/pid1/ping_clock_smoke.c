/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Exec BusyBox ping after clock_gettime gate — network smoke for MONOTONIC in ping.
 */

#include <time.h>
#include <unistd.h>

#define SYS_clock_gettime 228
#define CLOCK_MONOTONIC   1

struct ts228
{
	long sec;
	long nsec;
};

static long ir0_syscall2(long nr, long a, long b)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a), "S"(b)
		: "rcx", "r11", "memory");

	return ret;
}

static int emit(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return write(1, s, n) == (ssize_t)n ? 0 : 1;
}

int main(void)
{
	struct ts228 kts;
	struct timespec uts;
	char *const argv[] = { (char *)"busybox", (char *)"ping", (char *)"-c",
		(char *)"4", (char *)"10.0.2.2", NULL };
	char *const envp[] = { (char *)"PATH=/bin:/usr/bin", NULL };

	if (ir0_syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)(void *)&kts) != 0)
	{
		emit("PING_CLOCK_PRE_FAIL\n");
		return 1;
	}
	emit("PING_CLOCK_PRE_OK\n");

	if (clock_gettime(CLOCK_MONOTONIC, &uts) != 0)
	{
		emit("PING_CLOCK_LIBC_PRE_FAIL\n");
		return 2;
	}
	emit("PING_CLOCK_LIBC_PRE_OK\n");

	execve("/bin/busybox", argv, envp);
	emit("PING_EXEC_FAIL\n");
	return 3;
}
