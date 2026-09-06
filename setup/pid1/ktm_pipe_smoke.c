/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE49 — Pipeline closure (EOF + FD lifetime) smokes:
 *   1) echo hello | cat
 *   2) echo hello | cat | cat
 *   3) cat < /dev/null  (immediate EOF)
 *   4) writer close -> reader EOF
 *   5) reader close -> writer EPIPE
 *   6) exec CLOEXEC fd_before == fd_after
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

static int run_cat_stage(int in_fd, int out_fd, const int *extras, int nextras)
{
	pid_t pid = fork();

	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		int i;

		if (in_fd >= 0)
			dup2(in_fd, 0);
		if (out_fd >= 0)
			dup2(out_fd, 1);
		if (in_fd >= 0)
			close(in_fd);
		if (out_fd >= 0)
			close(out_fd);
		for (i = 0; i < nextras; i++)
		{
			if (extras[i] >= 0)
				close(extras[i]);
		}
		execl("/bin/cat", "cat", (char *)NULL);
		_exit(127);
	}
	return pid;
}

static int test_pipeline_one(void)
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

	int cat_extra[4];

	cat_extra[0] = p1[1];
	cat_extra[1] = outpipe[0];
	cat_pid = run_cat_stage(p1[0], outpipe[1], cat_extra, 2);
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

static int test_pipeline_two(void)
{
	int p1[2];
	int p2[2];
	int outpipe[2];
	char buf[32];
	ssize_t n;
	pid_t echo_pid;
	pid_t cat1_pid;
	pid_t cat2_pid;

	if (pipe2(p1, 0) < 0 || pipe2(p2, 0) < 0 || pipe2(outpipe, 0) < 0)
		return -1;

	echo_pid = fork();
	if (echo_pid < 0)
		return -1;
	if (echo_pid == 0)
	{
		dup2(p1[1], 1);
		close(p1[0]);
		close(p1[1]);
		close(p2[0]);
		close(p2[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		execl("/bin/echo", "echo", "hello", (char *)NULL);
		_exit(127);
	}

	int cat1_extra[4];
	int cat2_extra[4];

	cat1_extra[0] = p1[1];
	cat1_extra[1] = p2[0];
	cat1_extra[2] = outpipe[0];
	cat1_extra[3] = outpipe[1];
	cat2_extra[0] = p1[0];
	cat2_extra[1] = p1[1];
	cat2_extra[2] = p2[1];
	cat2_extra[3] = outpipe[0];
	cat1_pid = run_cat_stage(p1[0], p2[1], cat1_extra, 4);
	cat2_pid = run_cat_stage(p2[0], outpipe[1], cat2_extra, 4);
	close(p1[0]);
	close(p1[1]);
	close(p2[0]);
	close(p2[1]);
	close(outpipe[1]);

	if (waitpid(echo_pid, NULL, 0) < 0 ||
	    waitpid(cat1_pid, NULL, 0) < 0 ||
	    waitpid(cat2_pid, NULL, 0) < 0)
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

static int test_devnull_eof(void)
{
	int fd;
	char c;

	fd = open("/dev/null", O_RDONLY);
	if (fd < 0)
		return -1;
	if (read(fd, &c, 1) != 0)
	{
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int test_writer_close_eof(void)
{
	int p[2];
	pid_t pid;
	int status;

	if (pipe2(p, 0) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		char c;

		close(p[1]);
		if (read(p[0], &c, 1) != 0)
			_exit(1);
		close(p[0]);
		_exit(0);
	}

	close(p[0]);
	close(p[1]);
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static int test_reader_close_epipe(void)
{
	int p[2];

	if (pipe2(p, 0) < 0)
		return -1;

	close(p[0]);
	if (write(p[1], "z", 1) >= 0)
	{
		close(p[1]);
		return -1;
	}
	if (errno != EPIPE)
	{
		close(p[1]);
		return -1;
	}
	close(p[1]);
	return 0;
}

static int test_exec_cloexec(void)
{
	int p[2];
	int fd_before;
	int fd_after;
	pid_t pid;
	int status;

	fd_before = count_open_fds();
	if (pipe2(p, O_CLOEXEC) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
	{
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0)
	{
		/* CLOEXEC pipe fds must be dropped by exec; do not dup2 them to stdio */
		execl("/bin/echo", "echo", "ok", (char *)NULL);
		_exit(127);
	}

	close(p[0]);
	close(p[1]);
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	fd_after = count_open_fds();
	return (fd_before == fd_after) ? 0 : -1;
}

static void reap_all_children(void)
{
	int status;

	while (waitpid(-1, &status, WNOHANG) > 0)
		(void)status;
}

int main(void)
{
	int fd_before;
	int fd_after;
	int ok1 = 0;
	int ok2 = 0;
	int ok3 = 0;
	int ok4 = 0;
	int ok5 = 0;
	int ok6 = 0;

	write_str("FASE49_START\n");
	reap_all_children();
	fd_before = count_open_fds();

	ok1 = (test_pipeline_one() == 0);
	reap_all_children();
	ok2 = (test_pipeline_two() == 0);
	reap_all_children();
	ok3 = (test_devnull_eof() == 0);
	reap_all_children();
	ok4 = (test_writer_close_eof() == 0);
	reap_all_children();
	ok5 = (test_reader_close_epipe() == 0);
	reap_all_children();
	ok6 = (test_exec_cloexec() == 0);
	reap_all_children();

	fd_after = count_open_fds();

	write_str("FASE49_PIPE fd_before=");
	write_dec_u64((uint64_t)fd_before);
	write_str(" fd_after=");
	write_dec_u64((uint64_t)fd_after);
	write_str(" check1=");
	write_str(ok1 ? "OK" : "FAIL");
	write_str(" check2=");
	write_str(ok2 ? "OK" : "FAIL");
	write_str(" check3=");
	write_str(ok3 ? "OK" : "FAIL");
	write_str(" check4=");
	write_str(ok4 ? "OK" : "FAIL");
	write_str(" check5=");
	write_str(ok5 ? "OK" : "FAIL");
	write_str(" check6=");
	write_str(ok6 ? "OK" : "FAIL");
	write_str("\n");

	for (;;)
		(void)pause();

	return 0;
}
