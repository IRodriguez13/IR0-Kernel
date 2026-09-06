/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE41 reclaim smoke:
 * A) exit reclaim loop with mmap/touch/exit
 * B) exec reclaim loop (fork + mmap + exec /bin/f41true)
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FASE41_ITERS 64
#define FASE41_ALLOC (64 * 1024)

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

static int run_exit_reclaim(void)
{
	long used_before = read_used_kb();
	long used_peak = used_before;
	long used_after;
	uint64_t wait_fail = 0;

	for (int i = 0; i < FASE41_ITERS; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			volatile unsigned char *m;

			m = (volatile unsigned char *)mmap(NULL, FASE41_ALLOC,
						      PROT_READ | PROT_WRITE,
						      MAP_PRIVATE | MAP_ANONYMOUS,
						      -1, 0);
			if (m != MAP_FAILED)
			{
				for (size_t o = 0; o < FASE41_ALLOC; o += 4096)
					m[o] = (unsigned char)(o >> 12);
			}
			_exit(0);
		}
		if (pid < 0)
			return 2;

		{
			long used_now = read_used_kb();
			int st = 0;
			pid_t wr;

			if (used_now > used_peak)
				used_peak = used_now;
			wr = wait4(pid, &st, 0, 0);
			if (wr < 0)
				wait_fail++;
		}
	}

	used_after = read_used_kb();
	write_str("FASE41_A frames_before=");
	write_dec_u64((used_before > 0) ? (uint64_t)(used_before / 4) : 0);
	write_str(" frames_peak=");
	write_dec_u64((used_peak > 0) ? (uint64_t)(used_peak / 4) : 0);
	write_str(" frames_after=");
	write_dec_u64((used_after > 0) ? (uint64_t)(used_after / 4) : 0);
	write_str(" leak=");
	if (used_after > used_before)
		write_dec_u64((uint64_t)((used_after - used_before) / 4));
	else
		write_dec_u64(0);
	write_str(" wait_fail=");
	write_dec_u64(wait_fail);
	write_str("\n");

	return 0;
}

static int run_exec_reclaim(void)
{
	long used_before = read_used_kb();
	long used_after;
	uint64_t wait_fail = 0;
	char *argv[] = { "/bin/f41true", NULL };

	for (int i = 0; i < FASE41_ITERS; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			volatile unsigned char *m;

			m = (volatile unsigned char *)mmap(NULL, FASE41_ALLOC,
						      PROT_READ | PROT_WRITE,
						      MAP_PRIVATE | MAP_ANONYMOUS,
						      -1, 0);
			if (m != MAP_FAILED)
			{
				for (size_t o = 0; o < FASE41_ALLOC; o += 4096)
					m[o] = 0xA5;
			}
			execve("/bin/f41true", argv, NULL);
			_exit(127);
		}
		if (pid < 0)
			return 3;
		if (wait4(pid, NULL, 0, NULL) < 0)
			wait_fail++;
	}

	used_after = read_used_kb();
	write_str("FASE41_B vmas_before=-1 vmas_after=-1 pages_before=");
	write_dec_u64((used_before > 0) ? (uint64_t)(used_before / 4) : 0);
	write_str(" pages_after=");
	write_dec_u64((used_after > 0) ? (uint64_t)(used_after / 4) : 0);
	write_str(" wait_fail=");
	write_dec_u64(wait_fail);
	write_str(" kernel_exec_logs=1\n");

	return 0;
}

int main(void)
{
	int ra = run_exit_reclaim();
	int rb = run_exec_reclaim();

	write_str("FASE41_SUMMARY A=");
	write_dec_u64((uint64_t)ra);
	write_str(" B=");
	write_dec_u64((uint64_t)rb);
	write_str("\n");

	return (ra == 0 && rb == 0) ? 0 : 1;
}
