/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE50D — run real minimum programs and report observable contract.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void write_dec(unsigned int v)
{
	char buf[16];
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

static int read_once(int fd, char *buf, size_t size)
{
	ssize_t n;

	if (size == 0)
		return 0;
	n = read(fd, buf, size - 1);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return (int)n;
}

static int wait_with_timeout(pid_t pid, int *status, int *timed_out)
{
	int i;
	struct timespec ts;

	if (!status || !timed_out)
		return -1;

	*timed_out = 0;
	ts.tv_sec = 0;
	ts.tv_nsec = 100000000L;
	for (i = 0; i < 50; i++)
	{
		pid_t r = waitpid(pid, status, WNOHANG);

		if (r == pid)
			return 0;
		if (r < 0)
			return -1;
		(void)nanosleep(&ts, NULL);
	}

	*timed_out = 1;
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, status, 0);
	return 0;
}

static int run_capture(char *const argv[], char *out, size_t out_sz,
		       char *err, size_t err_sz, int *exit_code, int *timed_out)
{
	int outp[2];
	int errp[2];
	pid_t pid;
	int status;
	int out_n;
	int err_n;

	if (!argv || !argv[0] || !out || !err || !exit_code || !timed_out)
		return -1;
	if (pipe2(outp, 0) < 0 || pipe2(errp, 0) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
	{
		close(outp[0]);
		close(outp[1]);
		close(errp[0]);
		close(errp[1]);
		return -1;
	}
	if (pid == 0)
	{
		dup2(outp[1], 1);
		dup2(errp[1], 2);
		close(outp[0]);
		close(outp[1]);
		close(errp[0]);
		close(errp[1]);
		execv(argv[0], argv);
		_exit(127);
	}

	close(outp[1]);
	close(errp[1]);
	if (wait_with_timeout(pid, &status, timed_out) != 0)
	{
		close(outp[0]);
		close(errp[0]);
		return -1;
	}

	out_n = read_once(outp[0], out, out_sz);
	err_n = read_once(errp[0], err, err_sz);
	close(outp[0]);
	close(errp[0]);
	if (out_n < 0 || err_n < 0)
		return -1;

	if (*timed_out)
	{
		*exit_code = 124;
		return 0;
	}
	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		*exit_code = 128 + WTERMSIG(status);
	else
		*exit_code = 125;
	return 0;
}

static int report_program(const char *name, char *const argv[],
			  const char *expect_stdout_prefix, int require_nonempty)
{
	char out[256];
	char err[256];
	int ec = 0;
	int timeout = 0;
	const char *stdout_state = "EMPTY";
	const char *stderr_state = "EMPTY";
	const char *root = "none";
	int ok = 1;

	if (run_capture(argv, out, sizeof(out), err, sizeof(err), &ec, &timeout) != 0)
	{
		ec = 127;
		timeout = 0;
		root = "exec_fail";
		ok = 0;
	}
	else if (timeout)
	{
		root = "timeout";
		ok = 0;
	}
	else if (ec != 0)
	{
		root = "exit_nonzero";
		ok = 0;
	}

	if (out[0] != '\0')
		stdout_state = "NONEMPTY";
	if (err[0] != '\0')
		stderr_state = "NONEMPTY";

	if (ok && expect_stdout_prefix &&
	    strncmp(out, expect_stdout_prefix, strlen(expect_stdout_prefix)) != 0)
	{
		root = "stdout_mismatch";
		ok = 0;
	}
	if (ok && require_nonempty && out[0] == '\0')
	{
		root = "stdout_empty";
		ok = 0;
	}
	if (ok && err[0] != '\0')
	{
		root = "stderr_unexpected";
		ok = 0;
	}

	write_str("PROGRAM=");
	write_str(name);
	write_str(" EXIT=");
	write_dec((unsigned int)ec);
	write_str(" STDOUT=");
	write_str(stdout_state);
	write_str(" STDERR=");
	write_str(stderr_state);
	write_str(" TIMEOUT=");
	write_str(timeout ? "1" : "0");
	write_str(" ROOT_CAUSE=");
	write_str(root);
	write_str("\n");
	return ok;
}

int main(void)
{
	int ok = 1;
	int fd;
	char *argv_hello[] = { "/bin/hello-world", NULL };
	char *argv_echo[] = { "/bin/busybox", "echo", "hello", NULL };
	char *argv_cat[] = { "/bin/busybox", "cat", "/f50_prog_file.txt", NULL };
	char *argv_ls[] = { "/bin/busybox", "ls", "/", NULL };

	fd = open("/f50_prog_file.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd >= 0)
	{
		(void)write(fd, "program-file\n", 13);
		close(fd);
	}

	ok &= report_program("hello-world", argv_hello, "hello-world\n", 0);
	ok &= report_program("busybox echo", argv_echo, "hello\n", 0);
	ok &= report_program("busybox cat", argv_cat, "program-file\n", 0);
	ok &= report_program("busybox ls", argv_ls, NULL, 1);

	if (ok)
		write_str("USERSPACE_BOOTSTRAP_OK\n");
	else
		write_str("USERSPACE_CONTRACT_BROKEN\n");

	for (;;)
		(void)pause();

	return 0;
}
