/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: socket_syscalls.c
 * Description: minimal AF_INET SOCK_DGRAM socket syscalls
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "socket_syscalls.h"
#include "syscalls_glue.h"
#include "io_syscalls.h"
#include <errno.h>
#include <ir0/syscalls_kernel.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <ir0/net.h>
#include <ir0/open_flags.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <ir0/socket.h>
#include <ir0/signals.h>
#include <ir0/arch_cpu.h>
#include <ir0/pipe.h>
#include <ir0/vfs.h>
#include <ir0/memfd.h>
#include <ir0/devfs.h>
#include <ir0/uio.h>
#include <config.h>
#include <string.h>

#define IPPROTO_UDP 17

extern int64_t sys_close(int fd);

static int64_t unix_fill_sockaddr(struct sock_stream *ss, struct sockaddr *addr,
				  socklen_t *addrlen)
{
	struct sockaddr_un sun;
	socklen_t want;
	socklen_t have;
	size_t plen = 0;
	int abs = 0;
	char path[108];

	if (!current_process || !addr || !addrlen)
		return -EFAULT;
	if (copy_from_user(&want, addrlen, sizeof(want)) != 0)
		return -EFAULT;
	if (sock_stream_family(ss) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	memset(path, 0, sizeof(path));
	(void)sock_stream_get_unix_name(ss, path, sizeof(path), &plen, &abs);
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (plen > sizeof(sun.sun_path))
		plen = sizeof(sun.sun_path);
	memcpy(sun.sun_path, path, plen);
	have = (socklen_t)(sizeof(sun.sun_family) + plen);
	if (want > have)
		want = have;
	if (want > 0 && copy_to_user(addr, &sun, want) != 0)
		return -EFAULT;
	if (copy_to_user(addrlen, &have, sizeof(have)) != 0)
		return -EFAULT;
	(void)abs;
	return 0;
}

static size_t sock_strnlen(const char *s, size_t max)
{
	size_t i;

	for (i = 0; i < max; i++)
	{
		if (s[i] == '\0')
			return i;
	}
	return max;
}

static int sock_alloc_fd_flags(void *sock, int is_stream, int type_flags)
{
	fd_entry_t *fd_table;
	int fd;

	if (!sock || !current_process)
		return -EINVAL;

	fd_table = get_process_fd_table();
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!fd_table[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
	{
		if (is_stream)
			sock_stream_release(sock);
		else if (sock_icmp_is(sock))
			sock_icmp_release(sock);
		else
			sock_udp_release(sock);
		return -EMFILE;
	}

	memset(&fd_table[fd], 0, sizeof(fd_table[fd]));
	fd_table[fd].in_use = true;
	fd_table[fd].vfs_file = sock;
	fd_table[fd].is_socket = true;
	if (type_flags & SOCK_CLOEXEC)
		fd_table[fd].fd_flags = FD_CLOEXEC;
	if (type_flags & SOCK_NONBLOCK)
		fd_table[fd].flags = O_NONBLOCK;
	fase48_note_fd_created();
	return fd;
}

static int sock_alloc_fd_any(void *sock, int is_stream)
{
	return sock_alloc_fd_flags(sock, is_stream, 0);
}

static int sock_alloc_fd(struct sock_udp *sock)
{
	return sock_alloc_fd_any(sock, 0);
}

static struct sock_udp *sock_fd_lookup(int fd)
{
	fd_entry_t *fd_table;
	void *p;

	if (!current_process)
		return NULL;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return NULL;
	fd_table = get_process_fd_table();
	if (!fd_table[fd].in_use || !fd_table[fd].is_socket)
		return NULL;
	p = fd_table[fd].vfs_file;
	if (sock_stream_is(p))
		return NULL;
	if (sock_icmp_is(p))
		return NULL;
	return (struct sock_udp *)p;
}

static struct sock_icmp *sock_icmp_fd_lookup(int fd)
{
	fd_entry_t *fd_table;
	void *p;

	if (!current_process)
		return NULL;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return NULL;
	fd_table = get_process_fd_table();
	if (!fd_table[fd].in_use || !fd_table[fd].is_socket)
		return NULL;
	p = fd_table[fd].vfs_file;
	if (!sock_icmp_is(p))
		return NULL;
	return (struct sock_icmp *)p;
}

static struct sock_stream *sock_stream_fd_lookup(int fd)
{
	fd_entry_t *fd_table;
	void *p;

	if (!current_process)
		return NULL;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return NULL;
	fd_table = get_process_fd_table();
	if (!fd_table[fd].in_use || !fd_table[fd].is_socket)
		return NULL;
	p = fd_table[fd].vfs_file;
	if (!sock_stream_is(p))
		return NULL;
	return (struct sock_stream *)p;
}

static int sock_nonblock_fd(int fd, int flags)
{
	fd_entry_t *fd_table;

	if (flags & MSG_DONTWAIT)
		return 1;
	fd_table = get_process_fd_table();
	if (fd_table[fd].flags & O_NONBLOCK)
		return 1;
	return 0;
}

int64_t sys_socket(int domain, int type, int protocol)
{
	int fd;
	int type_flags = type;
	int base = type & SOCK_TYPE_MASK;

#if !CONFIG_ENABLE_NETWORKING
	(void)domain;
	(void)type;
	(void)protocol;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;

	if (base == SOCK_STREAM && (domain == AF_UNIX || domain == AF_INET))
	{
		struct sock_stream *ss;

		if (protocol != 0 && protocol != 6) /* IPPROTO_TCP */
			return -EPROTONOSUPPORT;
		ss = sock_stream_create(domain == AF_UNIX ? IR0_AF_UNIX : IR0_AF_INET);
		if (!ss)
			return -ENOMEM;
		fd = sock_alloc_fd_flags(ss, 1, type_flags);
		return fd < 0 ? fd : fd;
	}

	if (base == SOCK_RAW)
	{
		struct sock_icmp *icmp;

		if (domain != AF_INET)
			return -EAFNOSUPPORT;
		if (protocol != 0 && protocol != IPPROTO_ICMP)
			return -EPROTONOSUPPORT;
		icmp = sock_icmp_create();
		if (!icmp)
			return -ENOMEM;
		fd = sock_alloc_fd_flags(icmp, 0, type_flags);
		return fd;
	}

	if (domain != AF_INET)
		return -EAFNOSUPPORT;
	if (base != SOCK_DGRAM)
		return -EPROTOTYPE;
	if (protocol != 0 && protocol != IPPROTO_UDP)
		return -EPROTONOSUPPORT;

	{
		struct sock_udp *sock = sock_udp_create();

		if (!sock)
			return -ENOMEM;
		fd = sock_alloc_fd_flags(sock, 0, type_flags);
		return fd;
	}
}

int64_t sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	struct sock_stream *ss;
	struct sock_udp *sock;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!addr || addrlen < 2)
		return -EINVAL;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		uint16_t family = 0;

		if (copy_from_user(&family, addr, sizeof(family)) != 0)
			return -EFAULT;
			if (family == AF_UNIX)
			{
				struct sockaddr_un sun;
				size_t plen;
				int abs;

				if (addrlen < sizeof(sun.sun_family) + 1)
					return -EINVAL;
				memset(&sun, 0, sizeof(sun));
				if (copy_from_user(&sun, addr,
						   addrlen < sizeof(sun) ? addrlen : sizeof(sun)) != 0)
					return -EFAULT;
				abs = (sun.sun_path[0] == '\0');
				plen = (size_t)addrlen - sizeof(sun.sun_family);
				if (plen >= sizeof(sun.sun_path))
					plen = sizeof(sun.sun_path) - 1;
				if (!abs)
					plen = sock_strnlen(sun.sun_path, plen);
				return sock_stream_bind_unix_n(ss, sun.sun_path, plen, abs);
			}
		if (family == AF_INET)
		{
			struct sockaddr_in sin;

			if (addrlen < sizeof(sin))
				return -EINVAL;
			if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
				return -EFAULT;
			return sock_stream_bind_inet(ss, ntohs(sin.sin_port));
		}
		return -EAFNOSUPPORT;
	}

	{
		struct sockaddr_in sin;

		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
			return -EFAULT;
		if (sin.sin_family != AF_INET)
			return -EAFNOSUPPORT;
		sock = sock_fd_lookup(fd);
		if (!sock)
			return -ENOTSOCK;
		ret = sock_udp_bind(sock, ntohs(sin.sin_port));
		return ret;
	}
}

ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags,
		   const struct sockaddr *dest_addr, socklen_t addrlen)
{
	struct sock_stream *ss;
	struct sockaddr_in sin;
	struct sock_udp *sock;
	struct sock_icmp *icmp;
	uint8_t *kbuf;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)buf;
	(void)len;
	(void)flags;
	(void)dest_addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!buf)
		return -EINVAL;
	if (validate_userspace_buffer(buf, len) != 0)
		return -EFAULT;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		ssize_t n;

		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buf, len) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		n = sock_stream_send(ss, kbuf, len);
		kfree(kbuf);
		(void)flags;
		(void)dest_addr;
		(void)addrlen;
		return n;
	}

	if (!dest_addr || addrlen < sizeof(struct sockaddr_in))
		return -EINVAL;
	if (copy_from_user(&sin, dest_addr, sizeof(sin)) != 0)
		return -EFAULT;
	if (sin.sin_family != AF_INET)
		return -EAFNOSUPPORT;

	icmp = sock_icmp_fd_lookup(fd);
	if (icmp)
	{
		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buf, len) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		ret = sock_icmp_sendto(icmp, sin.sin_addr, kbuf, len);
		kfree(kbuf);
		if (ret < 0)
			return ret;
		(void)flags;
		return (ssize_t)ret;
	}

	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;

	kbuf = kmalloc(len);
	if (!kbuf)
		return -ENOMEM;
	if (copy_from_user(kbuf, buf, len) != 0)
	{
		kfree(kbuf);
		return -EFAULT;
	}

	ret = sock_udp_sendto(sock, sin.sin_addr, ntohs(sin.sin_port), kbuf, len);
	kfree(kbuf);
	if (ret < 0)
		return ret;
	(void)flags;
	return (ssize_t)ret;
}

ssize_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
		     struct sockaddr *src_addr, socklen_t *addrlen)
{
	struct sock_stream *ss;
	struct sock_udp *sock;
	struct sock_icmp *icmp;
	uint16_t src_port = 0;
	uint8_t *kbuf;
	ssize_t n;
	struct sockaddr_in sin;
	socklen_t out_len = sizeof(sin);
	int nb;
	uint32_t src_ip_be = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)buf;
	(void)len;
	(void)flags;
	(void)src_addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!buf)
		return -EINVAL;
	if (validate_userspace_buffer(buf, len) != 0)
		return -EFAULT;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		nb = sock_nonblock_fd(fd, flags);
		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;
		for (;;)
		{
			n = sock_stream_recv_flags(ss, kbuf, len, flags);
			if (n != -EAGAIN)
				break;
			if (nb)
			{
				kfree(kbuf);
				return -EAGAIN;
			}
			if (signals_pause_should_interrupt(current_process))
			{
				kfree(kbuf);
				return -EINTR;
			}
			{
				int64_t sleep_ret = syscall_sleep_ms_locked(20);

				if (sleep_ret < 0)
				{
					kfree(kbuf);
					return sleep_ret;
				}
			}
			ss = sock_stream_fd_lookup(fd);
			if (!ss)
			{
				kfree(kbuf);
				return -EBADF;
			}
		}
		if (n < 0)
		{
			kfree(kbuf);
			return n;
		}
		if (n > 0 && copy_to_user(buf, kbuf, (size_t)n) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);
		(void)src_addr;
		(void)addrlen;
		return n;
	}

	if (addrlen)
	{
		if (validate_userspace_buffer(addrlen, sizeof(socklen_t)) != 0)
			return -EFAULT;
		if (copy_from_user(&out_len, addrlen, sizeof(socklen_t)) != 0)
			return -EFAULT;
	}
	if (src_addr && out_len > 0)
	{
		if (validate_userspace_buffer(src_addr, out_len) != 0)
			return -EFAULT;
	}

	icmp = sock_icmp_fd_lookup(fd);
	if (icmp)
	{
		nb = sock_nonblock_fd(fd, flags);
		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;

		for (;;)
		{
			n = sock_icmp_recvfrom(icmp, kbuf, len, MSG_DONTWAIT,
					       &src_ip_be);
			if (n != -EAGAIN)
				break;
			if (nb)
			{
				kfree(kbuf);
				return -EAGAIN;
			}
			if (signals_pause_should_interrupt(current_process))
			{
				handle_signals();
				kfree(kbuf);
				/*
				 * Sysret would return to the insn after recvfrom and
				 * ignore task.RIP. If a userspace handler was armed,
				 * iretq into it (BusyBox ping SIGALRM → sendping4).
				 */
				if (current_process->saved_context)
				{
					process_restore_user_task_segments(current_process);
					switch_to_user_task(&current_process->task);
				}
				return -EINTR;
			}
			{
				int64_t sleep_ret = syscall_sleep_ms_locked(20);

				if (sleep_ret < 0)
				{
					kfree(kbuf);
					return sleep_ret;
				}
			}
			if (signals_pause_should_interrupt(current_process))
			{
				handle_signals();
				kfree(kbuf);
				if (current_process->saved_context)
				{
					process_restore_user_task_segments(current_process);
					switch_to_user_task(&current_process->task);
				}
				return -EINTR;
			}
			icmp = sock_icmp_fd_lookup(fd);
			if (!icmp)
			{
				kfree(kbuf);
				return -EBADF;
			}
		}
		if (n < 0)
		{
			kfree(kbuf);
			return n;
		}
		if (copy_to_user(buf, kbuf, (size_t)n) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);

		if (src_addr && out_len >= sizeof(sin))
		{
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_addr = src_ip_be;
			if (copy_to_user(src_addr, &sin, sizeof(sin)) != 0)
				return -EFAULT;
			if (addrlen)
			{
				socklen_t slen = sizeof(sin);

				if (copy_to_user(addrlen, &slen, sizeof(slen)) != 0)
					return -EFAULT;
			}
		}
		return n;
	}

	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;

	nb = sock_nonblock_fd(fd, flags);
	kbuf = kmalloc(len);
	if (!kbuf)
		return -ENOMEM;

	n = sock_udp_recvfrom(sock, kbuf, len, nb ? MSG_DONTWAIT : 0,
			      &src_ip_be, &src_port);
	if (n < 0)
	{
		kfree(kbuf);
		return n;
	}
	if (copy_to_user(buf, kbuf, (size_t)n) != 0)
	{
		kfree(kbuf);
		return -EFAULT;
	}
	kfree(kbuf);

	if (src_addr && out_len >= sizeof(sin))
	{
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(src_port);
		sin.sin_addr = src_ip_be;
		if (copy_to_user(src_addr, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		if (addrlen)
		{
			socklen_t slen = sizeof(sin);

			if (copy_to_user(addrlen, &slen, sizeof(slen)) != 0)
				return -EFAULT;
		}
	}
	return n;
}

int64_t sys_connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	struct sock_stream *ss;
	struct sock_udp *sock;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!addr || addrlen < 2)
		return -EINVAL;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		uint16_t family = 0;

		if (copy_from_user(&family, addr, sizeof(family)) != 0)
			return -EFAULT;
			if (family == AF_UNIX)
			{
				struct sockaddr_un sun;
				size_t plen;
				int abs;

				memset(&sun, 0, sizeof(sun));
				if (copy_from_user(&sun, addr,
						   addrlen < sizeof(sun) ? addrlen : sizeof(sun)) != 0)
					return -EFAULT;
				abs = (sun.sun_path[0] == '\0');
				plen = (size_t)addrlen - sizeof(sun.sun_family);
				if (plen >= sizeof(sun.sun_path))
					plen = sizeof(sun.sun_path) - 1;
				if (!abs)
					plen = sock_strnlen(sun.sun_path, plen);
				return sock_stream_connect_unix_n(ss, sun.sun_path, plen, abs);
			}
		if (family == AF_INET)
		{
			struct sockaddr_in sin;

			if (addrlen < sizeof(sin))
				return -EINVAL;
			if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
				return -EFAULT;
			return sock_stream_connect_inet(ss, sin.sin_addr, ntohs(sin.sin_port));
		}
		return -EAFNOSUPPORT;
	}

	{
		struct sockaddr_in sin;

		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
			return -EFAULT;
		if (sin.sin_family != AF_INET)
			return -EAFNOSUPPORT;
		sock = sock_fd_lookup(fd);
		if (!sock)
			return -ENOTSOCK;
		ret = sock_udp_connect(sock, sin.sin_addr, ntohs(sin.sin_port));
		return ret;
	}
}

int64_t sys_listen(int fd, int backlog)
{
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)backlog;
	return -ENOSYS;
#endif
	if (!current_process)
		return -ESRCH;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	return sock_stream_listen(ss, backlog);
}

int64_t sys_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	return sys_accept4(fd, addr, addrlen, 0);
}

int64_t sys_accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	struct sock_stream *ss;
	struct sock_stream *child;
	int nfd;

	(void)addr;
	(void)addrlen;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)flags;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;

	ss = sock_stream_fd_lookup(fd);
	if (!ss)
	{
		if (!sock_fd_lookup(fd))
			return -ENOTSOCK;
		return -EOPNOTSUPP;
	}

	/*
	 * Linux accept(2) blocks until a connection is pending unless the
	 * listener is O_NONBLOCK / accept4(..., SOCK_NONBLOCK). IR0 previously
	 * returned -EAGAIN immediately (same-process smokes connect-before-accept).
	 * X11 servers need the blocking path across fork/exec.
	 */
	for (;;)
	{
		child = sock_stream_accept(ss);
		if (child)
			break;
		if (sock_nonblock_fd(fd, 0) || (flags & SOCK_NONBLOCK))
			return -EAGAIN;
		if (signals_pause_should_interrupt(current_process))
			return -EINTR;
#if CONFIG_ENABLE_NETWORKING
		net_stack_poll();
#endif
		{
			int64_t sleep_ret = syscall_sleep_ms_locked(20);

			if (sleep_ret < 0)
				return sleep_ret;
		}
#if CONFIG_ENABLE_NETWORKING
		net_stack_poll();
#endif
		ss = sock_stream_fd_lookup(fd);
		if (!ss)
			return -EBADF;
	}
	nfd = sock_alloc_fd_flags(child, 1, flags);
	return nfd;
}

int64_t sys_socketpair(int domain, int type, int protocol, int *sv)
{
	struct sock_stream *a;
	struct sock_stream *b;
	int fd0;
	int fd1;
	int sv_k[2];
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)domain;
	(void)type;
	(void)protocol;
	(void)sv;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!sv)
		return -EFAULT;
	if (domain != AF_UNIX)
		return -EAFNOSUPPORT;
	if ((type & SOCK_TYPE_MASK) != SOCK_STREAM)
		return -EPROTOTYPE;
	if (protocol != 0)
		return -EPROTONOSUPPORT;

	ret = sock_stream_socketpair(&a, &b);
	if (ret < 0)
		return ret;

	fd0 = sock_alloc_fd_flags(a, 1, type);
	if (fd0 < 0)
	{
		sock_stream_release(b);
		return fd0;
	}
	fd1 = sock_alloc_fd_flags(b, 1, type);
	if (fd1 < 0)
	{
		(void)sys_close(fd0);
		return fd1;
	}

	sv_k[0] = fd0;
	sv_k[1] = fd1;
	if (copy_to_user(sv, sv_k, sizeof(sv_k)) != 0)
	{
		(void)sys_close(fd0);
		(void)sys_close(fd1);
		return -EFAULT;
	}
	return 0;
}

static void scm_rights_dtor(void *entry, size_t sz)
{
	fd_entry_t *e = (fd_entry_t *)entry;

	(void)sz;
	if (!e || !e->in_use)
		return;
	if (e->is_pipe && e->vfs_file)
		pipe_close_end((pipe_t *)e->vfs_file, e->pipe_end);
	else if (e->is_socket && e->vfs_file)
	{
		if (sock_stream_is(e->vfs_file))
			sock_stream_release((struct sock_stream *)e->vfs_file);
		else if (sock_icmp_is(e->vfs_file))
			sock_icmp_release((struct sock_icmp *)e->vfs_file);
		else if (!sock_stream_is_slot(e->vfs_file))
			sock_udp_release((struct sock_udp *)e->vfs_file);
	}
	else if (e->is_devfs)
	{
		devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

		if (e->vfs_file &&
		    devfs_node_wants_text_snap(e->dev_device_id))
		{
			devfs_text_snap_release((devfs_text_snap_t *)e->vfs_file);
			e->vfs_file = NULL;
		}
		if (node)
			devfs_close_node(node);
	}
	else if (e->is_memfd && e->vfs_file)
		ir0_memfd_release((struct ir0_memfd *)e->vfs_file);
	else if (e->vfs_file)
		vfs_close((struct vfs_file *)e->vfs_file);
	memset(e, 0, sizeof(*e));
}

static void scm_rights_ensure_dtor(void)
{
	static int once;

	if (!once)
	{
		sock_stream_set_rights_dtor(scm_rights_dtor);
		once = 1;
	}
}

static int scm_clone_fd_entry(fd_entry_t *dst, int srcfd)
{
	fd_entry_t *tab = get_process_fd_table();

	if (!tab || srcfd < 0 || srcfd >= MAX_FDS_PER_PROCESS || !tab[srcfd].in_use)
		return -EBADF;
	*dst = tab[srcfd];
	dst->fd_flags = 0;
	if (dst->is_pipe && dst->vfs_file)
		pipe_acquire_end((pipe_t *)dst->vfs_file, dst->pipe_end);
	else if (dst->is_socket && dst->vfs_file)
	{
		if (sock_stream_is(dst->vfs_file))
			sock_stream_acquire((struct sock_stream *)dst->vfs_file);
		else if (sock_icmp_is(dst->vfs_file))
			sock_icmp_acquire((struct sock_icmp *)dst->vfs_file);
		else if (!sock_stream_is_slot(dst->vfs_file))
			sock_udp_acquire((struct sock_udp *)dst->vfs_file);
	}
	else if (dst->is_devfs)
	{
		devfs_node_t *node = devfs_find_node_by_id(dst->dev_device_id);

		if (node)
			node->ref_count++;
		if (dst->vfs_file &&
		    devfs_node_wants_text_snap(dst->dev_device_id))
			devfs_text_snap_acquire((devfs_text_snap_t *)dst->vfs_file);
	}
	else if (dst->is_pseudo && dst->vfs_file)
	{
		pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)dst->vfs_file;

		bind->refs++;
	}
	else if (dst->is_memfd && dst->vfs_file)
		ir0_memfd_acquire((struct ir0_memfd *)dst->vfs_file);
	else if (dst->vfs_file)
		vfs_file_acquire((struct vfs_file *)dst->vfs_file);
	return 0;
}

static int scm_install_fd_entry(const fd_entry_t *src)
{
	fd_entry_t *tab = get_process_fd_table();
	int fd;

	if (!tab || !src)
		return -EINVAL;
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!tab[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
		return -EMFILE;
	tab[fd] = *src;
	tab[fd].in_use = true;
	fase48_note_fd_created();
	return fd;
}

ssize_t sys_sendmsg(int fd, const struct msghdr *umsg, int flags)
{
	struct msghdr msg;
	struct sock_stream *ss;
	struct sock_stream *peer;
	uint8_t ctrl[256];
	struct iovec iov_stack[8];
	size_t iovlen;
	size_t i;
	ssize_t total = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)umsg;
	(void)flags;
	return -ENOSYS;
#endif
	scm_rights_ensure_dtor();
	(void)flags;
	if (!current_process || !umsg)
		return -EFAULT;
	if (copy_from_user(&msg, umsg, sizeof(msg)) != 0)
		return -EFAULT;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (sock_stream_family(ss) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	peer = sock_stream_get_peer(ss);
	if (!peer)
		return -ENOTCONN;
	iovlen = msg.msg_iovlen;
	if (iovlen > 8)
		return -EMSGSIZE;
	if (iovlen > 0)
	{
		if (!msg.msg_iov ||
		    copy_from_user(iov_stack, msg.msg_iov, iovlen * sizeof(struct iovec)) != 0)
			return -EFAULT;
	}
	if (msg.msg_controllen > 0)
	{
		struct cmsghdr *cmsg;
		size_t off = 0;

		if (msg.msg_controllen > sizeof(ctrl) || !msg.msg_control)
			return -ENOBUFS;
		if (copy_from_user(ctrl, msg.msg_control, msg.msg_controllen) != 0)
			return -EFAULT;
		while (off + sizeof(struct cmsghdr) <= msg.msg_controllen)
		{
			size_t payload;
			int *fds;
			size_t nfd;
			size_t fi;

			cmsg = (struct cmsghdr *)(ctrl + off);
			if (cmsg->cmsg_len < sizeof(struct cmsghdr) ||
			    cmsg->cmsg_len > msg.msg_controllen - off)
				break;
			if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
			{
				payload = cmsg->cmsg_len - IR0_CMSG_ALIGN(sizeof(struct cmsghdr));
				fds = (int *)IR0_CMSG_DATA(cmsg);
				nfd = payload / sizeof(int);
				if (nfd == 0 || nfd > SOCK_STREAM_RIGHTS_MAX)
					return -EINVAL;
				for (fi = 0; fi < nfd; fi++)
				{
					fd_entry_t ent;
					int ret;

					memset(&ent, 0, sizeof(ent));
					ret = scm_clone_fd_entry(&ent, fds[fi]);
					if (ret < 0)
						return ret;
					ret = sock_stream_rights_push(peer, &ent, sizeof(ent));
					if (ret < 0)
					{
						scm_rights_dtor(&ent, sizeof(ent));
						return ret;
					}
				}
			}
			off += IR0_CMSG_ALIGN(cmsg->cmsg_len);
		}
	}
	for (i = 0; i < iovlen; i++)
	{
		uint8_t *kbuf;
		ssize_t n;

		if (!iov_stack[i].iov_base || iov_stack[i].iov_len == 0)
			continue;
		if (validate_userspace_buffer(iov_stack[i].iov_base, iov_stack[i].iov_len) != 0)
			return -EFAULT;
		kbuf = kmalloc(iov_stack[i].iov_len);
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, iov_stack[i].iov_base, iov_stack[i].iov_len) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		n = sock_stream_send(ss, kbuf, iov_stack[i].iov_len);
		kfree(kbuf);
		if (n < 0)
			return n;
		total += n;
		if ((size_t)n < iov_stack[i].iov_len)
			break;
	}
	if (total == 0 && msg.msg_controllen > 0)
		return 0;
	return total;
}

ssize_t sys_recvmsg(int fd, struct msghdr *umsg, int flags)
{
	struct msghdr msg;
	struct sock_stream *ss;
	struct iovec iov_stack[8];
	size_t iovlen;
	size_t i;
	ssize_t total = 0;
	uint8_t ctrl[256];
	size_t ctrl_used = 0;
	int got_rights = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)umsg;
	(void)flags;
	return -ENOSYS;
#endif
	scm_rights_ensure_dtor();
	(void)flags;
	if (!current_process || !umsg)
		return -EFAULT;
	if (copy_from_user(&msg, umsg, sizeof(msg)) != 0)
		return -EFAULT;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (sock_stream_family(ss) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	iovlen = msg.msg_iovlen;
	if (iovlen > 8)
		return -EMSGSIZE;
	if (iovlen > 0)
	{
		if (!msg.msg_iov ||
		    copy_from_user(iov_stack, msg.msg_iov, iovlen * sizeof(struct iovec)) != 0)
			return -EFAULT;
	}
	if (sock_stream_rights_count(ss) > 0 && msg.msg_control && msg.msg_controllen > 0)
	{
		fd_entry_t ents[SOCK_STREAM_RIGHTS_MAX];
		int newfds[SOCK_STREAM_RIGHTS_MAX];
		size_t n_pop = 0;
		size_t pending = (size_t)sock_stream_rights_count(ss);
		size_t max_fit;
		size_t fi;
		struct cmsghdr *cmsg;
		int *fdp;
		size_t need;

		if (pending > SOCK_STREAM_RIGHTS_MAX)
			pending = SOCK_STREAM_RIGHTS_MAX;
		max_fit = (msg.msg_controllen >= IR0_CMSG_SPACE(sizeof(int)))
			? ((msg.msg_controllen - IR0_CMSG_ALIGN(sizeof(struct cmsghdr))) /
			   sizeof(int))
			: 0;
		if (max_fit > SOCK_STREAM_RIGHTS_MAX)
			max_fit = SOCK_STREAM_RIGHTS_MAX;
		if (pending > max_fit)
			pending = max_fit;
		if (pending == 0)
			return -ENOBUFS;

		for (fi = 0; fi < pending; fi++)
		{
			memset(&ents[fi], 0, sizeof(ents[fi]));
			if (sock_stream_rights_pop(ss, &ents[fi], sizeof(ents[fi])) != 0)
				break;
			newfds[fi] = scm_install_fd_entry(&ents[fi]);
			if (newfds[fi] < 0)
			{
				int err = newfds[fi];

				scm_rights_dtor(&ents[fi], sizeof(ents[fi]));
				while (fi > 0)
				{
					fi--;
					(void)sys_close(newfds[fi]);
				}
				return err;
			}
			n_pop++;
		}
		if (n_pop == 0)
			goto recv_payload;

		need = IR0_CMSG_SPACE(n_pop * sizeof(int));
		if (need > sizeof(ctrl) || need > msg.msg_controllen)
		{
			while (n_pop > 0)
			{
				n_pop--;
				(void)sys_close(newfds[n_pop]);
			}
			return -ENOBUFS;
		}
		memset(ctrl, 0, need);
		cmsg = (struct cmsghdr *)ctrl;
		cmsg->cmsg_len = IR0_CMSG_LEN(n_pop * sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		fdp = (int *)IR0_CMSG_DATA(cmsg);
		for (fi = 0; fi < n_pop; fi++)
			fdp[fi] = newfds[fi];
		ctrl_used = need;
		got_rights = 1;
	}
recv_payload:
	for (i = 0; i < iovlen; i++)
	{
		uint8_t *kbuf;
		ssize_t n;

		if (!iov_stack[i].iov_base || iov_stack[i].iov_len == 0)
			continue;
		if (validate_userspace_buffer(iov_stack[i].iov_base, iov_stack[i].iov_len) != 0)
			return -EFAULT;
		kbuf = kmalloc(iov_stack[i].iov_len);
		if (!kbuf)
			return -ENOMEM;
		n = sock_stream_recv_flags(ss, kbuf, iov_stack[i].iov_len, flags);
		if (n < 0)
		{
			kfree(kbuf);
			return n;
		}
		if (n > 0 &&
		    copy_to_user(iov_stack[i].iov_base, kbuf, (size_t)n) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);
		total += n;
		if ((size_t)n < iov_stack[i].iov_len)
			break;
	}
	if (got_rights && msg.msg_control)
	{
		if (copy_to_user(msg.msg_control, ctrl, ctrl_used) != 0)
			return -EFAULT;
		msg.msg_controllen = ctrl_used;
		msg.msg_flags = 0;
		if (copy_to_user(umsg, &msg, sizeof(msg)) != 0)
			return -EFAULT;
	}
	else if (msg.msg_control)
	{
		msg.msg_controllen = 0;
		if (copy_to_user(umsg, &msg, sizeof(msg)) != 0)
			return -EFAULT;
	}
	(void)got_rights;
	return total;
}

int64_t sys_shutdown(int fd, int how)
{
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)how;
	return -ENOSYS;
#endif
	if (!current_process)
		return -ESRCH;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	return sock_stream_shutdown(ss, how);
}

int64_t sys_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	return unix_fill_sockaddr(ss, addr, addrlen);
}

int64_t sys_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	struct sock_stream *ss;
	struct sock_stream *peer;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	peer = sock_stream_get_peer(ss);
	if (!peer)
		return -ENOTCONN;
	return unix_fill_sockaddr(peer, addr, addrlen);
}

int64_t sys_setsockopt(int fd, int level, int optname, const void *optval,
		       socklen_t optlen)
{
	struct sock_stream *ss;
	int val = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)level;
	(void)optname;
	(void)optval;
	(void)optlen;
	return -ENOSYS;
#endif
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (level != SOL_SOCKET)
		return -ENOPROTOOPT;
	if (optname == SO_REUSEADDR)
	{
		if (optlen < sizeof(int) || !optval)
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)) != 0)
			return -EFAULT;
		return sock_stream_set_reuseaddr(ss, val);
	}
	return -ENOPROTOOPT;
}

int64_t sys_getsockopt(int fd, int level, int optname, void *optval,
		       socklen_t *optlen)
{
	struct sock_stream *ss;
	socklen_t len;
	int val;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)level;
	(void)optname;
	(void)optval;
	(void)optlen;
	return -ENOSYS;
#endif
	if (!optval || !optlen)
		return -EFAULT;
	if (copy_from_user(&len, optlen, sizeof(len)) != 0)
		return -EFAULT;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (level != SOL_SOCKET)
		return -ENOPROTOOPT;
	if (optname == SO_TYPE)
	{
		val = SOCK_STREAM;
		if (len < sizeof(val))
			return -EINVAL;
		if (copy_to_user(optval, &val, sizeof(val)) != 0)
			return -EFAULT;
		len = sizeof(val);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	if (optname == SO_ERROR)
	{
		val = 0;
		if (len < sizeof(val))
			return -EINVAL;
		if (copy_to_user(optval, &val, sizeof(val)) != 0)
			return -EFAULT;
		len = sizeof(val);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	if (optname == SO_REUSEADDR)
	{
		val = sock_stream_get_reuseaddr(ss);
		if (len < sizeof(val))
			return -EINVAL;
		if (copy_to_user(optval, &val, sizeof(val)) != 0)
			return -EFAULT;
		len = sizeof(val);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	return -ENOPROTOOPT;
}
