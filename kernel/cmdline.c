/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cmdline.c
 * Description: Parse Multiboot cmdline for log profile / trace categories.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cmdline.h>
#include <ir0/ktm/klog.h>
#include <ir0/multiboot.h>
#include <config.h>
#include <stddef.h>
#include <stdint.h>

static int g_ash_smoke_cmdline;

static int cmdline_token_eq(const char *s, const char *key)
{
	size_t i;

	if (!s || !key)
		return 0;
	for (i = 0; key[i]; i++)
	{
		if (s[i] != key[i])
			return 0;
	}
	return s[i] == '\0' || s[i] == ' ' || s[i] == '\t' || s[i] == ',';
}

static const char *cmdline_value_after(const char *s, const char *prefix)
{
	size_t i;

	if (!s || !prefix)
		return NULL;
	for (i = 0; prefix[i]; i++)
	{
		if (s[i] != prefix[i])
			return NULL;
	}
	return s + i;
}

static void apply_loglevel_token(const char *val)
{
	if (cmdline_token_eq(val, "quiet"))
		klog_apply_profile(KLOG_PROFILE_QUIET);
	else if (cmdline_token_eq(val, "normal"))
		klog_apply_profile(KLOG_PROFILE_NORMAL);
	else if (cmdline_token_eq(val, "debug"))
		klog_apply_profile(KLOG_PROFILE_DEBUG);
	else if (cmdline_token_eq(val, "trace"))
		klog_apply_profile(KLOG_PROFILE_TRACE);
}

static void apply_trace_list(const char *val)
{
	uint32_t mask = klog_get_trace_mask();
	const char *p = val;

	while (p && *p && *p != ' ' && *p != '\t')
	{
		if (cmdline_token_eq(p, "open_abi") || cmdline_token_eq(p, "all"))
			mask |= KLOG_TRACE_OPEN_ABI;
		if (cmdline_token_eq(p, "vfs") || cmdline_token_eq(p, "vfs_stat") ||
		    cmdline_token_eq(p, "all"))
			mask |= KLOG_TRACE_VFS_STAT;
		while (*p && *p != ',' && *p != ' ' && *p != '\t')
			p++;
		if (*p == ',')
			p++;
	}
	klog_set_trace_mask(mask);
	if (mask && klog_get_level() > KLOG_LEVEL_TRACE)
		klog_set_level(KLOG_LEVEL_TRACE);
}

static void apply_kconfig_default(void)
{
#if defined(CONFIG_LOG_PROFILE_QUIET) && CONFIG_LOG_PROFILE_QUIET
	klog_apply_profile(KLOG_PROFILE_QUIET);
#elif defined(CONFIG_LOG_PROFILE_DEBUG) && CONFIG_LOG_PROFILE_DEBUG
	klog_apply_profile(KLOG_PROFILE_DEBUG);
#elif defined(CONFIG_LOG_PROFILE_TRACE) && CONFIG_LOG_PROFILE_TRACE
	klog_apply_profile(KLOG_PROFILE_TRACE);
#else
	klog_apply_profile(KLOG_PROFILE_NORMAL);
#endif
}

int ir0_cmdline_ash_smoke_enabled(void)
{
	return g_ash_smoke_cmdline;
}

void ir0_cmdline_apply_log_profile(void)
{
	const struct multiboot_info *mb;
	const char *cmdline;
	const char *p;
	const char *val;

	apply_kconfig_default();

	mb = (const struct multiboot_info *)get_boot_params();
	if (!mb || !(mb->flags & MULTIBOOT_FLAG_CMDLINE) || !mb->cmdline)
		return;

	cmdline = (const char *)(uintptr_t)mb->cmdline;
	p = cmdline;
	while (*p)
	{
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		val = cmdline_value_after(p, "ir0.loglevel=");
		if (val)
		{
			apply_loglevel_token(val);
		}
		else if (cmdline_token_eq(p, "ir0.ash_smoke=1"))
		{
			g_ash_smoke_cmdline = 1;
		}
		else
		{
			val = cmdline_value_after(p, "ir0.trace=");
			if (val)
				apply_trace_list(val);
		}
		while (*p && *p != ' ' && *p != '\t')
			p++;
	}
}
