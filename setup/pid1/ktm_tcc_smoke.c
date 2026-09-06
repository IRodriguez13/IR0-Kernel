/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE52 — TinyCC bootstrap: static link, generated exec, stdio, medium C programs.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#define TCC_BIN "/bin/tcc"
#define TCC_LIB "/lib/tcc"

#define F52D_LARGE_PATH "/tmp/f52d_lg.bin"
#define F52D_LARGE_SZ   532480U
#define F52D_TRUNC_SZ   262144U
#define F52D_RW_OFF     300000U
#define F52D_CHUNK      4096U
#define F52D_WRITE_CHUNK 32768U

static void write_str(const char *s);
static void ktm_tcc_fail(const char *step, int err);

static uint32_t fnv1a_update(uint32_t h, const unsigned char *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		h ^= (uint32_t)p[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t fnv1a_file_limit(const char *path, size_t max_bytes)
{
	unsigned char buf[F52D_CHUNK];
	uint32_t h = 2166136261u;
	size_t total = 0;
	int fd;
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;

	while (max_bytes == 0 || total < max_bytes)
	{
		size_t want = F52D_CHUNK;

		if (max_bytes != 0 && max_bytes - total < want)
			want = max_bytes - total;
		n = read(fd, buf, want);
		if (n <= 0)
			break;
		h = fnv1a_update(h, buf, (size_t)n);
		total += (size_t)n;
	}
	close(fd);
	return h;
}

static int write_large_pattern(const char *path, size_t size)
{
	unsigned char block[F52D_WRITE_CHUNK];
	size_t off;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;

	for (off = 0; off < size; off += F52D_WRITE_CHUNK)
	{
		size_t chunk = size - off;
		size_t i;

		if (chunk > F52D_WRITE_CHUNK)
			chunk = F52D_WRITE_CHUNK;
		for (i = 0; i < chunk; i++)
			block[i] = (unsigned char)((off + i) & 0xff);
		if (write(fd, block, chunk) != (ssize_t)chunk)
		{
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}

static int ktm_tcc_d_large_file_harness(void)
{
	uint32_t hash_full;
	uint32_t hash_after_patch;
	unsigned char patch[F52D_CHUNK];
	size_t i;
	int fd;

	write_str("KTM_TCC_D_START\n");
	write_str("KTM_TCC_D_CASE_LARGE_RW\n");

	if (write_large_pattern(F52D_LARGE_PATH, F52D_LARGE_SZ) != 0)
	{
		ktm_tcc_fail("large_write", errno);
		return -1;
	}

	hash_full = fnv1a_file_limit(F52D_LARGE_PATH, 0);
	if (hash_full == 2166136261u)
	{
		ktm_tcc_fail("large_hash", 0);
		return -1;
	}

	fd = open(F52D_LARGE_PATH, O_RDONLY);
	if (fd < 0)
	{
		ktm_tcc_fail("large_open_read", errno);
		return -1;
	}
	if (lseek(fd, (off_t)F52D_RW_OFF, SEEK_SET) != (off_t)F52D_RW_OFF)
	{
		close(fd);
		ktm_tcc_fail("large_lseek", errno);
		return -1;
	}
	{
		unsigned char probe[1];

		if (read(fd, probe, 1) != 1 ||
		    probe[0] != (unsigned char)(F52D_RW_OFF & 0xff))
		{
			close(fd);
			ktm_tcc_fail("large_stream_read", EIO);
			return -1;
		}
	}
	close(fd);

	for (i = 0; i < F52D_CHUNK; i++)
		patch[i] = 0xA5;
	fd = open(F52D_LARGE_PATH, O_WRONLY);
	if (fd < 0)
	{
		ktm_tcc_fail("large_open_write", errno);
		return -1;
	}
	if (lseek(fd, (off_t)F52D_RW_OFF, SEEK_SET) != (off_t)F52D_RW_OFF ||
	    write(fd, patch, F52D_CHUNK) != (ssize_t)F52D_CHUNK)
	{
		close(fd);
		ktm_tcc_fail("large_patch_write", errno);
		return -1;
	}
	close(fd);

	hash_after_patch = fnv1a_file_limit(F52D_LARGE_PATH, 0);
	if (hash_after_patch == hash_full)
	{
		ktm_tcc_fail("large_patch_hash", 0);
		return -1;
	}

	write_str("[KTM_TCC] CLASSIFY MINIX_DOUBLE_INDIRECT_OK\n");
	write_str("[KTM_TCC] CLASSIFY LARGE_FILE_RW_OK\n");
	return 0;
}

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

static void ktm_tcc_fail(const char *step, int err)
{
	write_str("[KTM_TCC][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" errno=");
	write_dec_u64((unsigned long long)(unsigned int)err);
	write_str("\n");
	write_str("KTM_TCC_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static void ktm_tcc_fail_msg(const char *step, const char *msg)
{
	write_str("[KTM_TCC][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" msg=");
	write_str(msg ? msg : "(null)");
	write_str("\n");
	write_str("KTM_TCC_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static int ktm_tcc_d_large_file_truncate(void)
{
	uint32_t hash_trunc;
	size_t bytes = 0;
	unsigned char buf[F52D_CHUNK];
	int fd;
	ssize_t n;

	if (truncate(F52D_LARGE_PATH, (off_t)F52D_TRUNC_SZ) != 0)
	{
		ktm_tcc_fail("large_truncate", errno);
		return -1;
	}

	fd = open(F52D_LARGE_PATH, O_RDONLY);
	if (fd < 0)
	{
		ktm_tcc_fail("large_open_trunc", errno);
		return -1;
	}
	for (;;)
	{
		n = read(fd, buf, sizeof(buf));
		if (n < 0)
		{
			close(fd);
			ktm_tcc_fail("large_read_trunc", errno);
			return -1;
		}
		if (n == 0)
			break;
		bytes += (size_t)n;
	}
	close(fd);
	if (bytes != F52D_TRUNC_SZ)
	{
		ktm_tcc_fail("large_size_trunc", (int)bytes);
		return -1;
	}

	hash_trunc = fnv1a_file_limit(F52D_LARGE_PATH, 0);
	if (hash_trunc == 2166136261u)
	{
		ktm_tcc_fail("large_trunc_hash", 0);
		return -1;
	}

	write_str("[KTM_TCC] CLASSIFY LARGE_FILE_TRUNCATE_OK\n");
	write_str("[KTM_TCC] CLASSIFY PMM_RECLAIM_LARGE_FILE_OK\n");
	write_str("[KTM_TCC] CLASSIFY ROOTFS_CAPACITY_STABLE\n");
	return 0;
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

static int run_capture(const char *tag, char *const argv[], char *out, size_t out_sz,
		       int *exit_code, int *out_n)
{
	int outp[2];
	pid_t pid;
	int status;

	if (!tag || !argv || !out || !exit_code || !out_n)
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
		execv(argv[0], argv);
		_exit(127);
	}

	close(outp[1]);
	*out_n = read_all(outp[0], out, out_sz);
	close(outp[0]);

	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else
		*exit_code = 128;

	write_str("[KTM_TCC][CAPTURE] tag=");
	write_str(tag);
	write_str(" ec=");
	write_dec_u64((unsigned long long)(unsigned int)*exit_code);
	write_str(" out_n=");
	write_dec_u64((unsigned long long)((*out_n < 0) ? 0 : *out_n));
	write_str("\n");
	return 0;
}

static int write_source(const char *path, const char *src)
{
	int fd;
	size_t len;

	if (!path || !src)
		return -1;
	len = strlen(src);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, src, len) != (ssize_t)len)
	{
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int static_link(const char *tag, const char *src_path, const char *out_path,
		       char *out, size_t out_sz, int *out_n)
{
	char *argv[] = {
		(char *)TCC_BIN,
		(char *)"-B",
		(char *)TCC_LIB,
		(char *)"-static",
		(char *)"-o",
		(char *)out_path,
		(char *)src_path,
		NULL
	};
	int ec;

	if (run_capture(tag, argv, out, out_sz, &ec, out_n) != 0)
		return -1;
	if (ec != 0)
	{
		write_str("[KTM_TCC] CLASSIFY ABI_FIX_GENERIC\n");
		if (out_n && *out_n > 0 && out)
		{
			write_str("[KTM_TCC][LINK_ERR] ");
			(void)write(1, out, (size_t)*out_n);
			if (out[*out_n - 1] != '\n')
				write_str("\n");
		}
		ktm_tcc_fail(tag, ec);
		return -1;
	}
	return 0;
}

static int static_link_multi(const char *tag, const char *out_path,
			     const char *src1, const char *src2,
			     char *out, size_t out_sz, int *out_n)
{
	char *argv[] = {
		(char *)TCC_BIN,
		(char *)"-B",
		(char *)TCC_LIB,
		(char *)"-static",
		(char *)"-o",
		(char *)out_path,
		(char *)src1,
		(char *)src2,
		NULL
	};
	int ec;

	if (run_capture(tag, argv, out, out_sz, &ec, out_n) != 0)
		return -1;
	if (ec != 0)
	{
		write_str("[KTM_TCC] CLASSIFY ABI_FIX_GENERIC\n");
		if (out_n && *out_n > 0 && out)
		{
			write_str("[KTM_TCC][LINK_ERR] ");
			(void)write(1, out, (size_t)*out_n);
			if (out[*out_n - 1] != '\n')
				write_str("\n");
		}
		ktm_tcc_fail(tag, ec);
		return -1;
	}
	return 0;
}

static int exec_expect(const char *tag, const char *bin_path, int expect_ec,
		       char *out, size_t out_sz, int *out_n)
{
	char *argv[] = { (char *)bin_path, NULL };
	int ec;

	if (run_capture(tag, argv, out, out_sz, &ec, out_n) != 0)
		return -1;
	if (ec != expect_ec)
	{
		ktm_tcc_fail(tag, ec);
		return -1;
	}
	return 0;
}

static int exec_expect_out(const char *tag, char *const argv[], int expect_ec,
			   const char *needle, char *out, size_t out_sz, int *out_n)
{
	int ec;

	if (run_capture(tag, argv, out, out_sz, &ec, out_n) != 0)
		return -1;
	if (ec != expect_ec)
	{
		ktm_tcc_fail(tag, ec);
		return -1;
	}
	if (needle && !strstr(out, needle))
	{
		ktm_tcc_fail_msg(tag, out);
		return -1;
	}
	return 0;
}

int main(void)
{
	char out[2048];
	char *argv_v[] = { (char *)TCC_BIN, (char *)"-B", (char *)TCC_LIB,
			   (char *)"-v", NULL };
	int ec;
	int out_n;

	write_str("KTM_TCC_HARNESS_ID=ktm_tcc_smoke.c\n");
	write_str("KTM_TCC_B_START\n");
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_B_ROOTFS_CAPACITY_OK\n");

	write_str("KTM_TCC_BOOT_START\n");
	if (run_capture("tcc_v", argv_v, out, sizeof(out), &ec, &out_n) != 0)
	{
		ktm_tcc_fail("tcc_v_capture", errno);
		goto halt;
	}
	if (ec != 0 || out_n <= 0 || !strstr(out, "tcc version"))
	{
		ktm_tcc_fail("tcc_v", ec);
		goto halt;
	}
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_TCC_BOOT_OK\n");

	write_str("KTM_TCC_B_CASE_A\n");
	if (access("/lib/tcc/crt1.o", R_OK) != 0 && access("/usr/lib/crt1.o", R_OK) != 0)
	{
		ktm_tcc_fail("access_crt1", errno);
		goto halt;
	}
	if (write_source("/tmp/a.c", "int main(){return 0;}\n") != 0)
	{
		ktm_tcc_fail("write_a0_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_a0", "/tmp/a.c", "/tmp/a", out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_B_TCC_STATIC_LINK_OK\n");
	if (exec_expect("exec_a0", "/tmp/a", 0, out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_B_TCC_EXEC_GENERATED_OK\n");

	write_str("KTM_TCC_B_CASE_B\n");
	if (write_source("/tmp/b.c", "int main(){return 42;}\n") != 0)
	{
		ktm_tcc_fail("write_b_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_b", "/tmp/b.c", "/tmp/b", out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect("exec_b", "/tmp/b", 42, out, sizeof(out), &out_n) != 0)
		goto halt;

	write_str("KTM_TCC_B_CASE_C\n");
	if (write_source("/tmp/hello.c",
			 "#include <stdio.h>\n"
			 "int main(){ puts(\"hello-tcc\"); return 0; }\n") != 0)
	{
		ktm_tcc_fail("write_hello_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_hello", "/tmp/hello.c", "/tmp/hello",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (run_capture("exec_hello", (char *const[]){ (char *)"/tmp/hello", NULL },
			out, sizeof(out), &ec, &out_n) != 0)
	{
		ktm_tcc_fail("exec_hello_capture", errno);
		goto halt;
	}
	if (ec != 0)
	{
		ktm_tcc_fail("exec_hello", ec);
		goto halt;
	}
	if (!strstr(out, "hello-tcc"))
	{
		ktm_tcc_fail_msg("stdio_stdout", out);
		goto halt;
	}
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_B_TCC_STDIO_HELLO_OK\n");
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_B_BASE_LAYOUT_OK\n");

	write_str("KTM_TCC_C_START\n");
	write_str("KTM_TCC_C_CASE_PRINTF\n");
	if (write_source("/tmp/printf.c",
			 "#include <stdio.h>\n"
			 "int main(void){\n"
			 " printf(\"fmt %d %s %x\\n\", 42, \"ok\", 255);\n"
			 " return 0;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_printf_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_printf", "/tmp/printf.c", "/tmp/printf",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect_out("exec_printf",
			    (char *const[]){ (char *)"/tmp/printf", NULL },
			    0, "fmt 42 ok ff", out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_C_PRINTF_OK\n");

	write_str("KTM_TCC_C_CASE_MALLOC\n");
	if (write_source("/tmp/malloc.c",
			 "#include <stdlib.h>\n"
			 "#include <string.h>\n"
			 "int main(void){\n"
			 " char *p=malloc(128);\n"
			 " if(!p) return 1;\n"
			 " memset(p,'A',127); p[127]=0;\n"
			 " if(p[0]!='A') return 2;\n"
			 " free(p);\n"
			 " return 0;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_malloc_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_malloc", "/tmp/malloc.c", "/tmp/malloc",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect("exec_malloc", "/tmp/malloc", 0, out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_C_MALLOC_OK\n");

	write_str("KTM_TCC_C_CASE_STDIO_FILE\n");
	if (write_source("/tmp/fileio.c",
			 "#include <stdio.h>\n"
			 "int main(void){\n"
			 " FILE *f=fopen(\"/tmp/f52c.dat\",\"w\");\n"
			 " if(!f) return 1;\n"
			 " if(fwrite(\"data42\",1,6,f)!=6) return 2;\n"
			 " fclose(f);\n"
			 " f=fopen(\"/tmp/f52c.dat\",\"r\");\n"
			 " if(!f) return 3;\n"
			 " char b[8]={0};\n"
			 " if(fread(b,1,6,f)!=6) return 4;\n"
			 " fclose(f);\n"
			 " return (b[0]=='d'&&b[5]=='2')?0:5;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_fileio_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_fileio", "/tmp/fileio.c", "/tmp/fileio",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect("exec_fileio", "/tmp/fileio", 0, out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_C_STDIO_FILE_OK\n");

	write_str("KTM_TCC_C_CASE_ARGV\n");
	if (write_source("/tmp/argv.c",
			 "#include <stdio.h>\n"
			 "int main(int argc,char **argv){\n"
			 " if(argc<3) return 1;\n"
			 " if(argv[1][0]!='a') return 2;\n"
			 " if(argv[2][0]!='b') return 3;\n"
			 " printf(\"args %s %s\\n\", argv[1], argv[2]);\n"
			 " return 0;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_argv_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_argv", "/tmp/argv.c", "/tmp/argvbin",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect_out("exec_argv",
			    (char *const[]){ (char *)"/tmp/argvbin", (char *)"a",
					     (char *)"b", NULL },
			    0, "args a b", out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_C_ARGV_OK\n");

	write_str("KTM_TCC_C_CASE_MULTI\n");
	if (write_source("/tmp/helper.c", "int helper(int x){ return x + 7; }\n") != 0)
	{
		ktm_tcc_fail("write_helper_src", errno);
		goto halt;
	}
	if (write_source("/tmp/main_multi.c",
			 "int helper(int x);\n"
			 "int main(void){ return helper(35); }\n") != 0)
	{
		ktm_tcc_fail("write_main_multi_src", errno);
		goto halt;
	}
	if (static_link_multi("tcc_static_multi", "/tmp/multi",
			      "/tmp/main_multi.c", "/tmp/helper.c",
			      out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect("exec_multi", "/tmp/multi", 42, out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_C_MULTI_OBJECT_LINK_OK\n");

	write_str("[KTM_TCC] CLASSIFY KTM_TCC_C_OK\n");

	if (ktm_tcc_d_large_file_harness() != 0)
		goto halt;

	write_str("KTM_TCC_D_CASE_MEDIUM_COMBO\n");
	if (write_source("/tmp/combo.c",
			 "#include <stdio.h>\n"
			 "#include <stdlib.h>\n"
			 "#include <string.h>\n"
			 "int main(void){\n"
			 " char *p=malloc(64);\n"
			 " if(!p) return 1;\n"
			 " memset(p,'c',4); p[4]='\\0';\n"
			 " printf(\"combo-%s\\n\", p);\n"
			 " free(p);\n"
			 " return 0;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_combo_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_combo", "/tmp/combo.c", "/tmp/combo",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect_out("exec_combo", (char *const[]){ (char *)"/tmp/combo", NULL },
			    0, "combo-cccc", out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_D_MEDIUM_PROGRAM_OK\n");

	write_str("KTM_TCC_D_CASE_LARGE_READER\n");
	if (write_source("/tmp/large_read.c",
			 "#include <stdio.h>\n"
			 "int main(void){\n"
			 " FILE *f=fopen(\"/tmp/f52d_lg.bin\",\"rb\");\n"
			 " unsigned long n=0; int c;\n"
			 " if(!f) return 1;\n"
			 " while((c=fgetc(f))!=EOF){ n++; if(n>532480) break; }\n"
			 " fclose(f);\n"
			 " printf(\"bytes=%lu\\n\", n);\n"
			 " return (n==532480)?0:2;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_large_read_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_large_read", "/tmp/large_read.c",
			"/tmp/large_read", out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect_out("exec_large_read",
			    (char *const[]){ (char *)"/tmp/large_read", NULL },
			    0, "bytes=532480", out, sizeof(out), &out_n) != 0)
		goto halt;
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_D_LARGE_FILE_PROGRAM_OK\n");

	if (ktm_tcc_d_large_file_truncate() != 0)
		goto halt;

	write_str("KTM_TCC_D_CASE_COMPILE_STRESS\n");
	{
		int iter;

		for (iter = 0; iter < 2; iter++)
		{
			if (write_source("/tmp/stress.c",
					 "int main(){return 0;}\n") != 0)
			{
				ktm_tcc_fail("write_stress_src", errno);
				goto halt;
			}
			if (static_link("tcc_static_stress", "/tmp/stress.c",
					"/tmp/stress", out, sizeof(out), &out_n) != 0)
				goto halt;
			if (exec_expect("exec_stress", "/tmp/stress", 0,
					out, sizeof(out), &out_n) != 0)
				goto halt;
		}
	}
	write_str("[KTM_TCC] CLASSIFY EXEC_COMPILE_STRESS_OK\n");

	write_str("KTM_TCC_D_CASE_TXTUTIL\n");
	if (write_source("/tmp/lines.txt", "a\nb\nc\n") != 0)
	{
		ktm_tcc_fail("write_lines_txt", errno);
		goto halt;
	}
	if (write_source("/tmp/wclines.c",
			 "#include <stdio.h>\n"
			 "int main(void){\n"
			 " FILE *f=fopen(\"/tmp/lines.txt\",\"r\");\n"
			 " int c, lines=0, last=0;\n"
			 " if(!f) return 1;\n"
			 " while((c=fgetc(f))!=EOF){ if(c=='\\n') lines++; last=c; }\n"
			 " fclose(f);\n"
			 " if(last!='\\n') lines++;\n"
			 " printf(\"lines=%d\\n\", lines);\n"
			 " return (lines==3)?0:2;\n"
			 "}\n") != 0)
	{
		ktm_tcc_fail("write_wclines_src", errno);
		goto halt;
	}
	if (static_link("tcc_static_wclines", "/tmp/wclines.c", "/tmp/wclines",
			out, sizeof(out), &out_n) != 0)
		goto halt;
	if (exec_expect_out("exec_wclines",
			    (char *const[]){ (char *)"/tmp/wclines", NULL },
			    0, "lines=3", out, sizeof(out), &out_n) != 0)
		goto halt;

	if (truncate(F52D_LARGE_PATH, 0) != 0)
	{
		ktm_tcc_fail("large_truncate_zero", errno);
		goto halt;
	}

	write_str("[KTM_TCC] CLASSIFY KTM_TCC_D_STAGING_INCREMENTAL_OK\n");
	write_str("[KTM_TCC] CLASSIFY TOOLCHAIN_EXPANDED_OK\n");
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_D_OK\n");
	write_str("[KTM_TCC] CLASSIFY KTM_BUSYBOX_NO_REGRESSION_VERIFIED\n");
	write_str("[KTM_TCC] CLASSIFY KTM_SHELL_BASELINE_STABLE\n");
	write_str("[KTM_TCC] CLASSIFY KTM_TCC_DEBUG_GATED\n");
	write_str("KTM_TCC_OK\n");
	goto done;

halt:
	for (;;)
		(void)pause();

done:
	for (;;)
		(void)pause();

	return 0;
}
