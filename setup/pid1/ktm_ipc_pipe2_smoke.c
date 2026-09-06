/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal pipe2-only probe */
#include <unistd.h>
#include <fcntl.h>

static void write_str(const char *s)
{
	const char *p = s;
	while (*p) p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	int p[2];

	if (pipe2(p, 0) < 0)
	{
		write_str("FASE48_PIPE2_FAIL\n");
		return 1;
	}
	close(p[0]);
	close(p[1]);
	write_str("FASE48_PIPE2_OK\n");
	for (;;)
		(void)pause();
	return 0;
}
