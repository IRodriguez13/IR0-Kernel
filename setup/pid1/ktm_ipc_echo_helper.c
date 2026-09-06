/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal /bin/echo — print arguments (musl static).
 */

#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
	{
		if (i > 1)
			(void)write(1, " ", 1);
		if (argv[i])
			(void)write(1, argv[i], strlen(argv[i]));
	}
	(void)write(1, "\n", 1);
	return 0;
}
