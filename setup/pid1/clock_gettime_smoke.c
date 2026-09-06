/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * musl/BusyBox-style clock_gettime gate: syscalls 228 and 403 + libc path.
 */

#include <time.h>
#include <unistd.h>

#define SYS_clock_gettime   228
#define SYS_clock_gettime64 403
#define CLOCK_MONOTONIC     1

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

	if (ir0_syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)(void *)&kts) != 0)
	{
		emit("CLOCK228_FAIL\n");
		return 1;
	}
	emit("CLOCK228_OK\n");

	if (ir0_syscall2(SYS_clock_gettime64, CLOCK_MONOTONIC, (long)(void *)&kts) != 0)
	{
		emit("CLOCK403_FAIL\n");
		return 2;
	}
	emit("CLOCK403_OK\n");

	if (clock_gettime(CLOCK_MONOTONIC, &uts) != 0)
	{
		emit("CLOCK_LIBC_FAIL\n");
		return 3;
	}
	emit("CLOCK_LIBC_OK\n");
	return 0;
}
