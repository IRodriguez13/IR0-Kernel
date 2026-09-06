/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal hello-world program for FASE50D.
 */

#include <unistd.h>

int main(void)
{
	static const char msg[] = "hello-world\n";

	(void)write(1, msg, sizeof(msg) - 1);
	return 0;
}
