/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_usercopy_contract.c
 * Description: Host test: signals/uname must not raw-touch user VAs
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, char **out, size_t *out_len)
{
	FILE *f;
	long sz;
	char *buf;
	size_t n;

	f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return -1;
	}
	rewind(f);
	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	n = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[n] = '\0';
	*out = buf;
	*out_len = n;
	return 0;
}

static const char *find_sys_uname_body(const char *src, size_t *body_len)
{
	const char *p;
	const char *brace;
	int depth;
	const char *start;

	p = strstr(src, "sys_uname");
	if (!p)
		return NULL;
	/* Prefer the definition: int64_t sys_uname( */
	while (p) {
		const char *line = p;
		while (line > src && line[-1] != '\n')
			line--;
		if (strstr(line, "int64_t") && strchr(p, '('))
			break;
		p = strstr(p + 1, "sys_uname");
	}
	if (!p)
		return NULL;
	brace = strchr(p, '{');
	if (!brace)
		return NULL;
	start = brace + 1;
	depth = 1;
	p = start;
	while (*p && depth > 0) {
		if (*p == '{')
			depth++;
		else if (*p == '}')
			depth--;
		p++;
	}
	*body_len = (size_t)(p - start);
	return start;
}

void test_usercopy_no_raw_user_touch(void)
{
	char *sig = NULL;
	char *uname = NULL;
	size_t sig_len = 0;
	size_t uname_len = 0;
	const char *body;
	size_t body_len = 0;
	char *body_copy = NULL;

	TEST_BEGIN("usercopy_no_raw_user_touch");

	ASSERT_EQ(read_file("../../kernel/lib/signals.c", &sig, &sig_len), 0);
	ASSERT(strstr(sig, "memcpy((void *)") == NULL);
	ASSERT(strstr(sig, "copy_to_user_region_in_directory") != NULL);

	ASSERT_EQ(read_file("../../kernel/syscalls/fs_path_syscalls.c", &uname,
			    &uname_len),
		  0);
	body = find_sys_uname_body(uname, &body_len);
	ASSERT(body != NULL);
	body_copy = malloc(body_len + 1);
	ASSERT(body_copy != NULL);
	memcpy(body_copy, body, body_len);
	body_copy[body_len] = '\0';

	ASSERT(strstr(body_copy, "copy_to_user") != NULL);
	ASSERT(strstr(body_copy, "memset(buf") == NULL);
	ASSERT(strstr(body_copy, "strncpy(buf->") == NULL);

	free(body_copy);
	free(sig);
	free(uname);
	TEST_END();
}
