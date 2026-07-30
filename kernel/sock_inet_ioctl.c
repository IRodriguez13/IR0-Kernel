/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_inet_ioctl.c
 * Description: Linux-compatible SIOCGIF and SIOCSIF plus route ioctls for BusyBox.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_inet_ioctl.h>
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/net.h>
#include <string.h>

#define IR0_IFNAMSIZ 16
#define IR0_AF_INET 2
#define IR0_ARPHRD_ETHER 1

/* Linux x86-64 ifreq is 40 bytes (IFNAMSIZ + 24-byte union). */
struct ir0_sockaddr
{
	uint16_t sa_family;
	char sa_data[14];
};

struct ir0_ifreq
{
	char ifr_name[IR0_IFNAMSIZ];
	union
	{
		struct ir0_sockaddr addr;
		short flags;
		int mtu;
		int ivalue;
		char pad[24];
	} ifr_ifru;
};

struct ir0_ifconf
{
	int ifc_len;
	union
	{
		char *ifcu_buf;
		struct ir0_ifreq *ifcu_req;
	} ifc_ifcu;
};

/*
 * Linux x86-64 struct rtentry (120 bytes). Offsets verified against glibc
 * <net/route.h>: dst=8 gw=24 genmask=40 flags=56.
 */
struct ir0_rtentry
{
	unsigned long rt_pad1;
	struct ir0_sockaddr rt_dst;
	struct ir0_sockaddr rt_gateway;
	struct ir0_sockaddr rt_genmask;
	unsigned short rt_flags;
	short rt_pad2;
	unsigned long rt_pad3;
	void *rt_pad4;
	short rt_metric;
	char *rt_dev;
	unsigned long rt_mtu;
	unsigned long rt_window;
	unsigned short rt_irtt;
};

#if CONFIG_ENABLE_NETWORKING

static ip4_addr_t g_ip_broadcast;
static int g_ip_broadcast_set;

static struct net_device *sock_find_dev(const char *name)
{
	struct net_device *dev;
	char nbuf[IR0_IFNAMSIZ + 1];
	size_t i;

	if (!name)
		return NULL;

	for (i = 0; i < IR0_IFNAMSIZ && name[i]; i++)
		nbuf[i] = name[i];
	nbuf[i] = '\0';

	dev = net_get_devices();
	while (dev)
	{
		if (dev->name && strcmp(dev->name, nbuf) == 0)
			return dev;
		dev = dev->next;
	}
	return NULL;
}

static void sock_fill_sockaddr_in(struct ir0_sockaddr *sa, ip4_addr_t ip_be)
{
	memset(sa, 0, sizeof(*sa));
	sa->sa_family = IR0_AF_INET;
	memcpy(sa->sa_data + 2, &ip_be, sizeof(ip_be));
}

static ip4_addr_t sock_sockaddr_in_addr(const struct ir0_sockaddr *sa)
{
	ip4_addr_t ip = 0;

	if (!sa)
		return 0;
	memcpy(&ip, sa->sa_data + 2, sizeof(ip));
	return ip;
}

static ip4_addr_t sock_dev_ip(struct net_device *dev)
{
	(void)dev;
	return ip_local_addr;
}

static ip4_addr_t sock_dev_netmask(struct net_device *dev)
{
	(void)dev;
	return ip_netmask;
}

static ip4_addr_t sock_dev_bcast(struct net_device *dev)
{
	ip4_addr_t ip;
	ip4_addr_t nm;

	if (g_ip_broadcast_set)
		return g_ip_broadcast;
	ip = sock_dev_ip(dev);
	nm = sock_dev_netmask(dev);
	return ip | ~nm;
}

static void sock_apply_iface_ip(struct net_device *dev)
{
	if (!dev)
		return;
	arp_set_my_ip(ip_local_addr);
	(void)arp_set_interface_ip(dev, ip_local_addr);
	(void)ip_routes_seed_from_globals();
}

static int sock_siocgifconf(void *arg)
{
	struct ir0_ifconf ifc;
	struct net_device *dev;
	char *ubuf;
	int need = 0;
	int written = 0;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&ifc, arg, sizeof(ifc)) != 0)
		return -EFAULT;

	dev = net_get_devices();
	while (dev)
	{
		need += (int)sizeof(struct ir0_ifreq);
		dev = dev->next;
	}

	ubuf = ifc.ifc_ifcu.ifcu_buf;
	if (!ubuf || ifc.ifc_len <= 0)
	{
		ifc.ifc_len = need;
		if (copy_to_user(arg, &ifc, sizeof(ifc)) != 0)
			return -EFAULT;
		return 0;
	}

	dev = net_get_devices();
	while (dev && written + (int)sizeof(struct ir0_ifreq) <= ifc.ifc_len)
	{
		struct ir0_ifreq ifr;

		memset(&ifr, 0, sizeof(ifr));
		if (dev->name)
		{
			size_t n = strlen(dev->name);

			if (n >= IR0_IFNAMSIZ)
				n = IR0_IFNAMSIZ - 1;
			memcpy(ifr.ifr_name, dev->name, n);
		}
		sock_fill_sockaddr_in(&ifr.ifr_ifru.addr, sock_dev_ip(dev));
		if (copy_to_user(ubuf + written, &ifr, sizeof(ifr)) != 0)
			return -EFAULT;
		written += (int)sizeof(ifr);
		dev = dev->next;
	}

	ifc.ifc_len = written;
	if (copy_to_user(arg, &ifc, sizeof(ifc)) != 0)
		return -EFAULT;
	return 0;
}

static int g_if_metric;
static int g_if_txqlen = 1000;

static int sock_siocgifname(void *arg)
{
	struct ir0_ifreq ifr;
	struct net_device *dev;
	int want;
	int idx;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&ifr, arg, sizeof(ifr)) != 0)
		return -EFAULT;

	want = ifr.ifr_ifru.ivalue;
	if (want < 1)
		return -ENODEV;

	idx = 1;
	dev = net_get_devices();
	while (dev)
	{
		if (idx == want)
		{
			memset(ifr.ifr_name, 0, sizeof(ifr.ifr_name));
			if (dev->name)
			{
				size_t n = 0;

				while (n < IR0_IFNAMSIZ - 1 && dev->name[n])
				{
					ifr.ifr_name[n] = dev->name[n];
					n++;
				}
			}
			if (copy_to_user(arg, &ifr, sizeof(ifr)) != 0)
				return -EFAULT;
			return 0;
		}
		idx++;
		dev = dev->next;
	}
	return -ENODEV;
}

static int sock_siocgif_one(uint64_t request, void *arg)
{
	struct ir0_ifreq ifr;
	struct net_device *dev;
	ip4_addr_t ip;
	ip4_addr_t nm;
	ip4_addr_t bcast;
	int idx;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&ifr, arg, sizeof(ifr)) != 0)
		return -EFAULT;

	dev = sock_find_dev(ifr.ifr_name);
	if (!dev)
		return -ENODEV;

	ip = sock_dev_ip(dev);
	nm = sock_dev_netmask(dev);
	bcast = sock_dev_bcast(dev);

	switch (request)
	{
	case IR0_SIOCGIFFLAGS:
		ifr.ifr_ifru.flags = (short)(dev->flags & 0xffff);
		if (ifr.ifr_ifru.flags == 0 && ip != 0)
			ifr.ifr_ifru.flags = (short)(IFF_UP | IFF_RUNNING | IFF_BROADCAST);
		break;
	case IR0_SIOCGIFADDR:
		sock_fill_sockaddr_in(&ifr.ifr_ifru.addr, ip);
		break;
	case IR0_SIOCGIFNETMASK:
		sock_fill_sockaddr_in(&ifr.ifr_ifru.addr, nm);
		break;
	case IR0_SIOCGIFBRDADDR:
		sock_fill_sockaddr_in(&ifr.ifr_ifru.addr, bcast);
		break;
	case IR0_SIOCGIFMTU:
		ifr.ifr_ifru.mtu = (int)dev->mtu;
		break;
	case IR0_SIOCGIFHWADDR:
		memset(&ifr.ifr_ifru.addr, 0, sizeof(ifr.ifr_ifru.addr));
		ifr.ifr_ifru.addr.sa_family = IR0_ARPHRD_ETHER;
		memcpy(ifr.ifr_ifru.addr.sa_data, dev->mac, 6);
		break;
	case IR0_SIOCGIFINDEX:
		idx = 1;
		{
			struct net_device *d = net_get_devices();

			while (d && d != dev)
			{
				idx++;
				d = d->next;
			}
		}
		ifr.ifr_ifru.ivalue = idx;
		break;
	case IR0_SIOCGIFMETRIC:
		ifr.ifr_ifru.ivalue = g_if_metric;
		break;
	case IR0_SIOCGIFTXQLEN:
		ifr.ifr_ifru.ivalue = g_if_txqlen;
		break;
	default:
		return -ENOTTY;
	}

	if (copy_to_user(arg, &ifr, sizeof(ifr)) != 0)
		return -EFAULT;
	return 0;
}

static int sock_siocsif_one(uint64_t request, void *arg)
{
	struct ir0_ifreq ifr;
	struct net_device *dev;
	ip4_addr_t ip;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&ifr, arg, sizeof(ifr)) != 0)
		return -EFAULT;

	dev = sock_find_dev(ifr.ifr_name);
	if (!dev)
		return -ENODEV;

	switch (request)
	{
	case IR0_SIOCSIFFLAGS:
		dev->flags = (uint32_t)(unsigned short)ifr.ifr_ifru.flags;
		if (dev->flags & IFF_UP)
			dev->flags |= IFF_RUNNING;
		break;
	case IR0_SIOCSIFADDR:
		ip = sock_sockaddr_in_addr(&ifr.ifr_ifru.addr);
		ip_local_addr = ip;
		sock_apply_iface_ip(dev);
		break;
	case IR0_SIOCSIFNETMASK:
		ip_netmask = sock_sockaddr_in_addr(&ifr.ifr_ifru.addr);
		sock_apply_iface_ip(dev);
		break;
	case IR0_SIOCSIFBRDADDR:
		g_ip_broadcast = sock_sockaddr_in_addr(&ifr.ifr_ifru.addr);
		g_ip_broadcast_set = 1;
		break;
	case IR0_SIOCSIFDSTADDR:
		/* Point-to-point peer — store as gateway hint when non-zero. */
		ip = sock_sockaddr_in_addr(&ifr.ifr_ifru.addr);
		if (ip)
			ip_gateway = ip;
		break;
	case IR0_SIOCSIFMTU:
		if (ifr.ifr_ifru.mtu < 68 || ifr.ifr_ifru.mtu > 65535)
			return -EINVAL;
		dev->mtu = (size_t)ifr.ifr_ifru.mtu;
		break;
	case IR0_SIOCSIFHWADDR:
		memcpy(dev->mac, ifr.ifr_ifru.addr.sa_data, 6);
		break;
	case IR0_SIOCSIFMETRIC:
		g_if_metric = ifr.ifr_ifru.ivalue;
		break;
	case IR0_SIOCSIFTXQLEN:
		g_if_txqlen = ifr.ifr_ifru.ivalue;
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

static int sock_sioc_rt(uint64_t request, void *arg)
{
	struct ir0_rtentry rt;
	ip4_addr_t dst;
	ip4_addr_t gw;
	ip4_addr_t mask;
	int ret;

	if (!arg)
		return -EINVAL;
	if (copy_from_user(&rt, arg, sizeof(rt)) != 0)
		return -EFAULT;

	dst = sock_sockaddr_in_addr(&rt.rt_dst);
	gw = sock_sockaddr_in_addr(&rt.rt_gateway);
	mask = sock_sockaddr_in_addr(&rt.rt_genmask);

	if (request == IR0_SIOCADDRT)
	{
		if (gw)
			ip_gateway = gw;
		ret = ip_route_add(dst, mask, gw);
		return (ret == 0) ? 0 : -ENOMEM;
	}
	if (request == IR0_SIOCDELRT)
	{
		ret = ip_route_del(dst, mask);
		return (ret == 0) ? 0 : -ESRCH;
	}
	return -ENOTTY;
}

int64_t sock_inet_ioctl(uint64_t request, void *arg)
{
	switch (request)
	{
	case IR0_SIOCGIFCONF:
		return sock_siocgifconf(arg);
	case IR0_SIOCGIFNAME:
		return sock_siocgifname(arg);
	case IR0_SIOCGIFFLAGS:
	case IR0_SIOCGIFADDR:
	case IR0_SIOCGIFBRDADDR:
	case IR0_SIOCGIFNETMASK:
	case IR0_SIOCGIFMTU:
	case IR0_SIOCGIFHWADDR:
	case IR0_SIOCGIFINDEX:
	case IR0_SIOCGIFMETRIC:
	case IR0_SIOCGIFTXQLEN:
		return sock_siocgif_one(request, arg);
	case IR0_SIOCSIFFLAGS:
	case IR0_SIOCSIFADDR:
	case IR0_SIOCSIFDSTADDR:
	case IR0_SIOCSIFBRDADDR:
	case IR0_SIOCSIFNETMASK:
	case IR0_SIOCSIFMTU:
	case IR0_SIOCSIFHWADDR:
	case IR0_SIOCSIFMETRIC:
	case IR0_SIOCSIFTXQLEN:
		return sock_siocsif_one(request, arg);
	case IR0_SIOCADDRT:
	case IR0_SIOCDELRT:
		return sock_sioc_rt(request, arg);
	default:
		return -ENOTTY;
	}
}

#else /* !CONFIG_ENABLE_NETWORKING */

int64_t sock_inet_ioctl(uint64_t request, void *arg)
{
	(void)request;
	(void)arg;
	return -ENOTTY;
}

#endif
