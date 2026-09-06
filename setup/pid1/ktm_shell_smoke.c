/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE51 — BusyBox ash shell scripts smoke (pipes, redirect, loops, &&/||).
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void write_dec_u64(unsigned long long v)
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
		buf[n++] = (char)('0' + (v % 10ULL));
		v /= 10ULL;
	}
	while (n-- > 0)
		(void)write(1, &buf[n], 1);
}

static void fase51_fail(const char *step, const char *reason)
{
	write_str("[KTM_SHELL][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("KTM_SHELL_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static int read_all(int fd, char *buf, size_t size)
{
	size_t used = 0;

	if (!buf || size == 0)
		return -1;

	for (;;)
	{
		ssize_t n;
		size_t avail;

		if (used >= size - 1)
		{
			char sink[64];

			n = read(fd, sink, sizeof(sink));
			if (n <= 0)
				break;
			continue;
		}

		avail = (size - 1) - used;
		n = read(fd, buf + used, avail);
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		used += (size_t)n;
	}

	buf[used] = '\0';
	return (int)used;
}

static int sh_capture(const char *tag, const char *script, char *out, size_t out_sz,
		      int *exit_code, int *out_n)
{
	char *argv[] = { "sh", "-c", (char *)script, NULL };
	int outp[2];
	pid_t pid;
	int status;

	if (!tag || !script || !out || !exit_code || !out_n)
		return -1;
	if (pipe2(outp, 0) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
	{
		close(outp[0]);
		close(outp[1]);
		return -1;
	}
	if (pid == 0)
	{
		dup2(outp[1], 1);
		close(outp[0]);
		close(outp[1]);
		execv("/bin/busybox", argv);
		_exit(127);
	}

	close(outp[1]);
	*out_n = read_all(outp[0], out, out_sz);
	close(outp[0]);
	if (*out_n < 0)
		return -1;

	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else
		*exit_code = 128;

	write_str("[KTM_SHELL][CAPTURE] tag=");
	write_str(tag);
	write_str(" ec=");
	write_dec_u64((unsigned long long)(unsigned int)*exit_code);
	write_str(" out_n=");
	write_dec_u64((unsigned long long)((*out_n < 0) ? 0 : *out_n));
	write_str(" script=");
	write_str(script);
	write_str("\n");
	return 0;
}

static int sh_expect_stdout(const char *step, const char *script,
			    const char *want_prefix)
{
	char out[512];
	int ec;
	int out_n;

	if (sh_capture(step, script, out, sizeof(out), &ec, &out_n) != 0)
	{
		fase51_fail(step, "capture");
		return -1;
	}
	if (ec != 0)
	{
		fase51_fail(step, "exit");
		return -1;
	}
	if (!want_prefix || strncmp(out, want_prefix, strlen(want_prefix)) != 0)
	{
		write_str("[KTM_SHELL] CLASSIFY ");
		write_str(step);
		write_str("_STDOUT_MISMATCH\n");
		fase51_fail(step, "stdout");
		return -1;
	}
	return 0;
}

static int output_has_lines_abc(const char *out)
{
	return out && strstr(out, "a\n") && strstr(out, "b\n") && strstr(out, "c");
}

int main(void)
{
	char out[512];
	int ec;
	int out_n;

	write_str("KTM_SHELL_START\n");

	(void)unlink("/f51_a.txt");

	write_str("KTM_SHELL_PIPE_START\n");
	if (sh_expect_stdout("pipe", "echo hi | cat", "hi") != 0)
		goto halt;
	write_str("[KTM_SHELL] CLASSIFY KTM_SHELL_PIPE_OK\n");

	write_str("KTM_SHELL_REDIRECT_START\n");
	if (sh_expect_stdout("redirect", "echo hello > /f51_a.txt; cat /f51_a.txt",
			     "hello") != 0)
		goto halt;
	write_str("[KTM_SHELL] CLASSIFY KTM_SHELL_REDIRECT_OK\n");

	write_str("KTM_SHELL_FOR_START\n");
	if (sh_capture("for_loop", "for x in a b c; do echo $x; done",
		       out, sizeof(out), &ec, &out_n) != 0)
	{
		fase51_fail("for_loop", "capture");
		goto halt;
	}
	if (ec != 0)
	{
		write_str("[KTM_SHELL] CLASSIFY for_loop_EXIT_STATUS\n");
		fase51_fail("for_loop", "exit");
		goto halt;
	}
	if (!output_has_lines_abc(out))
	{
		fase51_fail("for_loop", "stdout");
		goto halt;
	}
	write_str("[KTM_SHELL] CLASSIFY KTM_SHELL_FOR_LOOP_OK\n");

	write_str("KTM_SHELL_CONTROL_START\n");
	if (sh_expect_stdout("true_and", "true && echo ok", "ok") != 0)
		goto halt;
	if (sh_expect_stdout("false_or", "false || echo ok", "ok") != 0)
		goto halt;
	write_str("[KTM_SHELL] CLASSIFY KTM_SHELL_CONTROL_FLOW_OK\n");

	write_str("[KTM_SHELL] CLASSIFY KTM_SHELL_BASELINE_STABLE\n");
	write_str("[KTM_SHELL] CLASSIFY KTM_SHELL_DEBUG_GATED\n");
	write_str("KTM_SHELL_OK\n");
	goto done;

halt:
	for (;;)
		(void)pause();

done:
	for (;;)
		(void)pause();

	return 0;
}
