/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE50 EXEC-only — minimal fork/execv/wait loop for /bin/busybox read flake.
 */

#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define KTM_EXEC_ONLY_N 50
#define KTM_EXEC_ONLY_PROBE_BYTES 4096

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

static void write_hex_u32(unsigned int v)
{
	const char *digits = "0123456789abcdef";
	char hex[8];
	int i;

	for (i = 7; i >= 0; i--)
	{
		hex[i] = digits[v & 0x0F];
		v >>= 4;
	}
	(void)write(1, hex, 8);
}

static uint32_t fnv1a_hash(const unsigned char *p, size_t n)
{
	size_t i;
	uint32_t h = 2166136261u;

	for (i = 0; i < n; i++)
	{
		h ^= (uint32_t)p[i];
		h *= 16777619u;
	}
	return h;
}

static int stat_looks_valid(const struct stat *st)
{
	if (!st)
		return 0;
	if (st->st_size <= 0)
		return 0;
	if (st->st_ino == 0 || st->st_ino > 65535u)
		return 0;
	return 1;
}

static int verify_busybox_invariants(int iter, ino_t *ref_ino, off_t *ref_size,
				     mode_t *ref_mode, uint32_t *ref_hash)
{
	struct stat st;
	unsigned char magic[4];
	unsigned char probe[KTM_EXEC_ONLY_PROBE_BYTES];
	ssize_t nr;
	int fd;
	uint32_t hash;
	int valid_stat;

	if (stat("/bin/busybox", &st) != 0)
	{
		write_str("[EXEC_ONLY][STAT] iter=");
		write_dec_u64((unsigned long long)iter);
		write_str(" fail errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
		write_str("[EXEC_ONLY] CLASSIFY MINIX_INODE_CORRUPTED_AFTER_N_EXECS\n");
		return -1;
	}

	valid_stat = stat_looks_valid(&st);
	write_str("[EXEC_ONLY][STAT] iter=");
	write_dec_u64((unsigned long long)iter);
	write_str(" ino=");
	write_dec_u64((unsigned long long)st.st_ino);
	write_str(" size=");
	write_dec_u64((unsigned long long)st.st_size);
	write_str(" mode=0x");
	write_hex_u32((unsigned int)st.st_mode);
	write_str(" stat_valid=");
	write_dec_u64((unsigned long long)(valid_stat ? 1ULL : 0ULL));
	write_str("\n");

	if (valid_stat)
	{
		if (*ref_ino == 0)
		{
			*ref_ino = st.st_ino;
			*ref_size = st.st_size;
			*ref_mode = st.st_mode;
		}
		else if (st.st_ino != *ref_ino || st.st_size != *ref_size ||
			 st.st_mode != *ref_mode)
		{
			write_str("[EXEC_ONLY] CLASSIFY MINIX_INODE_CORRUPTED_AFTER_N_EXECS\n");
			return -1;
		}
	}

	fd = open("/bin/busybox", O_RDONLY);
	if (fd < 0)
	{
		write_str("[EXEC_ONLY][OPEN] iter=");
		write_dec_u64((unsigned long long)iter);
		write_str(" errno=");
		write_dec_u64((unsigned long long)(unsigned int)errno);
		write_str("\n");
		return -1;
	}

	nr = read(fd, magic, 4);
	if (nr != 4 || magic[0] != 0x7F || magic[1] != 'E' ||
	    magic[2] != 'L' || magic[3] != 'F')
	{
		write_str("[EXEC_ONLY][ELF] iter=");
		write_dec_u64((unsigned long long)iter);
		write_str(" magic_bad\n");
		close(fd);
		return -1;
	}

	if (lseek(fd, 0, SEEK_SET) < 0)
	{
		close(fd);
		return -1;
	}

	nr = read(fd, probe, sizeof(probe));
	close(fd);
	if (nr <= 0)
	{
		write_str("[EXEC_ONLY][ELF] iter=");
		write_dec_u64((unsigned long long)iter);
		write_str(" probe_read_fail\n");
		return -1;
	}

	hash = fnv1a_hash(probe, (size_t)nr);
	write_str("[EXEC_ONLY][HASH] iter=");
	write_dec_u64((unsigned long long)iter);
	write_str(" probe_n=");
	write_dec_u64((unsigned long long)nr);
	write_str(" hash=0x");
	write_hex_u32(hash);
	write_str("\n");

	if (*ref_hash == 0)
		*ref_hash = hash;
	else if (hash != *ref_hash)
	{
		write_str("[EXEC_ONLY] CLASSIFY MINIX_BLOCK_CACHE_CORRUPTION\n");
		return -1;
	}

	return 0;
}

static int read_meminfo_frames(size_t *total_frames, size_t *used_frames)
{
	char buf[128];
	int fd;
	ssize_t nr;
	char *tab;
	char *end;
	unsigned long long total_kb = 0;
	unsigned long long free_kb = 0;
	unsigned long long used_kb = 0;

	if (!total_frames || !used_frames)
		return -1;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return -1;

	nr = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (nr <= 0)
		return -1;

	buf[nr] = '\0';
	tab = strchr(buf, '\t');
	if (!tab)
		return -1;
	*tab = '\0';
	total_kb = strtoull(buf, &end, 10);
	tab++;
	end = strchr(tab, '\t');
	if (!end)
		return -1;
	*end = '\0';
	free_kb = strtoull(tab, &end, 10);
	end++;
	used_kb = strtoull(end, NULL, 10);

	*total_frames = (size_t)(total_kb / 4ULL);
	*used_frames = (size_t)(used_kb / 4ULL);
	return 0;
}

static void log_pmm_iter(int iter, const char *phase, size_t used_before,
			 size_t used_after)
{
	long long delta;

	write_str("[EXEC_ONLY][PMM] iter=");
	write_dec_u64((unsigned long long)iter);
	write_str(" phase=");
	write_str(phase);
	write_str(" used_before=");
	write_dec_u64((unsigned long long)used_before);
	write_str(" used_after=");
	write_dec_u64((unsigned long long)used_after);
	delta = (long long)used_after - (long long)used_before;
	write_str(" delta=");
	if (delta < 0)
	{
		write_str("-");
		write_dec_u64((unsigned long long)(-delta));
	}
	else
		write_dec_u64((unsigned long long)delta);
	write_str("\n");
}

int main(void)
{
	char *argv_true[] = { "true", NULL };
	ino_t ref_ino = 0;
	off_t ref_size = 0;
	mode_t ref_mode = 0;
	uint32_t ref_hash = 0;
	size_t baseline_used = 0;
	size_t iter_used_after_wait = 0;
	int persistent_leak_iter = -1;
	int i;

	write_str("KTM_KTM_EXEC_ONLY_HARNESS_ID=ktm_exec_only_smoke.c\n");
	write_str("KTM_EXEC_ONLY_START\n");

	if (read_meminfo_frames(NULL, &baseline_used) == 0)
	{
		write_str("[EXEC_ONLY][PMM] baseline_used_frames=");
		write_dec_u64((unsigned long long)baseline_used);
		write_str("\n");
	}

	for (i = 0; i < KTM_EXEC_ONLY_N; i++)
	{
		pid_t pid;
		int status;
		int ec;
		size_t used_before = 0;
		size_t used_after_wait = 0;

		(void)read_meminfo_frames(NULL, &used_before);

		if (verify_busybox_invariants(i, &ref_ino, &ref_size, &ref_mode,
					      &ref_hash) != 0)
		{
			write_str("KTM_EXEC_ONLY_FAIL iter=");
			write_dec_u64((unsigned long long)i);
			write_str(" reason=invariant\n");
			goto halt;
		}

		pid = fork();
		if (pid < 0)
		{
			write_str("KTM_EXEC_ONLY_FAIL iter=");
			write_dec_u64((unsigned long long)i);
			write_str(" reason=fork errno=");
			write_dec_u64((unsigned long long)(unsigned int)errno);
			write_str("\n");
			goto halt;
		}
		if (pid == 0)
		{
			execv("/bin/busybox", argv_true);
			_exit(127);
		}

		if (waitpid(pid, &status, 0) < 0)
		{
			write_str("KTM_EXEC_ONLY_FAIL iter=");
			write_dec_u64((unsigned long long)i);
			write_str(" reason=wait errno=");
			write_dec_u64((unsigned long long)(unsigned int)errno);
			write_str("\n");
			goto halt;
		}

		(void)read_meminfo_frames(NULL, &used_after_wait);
		iter_used_after_wait = used_after_wait;
		log_pmm_iter(i, "after_wait_reap", used_before, used_after_wait);

		if (used_after_wait > used_before &&
		    (used_after_wait - used_before) > 2 &&
		    persistent_leak_iter < 0)
			persistent_leak_iter = i;

		if (WIFEXITED(status))
			ec = WEXITSTATUS(status);
		else
			ec = 128;

		if (ec != 0)
		{
			write_str("KTM_EXEC_ONLY_FAIL iter=");
			write_dec_u64((unsigned long long)i);
			write_str(" reason=exec status_raw=0x");
			write_hex_u32((unsigned int)status);
			write_str(" ec=");
			write_dec_u64((unsigned long long)ec);
			write_str("\n");
			write_str("[EXEC_ONLY] CLASSIFY KTM_EXEC_ONLY_REPRO_OK\n");
			goto halt;
		}

		write_str("KTM_EXEC_ONLY_ITER_OK ");
		write_dec_u64((unsigned long long)i);
		write_str("\n");
	}

	write_str("KTM_EXEC_ONLY_STABLE_OK\n");
	write_str("[EXEC_ONLY] CLASSIFY KTM_EXEC_ONLY_N50_STABLE_OK\n");
	if (persistent_leak_iter >= 0)
	{
		write_str("[EXEC_ONLY] CLASSIFY PMM_LEAK_PERSISTENT iter=");
		write_dec_u64((unsigned long long)persistent_leak_iter);
		write_str("\n");
	}
	else if (read_meminfo_frames(NULL, &iter_used_after_wait) == 0 &&
		 iter_used_after_wait <= baseline_used + 2)
	{
		write_str("[EXEC_ONLY] CLASSIFY PMM_RECLAIM_ON_WAIT_OK\n");
	}

halt:
	for (;;)
		(void)pause();

	return 0;
}
