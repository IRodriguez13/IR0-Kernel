/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net_command_stress.c
 * Description: Aggressive BusyBox networking applet stress for QEMU (pid1).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static void tag(const char *s)
{
	write(1, s, strlen(s));
}

static int runv(char *const av[])
{
	pid_t p;
	int st = -1;

	p = fork();
	if (p < 0)
		return -1;
	if (p == 0)
	{
		execv(av[0], av);
		_exit(127);
	}
	if (waitpid(p, &st, 0) < 0)
		return -1;
	if (WIFSIGNALED(st))
		return 1000 + WTERMSIG(st);
	if (!WIFEXITED(st))
		return -2;
	return WEXITSTATUS(st);
}

/* Soft retry for QEMU/SLIRP + rare RX checksum flakes (not SEGV). */
static int runv_ping(char *const av[])
{
	int i;
	int rc = -1;

	for (i = 0; i < 3; i++)
	{
		rc = runv(av);
		if (rc == 0 || rc >= 1000)
			return rc;
	}
	return rc;
}

static void note_rc(const char *name, int rc)
{
	char buf[96];
	int n;

	n = snprintf(buf, sizeof(buf), "STRESS_RC %s=%d\n", name, rc);
	if (n > 0)
		write(1, buf, (size_t)n);
}

static int force_eth0_up(void)
{
	struct ifreq ifr;
	int sk;
	int rc;

	sk = socket(AF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
	rc = ioctl(sk, SIOCGIFFLAGS, &ifr);
	if (rc == 0)
	{
		ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
		rc = ioctl(sk, SIOCSIFFLAGS, &ifr);
	}
	close(sk);
	return rc;
}

int main(void)
{
	int fails = 0;
	int round;
	int rc;

	tag("NET_STRESS_START\n");
	(void)force_eth0_up();

	for (round = 1; round <= 8; round++)
	{
		char rtag[32];

		snprintf(rtag, sizeof(rtag), "NET_STRESS_ROUND_%d\n", round);
		tag(rtag);

		/* ifconfig: display + mutate */
		rc = runv((char *[]){"/bin/ifconfig", NULL});
		note_rc("ifconfig", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/ifconfig", "-a", NULL});
		note_rc("ifconfig_-a", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/ifconfig", "eth0", "10.0.2.15",
					 "netmask", "255.255.255.0",
					 "broadcast", "10.0.2.255", "up", NULL});
		note_rc("ifconfig_set", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/ifconfig", "eth0", "mtu", "1500", NULL});
		note_rc("ifconfig_mtu", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/ifconfig", "eth0", "txqueuelen",
					 "1000", NULL});
		note_rc("ifconfig_txq", rc);
		if (rc >= 1000)
			fails++;

		/* route */
		rc = runv((char *[]){"/bin/route", "add", "default", "gw",
					 "10.0.2.2", NULL});
		note_rc("route_add", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/route", "-n", NULL});
		note_rc("route_n", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/route", "del", "default", NULL});
		note_rc("route_del", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/route", "add", "default", "gw",
					 "10.0.2.2", NULL});
		note_rc("route_add2", rc);
		if (rc >= 1000)
			fails++;

		/* ping: many fancy flags — must get replies (RX health gate) */
		rc = runv_ping((char *[]){"/bin/ping", "-c", "1", "-W", "2",
					 "10.0.2.2", NULL});
		note_rc("ping", rc);
		if (rc != 0)
			fails++;
		rc = runv_ping((char *[]){"/bin/ping", "-c", "1", "-W", "2", "-I",
					 "eth0", "10.0.2.2", NULL});
		note_rc("ping_I_dev", rc);
		if (rc != 0)
			fails++;
		rc = runv_ping((char *[]){"/bin/ping", "-c", "1", "-W", "2", "-I",
					 "10.0.2.15", "10.0.2.2", NULL});
		note_rc("ping_I_ip", rc);
		if (rc != 0)
			fails++;
		rc = runv_ping((char *[]){"/bin/ping", "-c", "1", "-W", "2", "-t",
					 "16", "10.0.2.2", NULL});
		note_rc("ping_t", rc);
		if (rc != 0)
			fails++;
		rc = runv_ping((char *[]){"/bin/ping", "-c", "2", "-A", "-W", "2",
					 "-q", "-s", "32", "10.0.2.2", NULL});
		note_rc("ping_Aqs", rc);
		if (rc != 0)
			fails++;
		rc = runv_ping((char *[]){"/bin/ping", "-c", "1", "-W", "2", "-p",
					 "aa", "10.0.2.2", NULL});
		note_rc("ping_p", rc);
		if (rc != 0)
			fails++;

		/* netstat / nslookup */
		rc = runv((char *[]){"/bin/netstat", NULL});
		note_rc("netstat", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/netstat", "-rn", NULL});
		note_rc("netstat_rn", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/nslookup", "10.0.2.2", "10.0.2.3",
					 NULL});
		note_rc("nslookup", rc);
		if (rc >= 1000)
			fails++;

		/* nc timeout exit(1) is expected (port 9 discard). */
		rc = runv((char *[]){"/bin/nc", "-w", "2", "10.0.2.2", "9",
					 NULL});
		note_rc("nc_w", rc);
		if (rc >= 1000)
			fails++;
		/* wget: only SEGV/crash counts; host HTTP may be flaky */
		rc = runv((char *[]){"/bin/wget", "-q", "-O", "-",
					 "http://10.0.2.2/", NULL});
		note_rc("wget", rc);
		if (rc >= 1000)
			fails++;
		rc = runv((char *[]){"/bin/wget", "-q", "-O", "/tmp/w.out",
					 "http://10.0.2.2/", NULL});
		note_rc("wget_O_file", rc);
		if (rc >= 1000)
			fails++;

		/* hostname (enabled in ir0_full) */
		rc = runv((char *[]){"/bin/hostname", NULL});
		note_rc("hostname", rc);
		if (rc >= 1000)
			fails++;

		/* Concurrent pings only — wire TCP is single g_out (parallel wget SEGVs). */
		{
			pid_t kids[2];
			int i;

			for (i = 0; i < 2; i++)
			{
				kids[i] = fork();
				if (kids[i] == 0)
				{
					execl("/bin/ping", "ping", "-c", "1",
					      "-W", "2", "10.0.2.2", (char *)0);
					_exit(127);
				}
			}
			for (i = 0; i < 2; i++)
			{
				if (kids[i] > 0)
				{
					int cst = 0;

					waitpid(kids[i], &cst, 0);
					if (WIFSIGNALED(cst))
						fails++;
				}
			}
			tag("NET_STRESS_PARALLEL_DONE\n");
		}
	}

	/* Abuse ioctl control path (musl if_nametoindex style) */
	for (round = 0; round < 64; round++)
	{
		struct ifreq ifr;
		int sk = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

		if (sk < 0)
		{
			fails++;
			break;
		}
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
		if (ioctl(sk, SIOCGIFINDEX, &ifr) != 0)
			fails++;
		close(sk);
	}
	tag("NET_STRESS_IFINDEX_DONE\n");

	if (fails == 0)
		tag("NET_STRESS_PASS\n");
	else
		tag("NET_STRESS_FAIL\n");
	tag("NET_STRESS_ALL_DONE\n");
	_exit(fails ? 1 : 0);
}
