/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal /bin/cat — copy stdin to stdout (musl static).
 */

#include <unistd.h>
#include <errno.h>

int main(void)
{
	char buf[4096];
	ssize_t n;

	while ((n = read(0, buf, sizeof(buf))) > 0)
	{
		ssize_t off = 0;

		while (off < n)
		{
			ssize_t w = write(1, buf + off, (size_t)(n - off));

			if (w < 0)
				return 1;
			off += w;
		}
	}

	return (n < 0) ? 1 : 0;
}
