/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * BusyBox probe stub — enough for: busybox sh -c "echo ok"
 */

#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
	if (argc >= 4 && argv[1] && !strcmp(argv[1], "sh") &&
	    argv[2] && !strcmp(argv[2], "-c") && argv[3])
	{
		if (strstr(argv[3], "echo") && strstr(argv[3], "ok"))
		{
			static const char msg[] = "ok\n";

			(void)write(1, msg, sizeof(msg) - 1);
			return 0;
		}
	}

	(void)write(2, "busybox: unsupported\n", 21);
	return 127;
}
