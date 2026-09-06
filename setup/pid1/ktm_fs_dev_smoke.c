/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * KTM fs/dev — base userspace layout + /dev smoke.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>

#define TCC_BIN "/bin/tcc"
#define TCC_LIB "/lib/tcc"
#define F53A_SRC "/tmp/f53a.c"
#define F53A_BIN "/tmp/f53abin"

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

static void fase53a_fail(const char *step, const char *reason)
{
	write_str("[KTM_FS_DEV][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("KTM_FS_DEV_FAIL_REASON=");
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

static int run_capture(const char *tag, const char *script,
		       char *out, size_t out_sz, int *exit_code, int *out_n)
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
		dup2(outp[1], 2);
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

	write_str("[KTM_FS_DEV][CAPTURE] tag=");
	write_str(tag);
	write_str(" ec=");
	write_dec_u64((unsigned long long)(unsigned int)*exit_code);
	write_str(" out_n=");
	write_dec_u64((unsigned long long)((*out_n < 0) ? 0 : *out_n));
	write_str("\n");
	return 0;
}

static int check_devfs_null_zero(void)
{
	int fd;
	char zeros[32];
	size_t i;

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0)
	{
		fase53a_fail("dev_null_open", "open");
		return -1;
	}
	if (write(fd, "discard", 7) != 7)
	{
		close(fd);
		fase53a_fail("dev_null_write", "write");
		return -1;
	}
	close(fd);
	write_str("[KTM_FS_DEV] CLASSIFY DEVFS_NULL_OK\n");

	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0)
	{
		fase53a_fail("dev_zero_open", "open");
		return -1;
	}
	if (read(fd, zeros, sizeof(zeros)) != (ssize_t)sizeof(zeros))
	{
		close(fd);
		fase53a_fail("dev_zero_read", "read");
		return -1;
	}
	close(fd);
	for (i = 0; i < sizeof(zeros); i++)
	{
		if (zeros[i] != 0)
		{
			fase53a_fail("dev_zero_content", "nonzero");
			return -1;
		}
	}
	write_str("[KTM_FS_DEV] CLASSIFY DEVFS_ZERO_OK\n");
	return 0;
}

static int check_tmpdir_rw(void)
{
	int fd;
	char buf[8];

	(void)unlink("/tmp/x");
	fd = open("/tmp/x", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
	{
		fase53a_fail("tmp_write_open", "open");
		return -1;
	}
	if (write(fd, "hi\n", 3) != 3)
	{
		close(fd);
		fase53a_fail("tmp_write", "write");
		return -1;
	}
	close(fd);

	fd = open("/tmp/x", O_RDONLY);
	if (fd < 0)
	{
		fase53a_fail("tmp_read_open", "open");
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	if (read(fd, buf, sizeof(buf) - 1) < 0)
	{
		close(fd);
		fase53a_fail("tmp_read", "read");
		return -1;
	}
	close(fd);
	if (strncmp(buf, "hi\n", 3) != 0)
	{
		fase53a_fail("tmp_content", "mismatch");
		return -1;
	}

	write_str("[KTM_FS_DEV] CLASSIFY TMPDIR_OK\n");
	return 0;
}

static int check_chdir_pwd(void)
{
	char cwd[256];

	if (chdir("/tmp") != 0)
	{
		fase53a_fail("chdir_tmp", "chdir");
		return -1;
	}
	if (!getcwd(cwd, sizeof(cwd)))
	{
		fase53a_fail("getcwd_tmp", "getcwd");
		return -1;
	}
	if (strcmp(cwd, "/tmp") != 0)
	{
		fase53a_fail("pwd_tmp", "mismatch");
		return -1;
	}

	write_str("[KTM_FS_DEV] CLASSIFY CWD_CHDIR_OK\n");
	return 0;
}

static int check_ls_dev(void)
{
	char out[1024];
	int ec;
	int out_n;

	if (run_capture("ls_dev", "/bin/busybox ls /dev", out, sizeof(out), &ec, &out_n) != 0)
	{
		fase53a_fail("ls_dev", "capture");
		return -1;
	}
	if (ec != 0)
	{
		fase53a_fail("ls_dev", "exit");
		return -1;
	}
	if (out_n <= 0)
	{
		fase53a_fail("ls_dev", "empty");
		return -1;
	}
	return 0;
}

static int check_tcc_layout(void)
{
	static const char src[] =
		"#include <stdio.h>\n"
		"int main(void){puts(\"f53a\");return 0;}\n";
	char cmd[256];
	int fd;
	int ec;
	int out_n;
	char out[512];
	pid_t pid;
	int status;
	char *argv_exec[] = { (char *)F53A_BIN, NULL };

	fd = open(F53A_SRC, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
	{
		fase53a_fail("tcc_src_open", "open");
		return -1;
	}
	if (write(fd, src, sizeof(src) - 1) != (ssize_t)(sizeof(src) - 1))
	{
		close(fd);
		fase53a_fail("tcc_src_write", "write");
		return -1;
	}
	close(fd);

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd),
		 "%s -B %s -static -o %s %s",
		 TCC_BIN, TCC_LIB, F53A_BIN, F53A_SRC);
	if (run_capture("tcc_compile", cmd, out, sizeof(out), &ec, &out_n) != 0)
	{
		fase53a_fail("tcc_compile", "capture");
		return -1;
	}
	if (ec != 0)
	{
		fase53a_fail("tcc_compile", "exit");
		return -1;
	}

	pid = fork();
	if (pid < 0)
	{
		fase53a_fail("tcc_exec", "fork");
		return -1;
	}
	if (pid == 0)
	{
		execv(F53A_BIN, argv_exec);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
	{
		fase53a_fail("tcc_exec", "waitpid");
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		fase53a_fail("tcc_exec", "status");
		return -1;
	}

	write_str("[KTM_FS_DEV] CLASSIFY TCC_LAYOUT_NO_REGRESSION\n");
	return 0;
}

int main(void)
{
	write_str("KTM_FS_DEV_START\n");
	write_str("KTM_FS_DEV_HARNESS_ID=ktm_fs_dev_smoke.c\n");

	if (check_devfs_null_zero() != 0)
		goto halt;
	if (check_tmpdir_rw() != 0)
		goto halt;
	if (check_chdir_pwd() != 0)
		goto halt;
	if (check_ls_dev() != 0)
		goto halt;
	if (check_tcc_layout() != 0)
		goto halt;

	write_str("[KTM_FS_DEV] CLASSIFY KTM_LEGACY_STACK_NO_REGRESSION\n");
	write_str("KTM_FS_DEV_OK\n");

halt:
	for (;;)
		(void)pause();

	return 0;
}
