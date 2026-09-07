/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: min_link_stubs.c
 * Description: Weak stubs so MEMORY_OBJS (+ sample) can link into kernel-arm64-min.bin.
 *              Not product ALL_OBJS — x86 drivers remain excluded.
 */

#include <stddef.h>
#include <stdint.h>

#include <arch/common/arch_portable.h>

/* From freestanding_stubs / serial when linked with boot image. */
extern void panic(const char *msg);
extern void serial_print(const char *s);

void __attribute__((weak)) panic(const char *msg)
{
	(void)msg;
	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

void __attribute__((weak)) panicex(const char *msg)
{
	panic(msg ? msg : "panicex");
}

void __attribute__((weak)) serial_print_hex64(uint64_t v)
{
	(void)v;
}

void __attribute__((weak)) klog_print(const char *message)
{
	serial_print(message);
}

int __attribute__((weak)) klog_trace_enabled(uint32_t category)
{
	(void)category;
	return 0;
}

void __attribute__((weak)) klog_trace(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void __attribute__((weak)) klog_trace_fmt(const char *component,
					  const char *format, ...)
{
	(void)component;
	(void)format;
}

void *__attribute__((weak)) current_process;

void *__attribute__((weak)) process_find_by_pid(int pid)
{
	(void)pid;
	return NULL;
}

int __attribute__((weak)) process_get_pid(void)
{
	return 1;
}

int __attribute__((weak)) ktm_fault_should_fail(void)
{
	return 0;
}

int __attribute__((weak)) video_backend_is_available(void)
{
	return 0;
}

uint64_t __attribute__((weak)) video_backend_get_fb_phys(void)
{
	return 0;
}

uint64_t __attribute__((weak)) video_backend_get_fb_size(void)
{
	return 0;
}

int __attribute__((weak)) ahci_map_mmio_in_directory(void *dir, uint64_t pa, uint64_t size)
{
	(void)dir;
	(void)pa;
	(void)size;
	return -1;
}

void *__attribute__((weak)) alloc(size_t n)
{
	(void)n;
	return NULL;
}

void __attribute__((weak)) alloc_free(void *p)
{
	(void)p;
}

void __attribute__((weak)) alloc_init(void)
{
}

void *__attribute__((weak)) all_realloc(void *p, size_t n)
{
	(void)p;
	(void)n;
	return NULL;
}

void *__attribute__((weak)) kmalloc_aligned_try(size_t n, size_t align)
{
	(void)n;
	(void)align;
	return NULL;
}

void *__attribute__((weak)) __kmalloc_checked(size_t n, const char *f, int l)
{
	(void)n;
	(void)f;
	(void)l;
	return NULL;
}

void __attribute__((weak)) __kfree_aligned_checked(void *p, const char *f, int l)
{
	(void)p;
	(void)f;
	(void)l;
}

void *__attribute__((weak)) memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *__attribute__((weak)) memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;

	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

/* PMM / paging cross-calls satisfied within MEMORY_OBJS when all four link. */
