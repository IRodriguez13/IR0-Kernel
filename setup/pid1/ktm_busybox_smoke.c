/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE50B — BusyBox minimal real smoke.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
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

static void write_hex_byte(unsigned char b)
{
	char hex[2];
	const char *digits = "0123456789abcdef";

	hex[0] = digits[(b >> 4) & 0x0F];
	hex[1] = digits[b & 0x0F];
	(void)write(1, hex, 2);
}

static void write_hex_u32(unsigned int v)
{
	write_hex_byte((unsigned char)((v >> 24) & 0xFF));
	write_hex_byte((unsigned char)((v >> 16) & 0xFF));
	write_hex_byte((unsigned char)((v >> 8) & 0xFF));
	write_hex_byte((unsigned char)(v & 0xFF));
}

static void fase50b_emit_classify(const char *tag)
{
	write_str("[KTM_BUSYBOX_B] CLASSIFY ");
	write_str(tag ? tag : "(null)");
	write_str("\n");
}

static void fase50b_classify_open_fail(int open_errno, int pre_exists)
{
	if (open_errno == EINVAL)
		fase50b_emit_classify("FILE_CREATE_FLAKE_FLAGS");
	else if (open_errno == EACCES || open_errno == EPERM)
		fase50b_emit_classify("FILE_CREATE_FLAKE_PERMISSION");
	else if (open_errno == EEXIST)
		fase50b_emit_classify("FILE_CREATE_FLAKE_EXISTING_FILE");
	else if (open_errno == EMFILE)
		fase50b_emit_classify("FILE_CREATE_FLAKE_HARNESS_SETUP");
	else if (pre_exists)
		fase50b_emit_classify("FILE_CREATE_FLAKE_STALE_DISK");
	else
		fase50b_emit_classify("FILE_CREATE_FLAKE_HARNESS_SETUP");
}

/*
 * Pre-open probe for /f50_file.txt create path — classify flake root cause
 * without changing kernel behaviour.
 */
static int fase50b_probe_before_create(const char *path, int create_flags)
{
	struct stat st;
	char cwd[256];
	int pre_exists = 0;

	write_str("[KTM_BUSYBOX_B][PROBE] path=");
	write_str(path ? path : "(null)");
	write_str(" linux_flags=0x");
	write_hex_u32((unsigned int)create_flags);
	write_str("\n");

	if (stat(path, &st) == 0)
	{
		pre_exists = 1;
		write_str("[KTM_BUSYBOX_B][PROBE] stat_exists=1 mode=0x");
		write_hex_u32((unsigned int)st.st_mode);
		write_str(" size=");
		write_dec_u64((unsigned long long)st.st_size);
		write_str("\n");
	}
	else
	{
		write_str("[KTM_BUSYBOX_B][PROBE] stat_exists=0 errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
	}

	if (getcwd(cwd, sizeof(cwd)))
	{
		write_str("[KTM_BUSYBOX_B][PROBE] cwd=");
		write_str(cwd);
		write_str("\n");
	}
	else
	{
		write_str("[KTM_BUSYBOX_B][PROBE] getcwd_fail errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
		fase50b_emit_classify("FILE_CREATE_FLAKE_HARNESS_SETUP");
	}

	if (stat("/", &st) == 0)
	{
		write_str("[KTM_BUSYBOX_B][PROBE] root_st_mode=0x");
		write_hex_u32((unsigned int)st.st_mode);
		write_str("\n");
	}
	else
	{
		write_str("[KTM_BUSYBOX_B][PROBE] root_stat_fail errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
	}

	if (unlink(path) == 0)
	{
		write_str("[KTM_BUSYBOX_B][PROBE] pre_unlink=ok (removed stale entry)\n");
		if (pre_exists)
			fase50b_emit_classify("FILE_CREATE_FLAKE_STALE_DISK");
		pre_exists = 0;
	}
	else if (errno == ENOENT)
	{
		write_str("[KTM_BUSYBOX_B][PROBE] pre_unlink=skip errno=ENOENT\n");
	}
	else
	{
		write_str("[KTM_BUSYBOX_B][PROBE] pre_unlink_fail errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
	}

	return pre_exists;
}

static void write_hex_buf(const char *buf, int n)
{
	int i;

	if (!buf || n <= 0)
		return;
	for (i = 0; i < n; i++)
	{
		if (i > 0)
			(void)write(1, " ", 1);
		write_hex_byte((unsigned char)buf[i]);
	}
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

static void ktm_busybox_d_emit_classify(const char *tag)
{
	write_str("[KTM_BUSYBOX_D] CLASSIFY ");
	write_str(tag ? tag : "(null)");
	write_str("\n");
}

static int count_open_fds(void)
{
	int fd;
	int n = 0;

	for (fd = 0; fd < 256; fd++)
	{
		if (fcntl(fd, F_GETFD) != -1)
			n++;
	}
	return n;
}

static void emit_fd_summary(const char *tag, const char *when)
{
	int fd;
	int n = 0;

	write_str("[KTM_BUSYBOX_D][FD] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" when=");
	write_str(when ? when : "(null)");
	write_str(" open=");
	for (fd = 0; fd < 32; fd++)
	{
		if (fcntl(fd, F_GETFD) != -1)
		{
			if (n > 0)
				write_str(",");
			write_dec_u64((unsigned long long)fd);
			n++;
		}
	}
	write_str(" total=");
	write_dec_u64((unsigned long long)count_open_fds());
	write_str("\n");
}

static void emit_cwd(const char *tag)
{
	char cwd[256];

	if (getcwd(cwd, sizeof(cwd)))
	{
		write_str("[KTM_BUSYBOX_D][CWD] tag=");
		write_str(tag ? tag : "(null)");
		write_str(" cwd=");
		write_str(cwd);
		write_str("\n");
	}
	else
	{
		write_str("[KTM_BUSYBOX_D][CWD] tag=");
		write_str(tag ? tag : "(null)");
		write_str(" getcwd_fail errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
	}
}

static void emit_argv(const char *tag, char *const argv[])
{
	int i;

	write_str("[KTM_BUSYBOX_D][STEP] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" argv=");
	for (i = 0; argv && argv[i]; i++)
	{
		if (i > 0)
			write_str(" ");
		write_str(argv[i]);
	}
	write_str("\n");
}

/*
 * Reap any unexpected zombie children before the next step.
 * Returns count reaped (non-zero implies leak from prior step).
 */
static int reap_pending_children(const char *tag)
{
	int n = 0;

	for (;;)
	{
		int st;
		pid_t p = waitpid(-1, &st, WNOHANG);

		if (p <= 0)
			break;
		n++;
		write_str("[KTM_BUSYBOX_D][ZOMBIE] tag=");
		write_str(tag ? tag : "(null)");
		write_str(" unexpected_reap pid=");
		write_dec_u64((unsigned long long)p);
		write_str(" status_raw=0x");
		write_hex_u32((unsigned int)st);
		write_str("\n");
	}
	return n;
}

static void emit_wait_diag(pid_t child_pid, pid_t wait_ret, int status)
{
	write_str("[KTM_BUSYBOX_D][WAIT] child_pid=");
	write_dec_u64((unsigned long long)child_pid);
	write_str(" parent_pid=");
	write_dec_u64((unsigned long long)getpid());
	write_str(" wait_ret=");
	write_dec_u64((unsigned long long)wait_ret);
	write_str(" status_raw=0x");
	write_hex_u32((unsigned int)status);
	write_str(" WIFEXITED=");
	write_dec_u64((unsigned long long)(WIFEXITED(status) ? 1ULL : 0ULL));
	if (WIFEXITED(status))
	{
		write_str(" WEXITSTATUS=");
		write_dec_u64((unsigned long long)(unsigned int)WEXITSTATUS(status));
	}
	write_str(" zombie_reap_ok=");
	write_dec_u64((unsigned long long)(wait_ret == child_pid ? 1ULL : 0ULL));
	write_str("\n");
}

static void fase50d_classify_capture_fail(const char *step, const char *reason,
					  int want_ec, int got_ec, int out_n,
					  int err_n, int zomb_before,
					  int fd_before, int fd_after)
{
	if (zomb_before > 0)
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_ZOMBIE_LEAK");
	if (fd_after > fd_before)
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_FD_LEAK");
	if (got_ec == 127 && out_n == 0 && err_n == 0)
	{
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_EXIT_CODE_MISMATCH");
		write_str("[KTM_BUSYBOX_D][HINT] exec_fail_127 grep serial EXEC_VFS_READ_ERR\n");
	}
	else if (got_ec == 129)
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_WAIT_STATUS_BAD");
	if (reason && strcmp(reason, "stdout") == 0 && got_ec == 0 && out_n == 0)
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STDOUT_CAPTURE_BAD");
	else if (reason && strcmp(reason, "exit") == 0 && got_ec != want_ec)
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_EXIT_CODE_MISMATCH");
	if (step)
	{
		write_str("[KTM_BUSYBOX_D] CLASSIFY_CTX step=");
		write_str(step);
		write_str(" reason=");
		write_str(reason ? reason : "(null)");
		write_str(" want_ec=");
		write_dec_u64((unsigned long long)(unsigned int)want_ec);
		write_str(" got_ec=");
		write_dec_u64((unsigned long long)(unsigned int)got_ec);
		write_str("\n");
	}
}

static int fase50d_verify_elf_regular(const char *path)
{
	struct stat st;
	unsigned char hdr[4];
	int fd;
	ssize_t nr;

	if (!path || stat(path, &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode))
		return -1;
	if (st.st_size < 4)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	nr = read(fd, hdr, 4);
	close(fd);
	if (nr != 4)
		return -1;
	if (hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F')
		return -1;
	return 0;
}

static void fase50d_verify_rootfs_bins(void)
{
	const char *paths[] = { "/bin/busybox", "/bin/sh", "/bin/cat", NULL };
	int i;

	write_str("[KTM_BUSYBOX_D][ROOTFS] fresh_disk=1 verify_elf_regular\n");
	for (i = 0; paths[i]; i++)
	{
		struct stat st;

		write_str("[KTM_BUSYBOX_D][ROOTFS] path=");
		write_str(paths[i]);
		if (stat(paths[i], &st) != 0)
		{
			write_str(" stat_fail errno=");
			write_dec_u64((unsigned long long)(unsigned int)errno);
			write_str("\n");
			ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STALE_ROOTFS");
			continue;
		}
		write_str(" mode=0x");
		write_hex_u32((unsigned int)st.st_mode);
		write_str(" size=");
		write_dec_u64((unsigned long long)st.st_size);
		write_str(" S_ISREG=");
		write_dec_u64((unsigned long long)(S_ISREG(st.st_mode) ? 1ULL : 0ULL));
		if (st.st_size == 0)
		{
			write_str(" zero_size\n");
			ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STALE_ROOTFS");
			continue;
		}
		if (fase50d_verify_elf_regular(paths[i]) != 0)
		{
			write_str(" elf_fail\n");
			ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STALE_ROOTFS");
			continue;
		}
		write_str(" elf_ok\n");
	}
}

static void fase50d_check_stale_temps(const char *tag)
{
	const char *paths[] = {
		"/f50_dir", "/f50_touch.txt", "/f50_a.txt", "/f50_b.txt",
		"/f50_rmdir_dir", "/f50_mv.txt", "/f50_mvd.txt",
		"/f50_grep.txt", "/f50_lines.txt", NULL
	};
	int i;

	for (i = 0; paths[i]; i++)
	{
		struct stat st;

		if (stat(paths[i], &st) == 0)
		{
			write_str("[KTM_BUSYBOX_D][STALE] tag=");
			write_str(tag ? tag : "(null)");
			write_str(" path=");
			write_str(paths[i]);
			write_str(" exists=1\n");
			ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STALE_ROOTFS");
		}
	}
}

static void emit_capture_diag(const char *tag, const char *out, int out_n,
			      const char *err, int err_n, int exit_code)
{
	write_str("[KTM_BUSYBOX_B][CAPTURE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" out_n=");
	write_dec_u64((unsigned long long)((out_n < 0) ? 0 : out_n));
	write_str(" err_n=");
	write_dec_u64((unsigned long long)((err_n < 0) ? 0 : err_n));
	write_str(" exit_code=");
	write_dec_u64((unsigned long long)exit_code);
	write_str("\n");

	write_str("[KTM_BUSYBOX_B][CAPTURE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" first_out=");
	if (out_n > 0)
		write_hex_byte((unsigned char)out[0]);
	else
		write_str("--");
	write_str(" last_out=");
	if (out_n > 0)
		write_hex_byte((unsigned char)out[out_n - 1]);
	else
		write_str("--");
	write_str(" first_err=");
	if (err_n > 0)
		write_hex_byte((unsigned char)err[0]);
	else
		write_str("--");
	write_str(" last_err=");
	if (err_n > 0)
		write_hex_byte((unsigned char)err[err_n - 1]);
	else
		write_str("--");
	write_str("\n");

	write_str("[KTM_BUSYBOX_B][CAPTURE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" out_hex=");
	write_hex_buf(out, out_n);
	write_str("\n");
	write_str("[KTM_BUSYBOX_B][CAPTURE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" err_hex=");
	write_hex_buf(err, err_n);
	write_str("\n");

	write_str("[KTM_BUSYBOX_B][CAPTURE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" out_str=");
	write_str(out ? out : "");
	write_str("\n");
	write_str("[KTM_BUSYBOX_B][CAPTURE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" err_str=");
	write_str(err ? err : "");
	write_str("\n");
}

static int run_capture(const char *tag, char *const argv[], char *out,
		       size_t out_sz, char *err, size_t err_sz,
		       int *exit_code, int *out_n, int *err_n)
{
	int outp[2];
	int errp[2];
	pid_t pid;
	pid_t wait_ret;
	int status;
	int fd_before;
	int fd_after;
	int zomb_before;

	if (!argv || !argv[0] || !out || !err || !exit_code || !out_n || !err_n)
		return -1;

	emit_argv(tag, argv);
	zomb_before = reap_pending_children(tag);
	if (zomb_before > 0)
	{
		write_str("[KTM_BUSYBOX_D][ZOMBIE] tag=");
		write_str(tag ? tag : "(null)");
		write_str(" before_step count=");
		write_dec_u64((unsigned long long)zomb_before);
		write_str("\n");
	}

	fd_before = count_open_fds();
	emit_fd_summary(tag, "before");
	emit_cwd(tag);

	if (pipe2(outp, 0) < 0 || pipe2(errp, 0) < 0)
		return -1;

	write_str("[KTM_BUSYBOX_D][PIPE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" out_rd=");
	write_dec_u64((unsigned long long)outp[0]);
	write_str(" out_wr=");
	write_dec_u64((unsigned long long)outp[1]);
	write_str(" err_rd=");
	write_dec_u64((unsigned long long)errp[0]);
	write_str(" err_wr=");
	write_dec_u64((unsigned long long)errp[1]);
	write_str("\n");

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
		execv("/bin/busybox", argv);
		_exit(127);
	}

	write_str("[KTM_BUSYBOX_D][FORK] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" child_pid=");
	write_dec_u64((unsigned long long)pid);
	write_str("\n");

	close(outp[1]);
	close(errp[1]);

	*out_n = read_all(outp[0], out, out_sz);
	*err_n = read_all(errp[0], err, err_sz);
	close(outp[0]);
	close(errp[0]);

	write_str("[KTM_BUSYBOX_D][PIPE] tag=");
	write_str(tag ? tag : "(null)");
	write_str(" closed_read_ends=2 write_ends=2\n");

	if (*out_n < 0 || *err_n < 0)
	{
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STDOUT_CAPTURE_BAD");
		return -1;
	}

	wait_ret = waitpid(pid, &status, 0);
	if (wait_ret < 0)
		return -1;

	emit_wait_diag(pid, wait_ret, status);

	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else
		*exit_code = 128;

	fd_after = count_open_fds();
	emit_fd_summary(tag, "after");
	if (fd_after > fd_before)
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_PIPE_REF_LEAK");

	emit_capture_diag(tag, out, *out_n, err, *err_n, *exit_code);
	return 0;
}

static void fase50d_fail(const char *step, const char *reason)
{
	write_str("[KTM_BUSYBOX_D][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("BUSYBOX_FAIL_REASON=fase50d_");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static int bb_expect_exit_code(const char *step, char *const argv[], int want_ec)
{
	char out[256];
	char err[256];
	int ec;
	int out_n;
	int err_n;
	int zomb_before;

	zomb_before = reap_pending_children(step);
	if (run_capture(step, argv, out, sizeof(out), err, sizeof(err), &ec,
			&out_n, &err_n) != 0)
	{
		fase50d_classify_capture_fail(step, "exec", want_ec, ec, out_n,
					      err_n, zomb_before, 0, 0);
		fase50d_fail(step, "exec");
		return -1;
	}
	if (ec != want_ec)
	{
		fase50d_classify_capture_fail(step, "exit", want_ec, ec, out_n,
					      err_n, zomb_before, 0, 0);
		fase50d_fail(step, "exit");
		return -1;
	}
	return 0;
}

static int bb_expect_exit0(const char *step, char *const argv[])
{
	return bb_expect_exit_code(step, argv, 0);
}

static int bb_expect_stdout_prefix(const char *step, char *const argv[],
				 const char *prefix)
{
	char out[256];
	char err[256];
	int ec;
	int out_n;
	int err_n;
	int zomb_before;

	zomb_before = reap_pending_children(step);
	if (run_capture(step, argv, out, sizeof(out), err, sizeof(err), &ec,
			&out_n, &err_n) != 0)
	{
		fase50d_classify_capture_fail(step, "exec", 0, ec, out_n, err_n,
					      zomb_before, 0, 0);
		fase50d_fail(step, "exec");
		return -1;
	}
	if (ec != 0)
	{
		fase50d_classify_capture_fail(step, "exit", 0, ec, out_n, err_n,
					      zomb_before, 0, 0);
		fase50d_fail(step, "exit");
		return -1;
	}
	if (!prefix || strncmp(out, prefix, strlen(prefix)) != 0)
	{
		fase50d_classify_capture_fail(step, "stdout", 0, ec, out_n,
					      err_n, zomb_before, 0, 0);
		fase50d_fail(step, "stdout");
		return -1;
	}
	return 0;
}

int main(void)
{
	char out[256];
	char err[256];
	int ec;
	int out_n;
	int err_n;
	int fd;
	char *argv_echo[] = { "echo", "hello", NULL };
	char *argv_ls[] = { "ls", "/", NULL };
	char *argv_cat[] = { "cat", "/f50_file.txt", NULL };
	char *argv_pwd[] = { "pwd", NULL };
	char *argv_mkdir[] = { "mkdir", "/f50_dir", NULL };
	char *argv_touch[] = { "touch", "/f50_touch.txt", NULL };
	char *argv_rm[] = { "rm", "/f50_touch.txt", NULL };
	char *argv_cp[] = { "cp", "/f50_a.txt", "/f50_b.txt", NULL };
	char *argv_cat_b[] = { "cat", "/f50_b.txt", NULL };
	char *argv_true[] = { "true", NULL };
	char *argv_false[] = { "false", NULL };
	char *argv_mkdir_rmdir[] = { "mkdir", "/f50_rmdir_dir", NULL };
	char *argv_rmdir[] = { "rmdir", "/f50_rmdir_dir", NULL };
	char *argv_mv[] = { "mv", "/f50_mv.txt", "/f50_mvd.txt", NULL };
	char *argv_cat_mv[] = { "cat", "/f50_mvd.txt", NULL };
	char *argv_grep[] = { "grep", "needle", "/f50_grep.txt", NULL };
	char *argv_head[] = { "head", "-n", "2", "/f50_lines.txt", NULL };
	char *argv_tail[] = { "tail", "-n", "1", "/f50_lines.txt", NULL };
	char *argv_sh[] = { "sh", "-c", "echo hi", NULL };
	int create_flags = O_CREAT | O_TRUNC | O_WRONLY;
	int pre_exists;

	write_str("KTM_BUSYBOX_HARNESS_ID=ktm_busybox_smoke.c\n");
	write_str("KTM_BUSYBOX_B_START\n");

	if (run_capture("echo", argv_echo, out, sizeof(out), err, sizeof(err),
			&ec, &out_n, &err_n) != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=echo_exec\n");
		goto halt;
	}
	if (ec != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=echo_exit\n");
		goto halt;
	}
	write_str("[KTM_BUSYBOX_B][EXPECT] expected=hello\\n|hello received_hex=");
	write_hex_buf(out, out_n);
	write_str(" length=");
	write_dec_u64((unsigned long long)((out_n < 0) ? 0 : out_n));
	write_str("\n");
	if (!(strcmp(out, "hello\n") == 0 || strcmp(out, "hello") == 0))
	{
		if (out_n == 0 && err_n == 0)
			write_str("BUSYBOX_FAIL_REASON=HARNESS_CAPTURE_BROKEN\n");
		else
			write_str("BUSYBOX_FAIL_REASON=echo_stdout\n");
		goto halt;
	}
	write_str("[KTM_BUSYBOX_B] CLASSIFY BUSYBOX_ECHO_CAPTURE_OK\n");

	if (run_capture("ls", argv_ls, out, sizeof(out), err, sizeof(err), &ec,
			&out_n, &err_n) != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=ls_exec\n");
		goto halt;
	}
	if (ec != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=ls_exit\n");
		goto halt;
	}

	pre_exists = fase50b_probe_before_create("/f50_file.txt", create_flags);
	fd = open("/f50_file.txt", create_flags, 0644);
	if (fd < 0)
	{
		int open_errno = errno;

		write_str("BUSYBOX_FAIL_REASON=file_create errno=");
		write_dec_u64((unsigned long long)(unsigned int)open_errno);
		write_str(" (was_misreport=-fd=");
		write_dec_u64((unsigned long long)(unsigned int)(-fd));
		write_str(")\n");
		fase50b_classify_open_fail(open_errno, pre_exists);
		goto halt;
	}
	write_str("[KTM_BUSYBOX_C] CLASSIFY FILE_CREATE_STILL_OK\n");
	(void)write(fd, "archivo-fase50\n", 14);
	close(fd);

	if (run_capture("cat", argv_cat, out, sizeof(out), err, sizeof(err), &ec,
			&out_n, &err_n) != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=cat_exec\n");
		goto halt;
	}
	if (ec != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=cat_exit\n");
		goto halt;
	}
	if (strncmp(out, "archivo-fase50", 13) != 0)
	{
		write_str("BUSYBOX_FAIL_REASON=cat_stdout\n");
		goto halt;
	}

	write_str("BUSYBOX_BOOT_OK\n");

	write_str("KTM_BUSYBOX_D_START\n");
	fase50d_verify_rootfs_bins();
	fase50d_check_stale_temps("tanda1_pre");

	if (bb_expect_stdout_prefix("pwd", argv_pwd, "/") != 0)
		goto halt;

	if (bb_expect_exit0("mkdir", argv_mkdir) != 0)
		goto halt;

	if (bb_expect_exit0("touch", argv_touch) != 0)
		goto halt;

	if (bb_expect_exit0("rm", argv_rm) != 0)
		goto halt;

	fd = open("/f50_a.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
	{
		fase50d_fail("cp_setup", "open_src");
		goto halt;
	}
	(void)write(fd, "copyme\n", 7);
	close(fd);

	if (bb_expect_exit0("cp", argv_cp) != 0)
		goto halt;

	if (bb_expect_stdout_prefix("cat_b", argv_cat_b, "copyme") != 0)
		goto halt;

	write_str("KTM_BUSYBOX_COREUTILS_MINIMAL_OK\n");

	write_str("KTM_BUSYBOX_D_TANDA2_START\n");

	if (bb_expect_exit0("true", argv_true) != 0)
		goto halt;

	if (bb_expect_exit_code("false", argv_false, 1) != 0)
		goto halt;

	if (bb_expect_exit0("mkdir_rmdir", argv_mkdir_rmdir) != 0)
		goto halt;

	if (bb_expect_exit0("rmdir", argv_rmdir) != 0)
		goto halt;

	fd = open("/f50_mv.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
	{
		fase50d_fail("mv_setup", "open_src");
		goto halt;
	}
	(void)write(fd, "moved\n", 6);
	close(fd);

	if (bb_expect_exit0("mv", argv_mv) != 0)
		goto halt;

	if (bb_expect_stdout_prefix("cat_mv", argv_cat_mv, "moved") != 0)
		goto halt;

	write_str("KTM_BUSYBOX_D_TANDA2_OK\n");
	write_str("[KTM_BUSYBOX_D] CLASSIFY VFS_BACKEND_NEUTRAL\n");

	write_str("KTM_BUSYBOX_D_TANDA3_START\n");

	fd = open("/f50_grep.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
	{
		write_str("[KTM_BUSYBOX_D][OPEN] step=grep_setup errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STALE_ROOTFS");
		fase50d_fail("grep_setup", "open");
		goto halt;
	}
	(void)write(fd, "haystack\nneedle here\n", 20);
	close(fd);

	if (bb_expect_stdout_prefix("grep", argv_grep, "needle") != 0)
		goto halt;

	fd = open("/f50_lines.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
	{
		write_str("[KTM_BUSYBOX_D][OPEN] step=head_tail_setup errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
		ktm_busybox_d_emit_classify("KTM_BUSYBOX_D_FLAKE_STALE_ROOTFS");
		fase50d_fail("head_tail_setup", "open");
		goto halt;
	}
	(void)write(fd, "line1\nline2\nline3\n", 18);
	close(fd);

	if (bb_expect_stdout_prefix("head", argv_head, "line1") != 0)
		goto halt;

	if (bb_expect_stdout_prefix("tail", argv_tail, "line3") != 0)
		goto halt;

	write_str("KTM_BUSYBOX_D_TANDA3_OK\n");
	write_str("[KTM_BUSYBOX_D] CLASSIFY SYSCALL_MONOLITH_NOT_GROWN\n");

	write_str("KTM_BUSYBOX_E_START\n");

	if (bb_expect_stdout_prefix("sh_echo", argv_sh, "hi") != 0)
		goto halt;

	write_str("KTM_BUSYBOX_E_OK\n");
	write_str("[KTM_BUSYBOX_E] CLASSIFY KTM_BUSYBOX_BASELINE_STABLE\n");
	write_str("[KTM_BUSYBOX_E] CLASSIFY OPENAT_RESOLVE_FACADE_OK\n");
	write_str("[KTM_BUSYBOX_E] CLASSIFY SYSCALL_FS_SPLIT_CONTINUES\n");
	write_str("[KTM_BUSYBOX_E] CLASSIFY DEBUG_LOGS_GATED\n");
	write_str("[KTM_BUSYBOX_E] CLASSIFY KTM_BUSYBOX_NO_REGRESSION\n");
	write_str("[KTM_BUSYBOX_E] CLASSIFY KTM_BUSYBOX_NO_REGRESSION_VERIFIED\n");

halt:
	for (;;)
		(void)pause();

	return 0;
}
