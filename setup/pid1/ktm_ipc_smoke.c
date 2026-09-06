/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE48 IPC + shell readiness smokes:
 *   A) pipe_pingpong
 *   B) pipe_exec (fork + dup2 + exec cat)
 *   C) pipeline (echo | cat | cat)
 *   D) busybox_probe
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
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

static int count_open_fds(void)
{
	int n = 0;

	for (int fd = 0; fd < 64; fd++)
	{
		if (fcntl(fd, F_GETFD) != -1)
			n++;
	}
	return n;
}

static int test_pipe_pingpong(void)
{
	int pc[2];
	int cp[2];
	char c;
	pid_t pid;

	if (pipe2(pc, 0) < 0 || pipe2(cp, 0) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		close(pc[1]);
		close(cp[0]);
		if (read(pc[0], &c, 1) != 1)
			_exit(1);
		(void)write(cp[1], "C", 1);
		close(pc[0]);
		close(cp[1]);
		_exit(0);
	}

	close(pc[0]);
	(void)write(pc[1], "P", 1);
	close(pc[1]);
	(void)waitpid(pid, NULL, 0);
	if (read(cp[0], &c, 1) != 1 || c != 'C')
		return -1;
	close(cp[0]);
	close(cp[1]);
	return 0;
}

static int test_pipe_exec(void)
{
	int inpipe[2];
	int outpipe[2];
	char buf[8];
	pid_t pid;

	if (pipe2(inpipe, 0) < 0 || pipe2(outpipe, 0) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		dup2(inpipe[0], 0);
		dup2(outpipe[1], 1);
		close(inpipe[0]);
		close(inpipe[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		execl("/bin/cat", "cat", (char *)NULL);
		_exit(127);
	}

	close(inpipe[0]);
	close(outpipe[1]);
	(void)write(inpipe[1], "hi\n", 3);
	close(inpipe[1]);
	if (waitpid(pid, NULL, 0) < 0)
	{
		close(outpipe[0]);
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	if (read(outpipe[0], buf, sizeof(buf) - 1) < 2)
	{
		close(outpipe[0]);
		return -1;
	}
	close(outpipe[0]);
	return (buf[0] == 'h' && buf[1] == 'i') ? 0 : -1;
}

static int run_cat_stage(int in_fd, int out_fd)
{
	pid_t pid = fork();

	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		if (in_fd >= 0)
			dup2(in_fd, 0);
		if (out_fd >= 0)
			dup2(out_fd, 1);
		if (in_fd >= 0)
			close(in_fd);
		if (out_fd >= 0)
			close(out_fd);
		execl("/bin/cat", "cat", (char *)NULL);
		_exit(127);
	}
	return pid;
}

static int test_pipeline(void)
{
	int p1[2];
	int outpipe[2];
	char buf[32];
	ssize_t n;
	pid_t echo_pid;
	pid_t cat_pid;

	if (pipe2(p1, 0) < 0 || pipe2(outpipe, 0) < 0)
		return -1;

	echo_pid = fork();
	if (echo_pid < 0)
		return -1;
	if (echo_pid == 0)
	{
		dup2(p1[1], 1);
		close(p1[0]);
		close(p1[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		execl("/bin/echo", "echo", "hello", (char *)NULL);
		_exit(127);
	}

	cat_pid = run_cat_stage(p1[0], outpipe[1]);
	close(p1[0]);
	close(p1[1]);
	close(outpipe[1]);

	if (waitpid(echo_pid, NULL, 0) < 0 ||
	    waitpid(cat_pid, NULL, 0) < 0)
	{
		close(outpipe[0]);
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	n = read(outpipe[0], buf, sizeof(buf) - 1);
	close(outpipe[0]);

	if (n < 5)
		return -1;
	return (buf[0] == 'h' && buf[1] == 'e' && buf[2] == 'l') ? 0 : -1;
}

static int test_busybox_probe(void)
{
	char buf[8];
	int p[2];
	pid_t pid;
	int status = 1;

	if (pipe2(p, 0) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		dup2(p[1], 1);
		close(p[0]);
		close(p[1]);
		execl("/bin/busybox", "busybox", "sh", "-c", "echo ok", (char *)NULL);
		_exit(127);
	}

	close(p[1]);
	if (waitpid(pid, &status, 0) < 0)
	{
		close(p[0]);
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		close(p[0]);
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	if (read(p[0], buf, 2) != 2)
	{
		close(p[0]);
		return -1;
	}
	close(p[0]);
	return (buf[0] == 'o' && buf[1] == 'k') ? 0 : -1;
}

static void reap_all_children(void)
{
	int status;

	while (waitpid(-1, &status, WNOHANG) > 0)
		(void)status;
}

static void drain_sentinel(void)
{
	pid_t pid = fork();
	pid_t r;

	if (pid == 0)
		_exit(0);
	if (pid > 0)
		(void)waitpid(pid, NULL, 0);
	while ((r = wait4(-1, NULL, WNOHANG, NULL)) > 0)
		(void)r;
}

int main(void)
{
	int fd_after;
	int ok_a = 0;
	int ok_b = 0;
	int ok_c = 0;
	int ok_d = 0;

	write_str("FASE48_START\n");
	reap_all_children();
	ok_a = (test_pipe_pingpong() == 0);
	reap_all_children();
	ok_b = (test_pipe_exec() == 0);
	reap_all_children();
	ok_c = (test_pipeline() == 0);
	write_str("FASE48_PIPELINE_DONE\n");
	reap_all_children();
	ok_d = (test_busybox_probe() == 0);

	drain_sentinel();
	fd_after = count_open_fds();

	write_str("FASE48_IPC fd_before=3 fd_after=");
	write_dec_u64((uint64_t)fd_after);
	write_str(" pingpong=");
	write_str(ok_a ? "OK" : "FAIL");
	write_str(" pipe_exec=");
	write_str(ok_b ? "OK" : "FAIL");
	write_str(" pipeline=");
	write_str(ok_c ? "OK" : "FAIL");
	write_str(" busybox=");
	write_str(ok_d ? "OK" : "FAIL");
	write_str("\n");

	if (ok_d)
		write_str("FASE48_BUSYBOX_PROBE_OK\n");

	for (;;)
		(void)pause();

	return 0;
}
