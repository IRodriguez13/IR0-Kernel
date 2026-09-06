/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58C — Hold after kernel boot direct draw (no userspace FB writes).
 */

#include <unistd.h>

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	write_str("KTM_BOOT_HALT_TAG\n");
	write_str("KTM_BOOT_HALT_GUI_HOLD\n");
	write_str("KTM_BOOT_HALT_OK\n");

	for (;;)
		(void)pause();

	return 0;
}
