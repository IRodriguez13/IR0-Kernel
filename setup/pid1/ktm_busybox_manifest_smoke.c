/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * KTM busybox manifest — headless BusyBox coreutils smoke harness (PID 1, static musl).
 *
 * Runs applets from fase58_full.config and emits serial tags for make grep.
 */

#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

static void write_str(const char *s)
{
	size_t n = 0;

	if (!s)
		return;
	while (s[n])
		n++;
	(void)write(1, s, n);
}

static int read_all(int fd, char *buf, size_t sz)
{
	size_t total = 0;
	ssize_t n;

	if (!buf || sz == 0)
		return 0;

	while (total < sz - 1)
	{
		n = read(fd, buf + total, sz - 1 - total);
		if (n <= 0)
			break;
		total += (size_t)n;
	}
	buf[total] = '\0';
	return (int)total;
}

static int run_capture(const char *tag, char *const argv[],
		       char *out, size_t out_sz, int *exit_code)
{
	int outp[2];
	pid_t pid;
	int status;
	int out_n;

	if (pipe(outp) != 0)
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
		close(outp[0]);
		dup2(outp[1], 1);
		dup2(outp[1], 2);
		close(outp[1]);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(outp[1]);
	out_n = read_all(outp[0], out, out_sz);
	close(outp[0]);

	if (waitpid(pid, &status, 0) < 0)
		return -1;

	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else
		*exit_code = 128;

	write_str("[KTM_BB_MANIFEST][");
	write_str(tag);
	write_str("] ec=");
	{
		char d[8];
		int v = *exit_code;
		int pos = 0;

		if (v < 0)
			v = 0;
		if (v == 0)
		{
			d[pos++] = '0';
		}
		else
		{
			char tmp[8];
			int t = 0;

			while (v > 0 && t < 7)
			{
				tmp[t++] = (char)('0' + (v % 10));
				v /= 10;
			}
			while (t > 0)
				d[pos++] = tmp[--t];
		}
		d[pos] = '\0';
		(void)write(1, d, (size_t)pos);
	}
	write_str(" out_len=");
	{
		char nbuf[12];
		int v = out_n;
		int i = 0;

		if (v < 0)
			v = 0;
		do
		{
			nbuf[i++] = (char)('0' + (v % 10));
			v /= 10;
		} while (v > 0 && i < (int)sizeof(nbuf) - 1);
		while (i > 0)
		{
			char c = nbuf[--i];

			(void)write(1, &c, 1);
		}
	}
	write_str("\n");

	return 0;
}

static int expect_ok_tag(const char *step, char *const argv[], const char *ok_tag)
{
	char out[512];
	int ec = -1;

	if (run_capture(step, argv, out, sizeof(out), &ec) != 0)
		return -1;
	if (ec != 0)
		return -1;
	write_str(ok_tag);
	write_str("\n");
	return 0;
}

static int expect_stdout_has(const char *step, char *const argv[],
			     const char *needle, const char *ok_tag)
{
	char out[512];
	int ec = -1;
	size_t i;
	size_t out_len;

	if (run_capture(step, argv, out, sizeof(out), &ec) != 0)
		return -1;
	if (ec != 0)
		return -1;
	if (!needle)
		return -1;

	out_len = strlen(out);
	for (i = 0; i + strlen(needle) <= out_len; i++)
	{
		if (memcmp(out + i, needle, strlen(needle)) == 0)
		{
			write_str(ok_tag);
			write_str("\n");
			return 0;
		}
	}
	return -1;
}

int main(void)
{
	int kfd = -1;
	char *argv_echo[] = { "/bin/busybox", "echo", "hi", NULL };
	char *argv_echo_path[] = { "/bin/echo", "hi", NULL };
	char *argv_pwd[] = { "/bin/busybox", "pwd", NULL };
	char *argv_ls[] = { "/bin/busybox", "ls", "/", NULL };
	char *argv_ls_path[] = { "/bin/ls", "/", NULL };
	char *argv_touch[] = { "/bin/busybox", "touch", "/tmp/a", NULL };
	char *argv_write[] = { "/bin/sh", "-c", "echo hi > /tmp/a", NULL };
	char *argv_cat[] = { "/bin/busybox", "cat", "/tmp/a", NULL };
	char *argv_cat_path[] = { "/bin/cat", "/tmp/a", NULL };
	char *argv_uname[] = { "/bin/busybox", "uname", NULL };
	char *argv_ps[] = { "/bin/busybox", "ps", NULL };

	write_str("KTM_BB_MANIFEST_HARNESS_ID=ktm_busybox_manifest_smoke.c\n");
	write_str("KTM_BB_MANIFEST_START\n");

	kfd = ktm_open();
	if (kfd >= 0)
		(void)ktm_case_begin(kfd, "busybox_coreutils");

	if (expect_stdout_has("echo", argv_echo, "hi", "KTM_BB_MANIFEST_ECHO_OK") != 0)
		goto fail;

	if (expect_stdout_has("echo_path", argv_echo_path, "hi", "KTM_BB_MANIFEST_ECHO_PATH_OK") != 0)
		goto fail;

	if (expect_ok_tag("pwd", argv_pwd, "KTM_BB_MANIFEST_PWD_OK") != 0)
		goto fail;

	if (expect_ok_tag("ls", argv_ls, "KTM_BB_MANIFEST_LS_ROOT_OK") != 0)
		goto fail;

	if (expect_ok_tag("ls_path", argv_ls_path, "KTM_BB_MANIFEST_LS_PATH_OK") != 0)
		goto fail;

	if (expect_ok_tag("touch", argv_touch, "KTM_BB_MANIFEST_TOUCH_OK") != 0)
		goto fail;

	if (expect_ok_tag("write", argv_write, "KTM_BB_MANIFEST_WRITE_OK") != 0)
		goto fail;

	if (expect_stdout_has("cat", argv_cat, "hi", "KTM_BB_MANIFEST_CAT_OK") != 0)
		goto fail;

	if (expect_stdout_has("cat_path", argv_cat_path, "hi", "KTM_BB_MANIFEST_CAT_PATH_OK") != 0)
		goto fail;

	if (expect_ok_tag("uname", argv_uname, "KTM_BB_MANIFEST_UNAME_OK") != 0)
		goto fail;

	if (expect_ok_tag("ps", argv_ps, "KTM_BB_MANIFEST_PS_OK") != 0)
		write_str("KTM_BB_MANIFEST_PS_SKIP\n");

	if (kfd >= 0)
	{
		(void)ktm_checkpoint(kfd, "applets_ok");
		(void)ktm_assert_true(kfd, "coreutils_pass", 1);
		(void)ktm_case_end(kfd, "busybox_coreutils", 0);
		ktm_close(kfd);
		write_str("KTM_BUSYBOX_COREUTILS_OK\n");
	}
	else
		write_str("KTM_BUSYBOX_COREUTILS_SKIP\n");

	write_str("BUSYBOX_MANIFEST_OK\n");
	write_str("KTM_USERDEV_OK\n");
	write_str("KTM_BB_MANIFEST_OK\n");
	for (;;)
		pause();
	return 0;

fail:
	if (kfd >= 0)
	{
		(void)ktm_case_end(kfd, "busybox_coreutils", 1);
		ktm_close(kfd);
	}
	write_str("KTM_BB_MANIFEST_FAIL\n");
	for (;;)
		pause();
	return 1;
}
