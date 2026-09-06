/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE53B — POSIX/pseudo-fs routed path robustness harness.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <stdint.h>

struct linux_dirent64
{
	uint64_t d_ino;
	int64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

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

static void fase53b_fail(const char *step, const char *reason)
{
	write_str("[KTM_POSIX_PSEUDOFS][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("KTM_POSIX_PSEUDOFS_FAIL_REASON=");
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

	write_str("[KTM_POSIX_PSEUDOFS][CAPTURE] tag=");
	write_str(tag);
	write_str(" ec=");
	write_dec_u64((unsigned long long)(unsigned int)*exit_code);
	write_str(" out_n=");
	write_dec_u64((unsigned long long)((*out_n < 0) ? 0 : *out_n));
	write_str("\n");

	return 0;
}

static int check_ls_dev_twice(void)
{
	char out[1024];
	char first[1024];
	int ec;
	int out_n;
	int i;

	for (i = 0; i < 2; i++)
	{
		if (run_capture("ls_dev", "/bin/busybox ls /dev",
				out, sizeof(out), &ec, &out_n) != 0)
		{
			fase53b_fail("ls_dev", "capture");
			return -1;
		}
		if (ec != 0 || out_n <= 0)
		{
			fase53b_fail("ls_dev", "exit");
			return -1;
		}
		if (i == 0)
		{
			memset(first, 0, sizeof(first));
			strncpy(first, out, sizeof(first) - 1);
		}
		else if (strcmp(first, out) != 0)
		{
			fase53b_fail("ls_dev", "unstable_listing");
			return -1;
		}
	}

	return 0;
}

static int check_getdents_eof_stable(void)
{
	char buf[2048];
	long nread;
	int fd;
	int saw_entry;

	fd = open("/dev", O_RDONLY | O_DIRECTORY);
	if (fd < 0)
	{
		fase53b_fail("getdents_open", "open");
		return -1;
	}

	saw_entry = 0;
	for (;;)
	{
		nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
		if (nread < 0)
		{
			close(fd);
			fase53b_fail("getdents_read", "syscall");
			return -1;
		}
		if (nread == 0)
			break;
		saw_entry = 1;
	}

	if (!saw_entry)
	{
		close(fd);
		fase53b_fail("getdents_read", "empty");
		return -1;
	}

	nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
	close(fd);
	if (nread != 0)
	{
		fase53b_fail("getdents_eof", "unstable");
		return -1;
	}

	fd = open("/dev", O_RDONLY | O_DIRECTORY);
	if (fd < 0)
	{
		fase53b_fail("getdents_reopen", "open");
		return -1;
	}
	nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
	close(fd);
	if (nread <= 0)
	{
		fase53b_fail("getdents_reopen", "cursor_not_reset");
		return -1;
	}

	write_str("KTM_POSIX_PSEUDOFS_GETDENTS_CURSOR_OK\n");
	return 0;
}

static int check_access_faccessat_chdir_stat(void)
{
	struct stat st;
	int proc_exists;

	if (access("/dev/null", R_OK | W_OK) != 0)
	{
		fase53b_fail("access_dev_null", "access");
		return -1;
	}

	if (faccessat(AT_FDCWD, "/dev/zero", R_OK, 0) != 0)
	{
		fase53b_fail("faccessat_dev_zero", "faccessat");
		return -1;
	}

	if (faccessat(AT_FDCWD, "/dev/zero", R_OK, 0x40000000) == 0 || errno != EINVAL)
	{
		fase53b_fail("faccessat_bad_flags", "expected_einval");
		return -1;
	}
	write_str("KTM_POSIX_PSEUDOFS_FACCESSAT_OK\n");

	if (chdir("/tmp") != 0)
	{
		fase53b_fail("chdir_tmp", "chdir");
		return -1;
	}
	if (access(".", R_OK | W_OK | X_OK) != 0)
	{
		fase53b_fail("access_dot_after_chdir", "access");
		return -1;
	}
	write_str("KTM_POSIX_PSEUDOFS_ROUTED_PATH_OK\n");

	proc_exists = (access("/proc", F_OK) == 0);
	if (proc_exists && stat("/proc", &st) != 0)
	{
		fase53b_fail("stat_proc", "stat");
		return -1;
	}

	write_str("KTM_POSIX_PSEUDOFS_PSEUDOFS_NO_DUP_OK\n");
	return 0;
}

int main(void)
{
	write_str("KTM_POSIX_PSEUDOFS_START\n");
	write_str("KTM_POSIX_PSEUDOFS_HARNESS_ID=ktm_posix_pseudofs_smoke.c\n");

	if (check_ls_dev_twice() != 0)
		goto halt;
	if (check_getdents_eof_stable() != 0)
		goto halt;
	if (check_access_faccessat_chdir_stat() != 0)
		goto halt;

	write_str("KTM_POSIX_PSEUDOFS_OK\n");

halt:
	for (;;)
		(void)pause();

	return 0;
}
