/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Mimic BusyBox ping: SIGALRM handler calls clock_gettime (monotonic_us path).
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct ts228
{
	long sec;
	long nsec;
};

static volatile int alarm_count;
static struct ts228 alrm_ts;

static void on_alrm(int sig)
{
	struct ts228 *ts = &alrm_ts;

	(void)sig;
	alarm_count++;
	if (clock_gettime(CLOCK_MONOTONIC, (struct timespec *)ts) != 0)
	{
		write(1, "ALRM_CLOCK_FAIL\n", 16);
		return;
	}
	write(1, "ALRM_CLOCK_OK\n", 14);
}

int main(void)
{
	struct itimerval it;
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
	{
		write(1, "MAIN_CLOCK_FAIL\n", 16);
		return 1;
	}
	write(1, "MAIN_CLOCK_OK\n", 14);

	signal(SIGALRM, on_alrm);
	memset(&it, 0, sizeof(it));
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = 100000; /* 100ms like quick ping interval test */
	setitimer(ITIMER_REAL, &it, NULL);

	/* Block like ping recvfrom */
	for (int i = 0; i < 30 && alarm_count < 2; i++)
		pause();

	if (alarm_count == 0)
	{
		write(1, "NO_ALRM\n", 8);
		return 2;
	}
	if (alarm_count >= 1)
	{
		/* check post-handler main thread clock */
		if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		{
			write(1, "POST_CLOCK_FAIL\n", 16);
			return 3;
		}
		write(1, "POST_CLOCK_OK\n", 14);
	}
	write(1, "ALRM_SMOKE_OK\n", 14);
	return 0;
}
