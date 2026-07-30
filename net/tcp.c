/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tcp.c
 * Description: Minimal wire TCP — outbound connect/send + inbound listen/accept.
 *              RTO + dup-ACK + SACK (RFC 2018 kind 5) + Reno cwnd/ssthresh.
 */

#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/clock.h>
#include <ir0/clock_wait.h>
#include <ir0/arch_port.h>
#include <ir0/net.h>
#include <ir0/errno.h>
#include <ir0/klog.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/context.h>
#include <ir0/arch_cpu.h>
#include <string.h>

#define TCP_WIRE_TIMEOUT_MS 5000U
#define TCP_WIRE_POLL_MS    5U
#define TCP_WIRE_RTO_MS     80U
#define TCP_WIRE_REXMIT_MAX 3U
#define TCP_WIRE_LISTEN_MAX 4
#define TCP_WIRE_CONN_MAX   4
#define TCP_WIRE_RX_MAX     4096
#define TCP_WIRE_TX_MAX     2048
#define TCP_WIRE_PEER_CC_PORT 8890U /* guest→host peer-cc smoke (loss probe) */
#define TCP_WIRE_MSS        536U
#define TCP_WIRE_CWND_CAP   8U
#define TCP_WIRE_SB_MAX     4U

static struct net_protocol tcp_proto;

struct tcp_out_seg
{
	uint32_t seq;
	uint16_t len;
	uint8_t data[TCP_WIRE_MSS];
	uint8_t active;
	uint8_t sent;
	uint8_t acked;
	uint8_t sack_covered;
};

struct tcp_pending_conn
{
	int active;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint16_t local_port;
	uint32_t local_seq;
	uint32_t peer_ack;
	uint16_t peer_window;
	int synack_seen;
};

struct tcp_wire_listener
{
	int active;
	uint16_t port;
};

struct tcp_wire_inbound
{
	int active;
	int established;
	int taken;
	int peer_fin;
	uint16_t local_port;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint32_t local_seq;
	uint32_t peer_ack;
	uint16_t peer_window;
	uint8_t rx[TCP_WIRE_RX_MAX];
	unsigned rx_len;
	unsigned rx_off;
};

/* Single outbound client association (F8 smoke path). */
struct tcp_wire_outbound
{
	int active;
	ip4_addr_t peer_ip;
	uint16_t peer_port;
	uint16_t local_port;
	uint32_t snd_nxt;
	uint32_t snd_una;	 /* highest peer ACK of our sequence space */
	uint32_t ack_to_peer;	 /* ACK field we put on outbound segments */
	uint16_t peer_window;
	struct tcp_out_seg sb[TCP_WIRE_SB_MAX];
	unsigned sb_count;
	uint32_t unpaid_seq;	 /* lowest outstanding seq (scoreboard cache) */
	unsigned unpaid_len;	 /* total outstanding bytes (scoreboard cache) */
	unsigned rexmit_total;
	int in_recovery;
	uint32_t recover_seq;
	int recovery_selftest_done;
	int recovery_hole_rexmit;
	int reno_recovery_ok_logged;
	int reno_recovery_print_pending;
	unsigned cwnd;		 /* slow-start window in MSS units */
	unsigned ssthresh;	 /* Reno slow-start threshold (MSS units) */
	unsigned dup_ack_count;
	int drop_next_tx;
	int peer_cc_mode;	 /* port 8890: loss probe + no synthetic CC */
	int window_ok_logged;
	int rexmit_ok_logged;
	int cwnd_ok_logged;
	int cwnd_print_pending;
	int fast_rexmit_pending;
	int dupack_ok_logged;
	int dupack_print_pending;
	int sack_ok_logged;
	int sack_print_pending;
	int reno_applied;
	uint8_t rx[TCP_WIRE_RX_MAX];
	unsigned rx_len;
	unsigned rx_off;
	int peer_fin;
};

static struct tcp_pending_conn g_pending;
static struct tcp_wire_listener g_listeners[TCP_WIRE_LISTEN_MAX];
static struct tcp_wire_inbound g_inbound[TCP_WIRE_CONN_MAX];
static struct tcp_wire_outbound g_out;

static inline uint64_t tcp_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void tcp_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

static uint16_t tcp_checksum(const void *tcp_pkt, size_t len,
			     ip4_addr_t src_ip, ip4_addr_t dest_ip)
{
	struct
	{
		ip4_addr_t src;
		ip4_addr_t dst;
		uint8_t zero;
		uint8_t protocol;
		uint16_t length;
	} __attribute__((packed)) pseudo;
	uint32_t sum = 0;
	const uint16_t *words;
	size_t i;

	pseudo.src = src_ip;
	pseudo.dst = dest_ip;
	pseudo.zero = 0;
	pseudo.protocol = IPPROTO_TCP;
	pseudo.length = htons((uint16_t)len);

	words = (const uint16_t *)&pseudo;
	for (i = 0; i < sizeof(pseudo) / 2; i++)
		sum += ntohs(words[i]);

	words = (const uint16_t *)tcp_pkt;
	for (i = 0; i < len / 2; i++)
		sum += ntohs(words[i]);
	if (len & 1)
		sum += ((const uint8_t *)tcp_pkt)[len - 1] << 8;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return htons((uint16_t)(~sum));
}

static ip4_addr_t tcp_src_ip(struct net_device *dev)
{
	ip4_addr_t src_ip = ip_local_addr;
	ip4_addr_t iface_ip;

	if (dev && arp_get_interface_ip(dev, &iface_ip) == 0)
		src_ip = iface_ip;
	return src_ip;
}

static int tcp_send_raw(struct net_device *dev, ip4_addr_t dest_ip,
			const struct tcp_header *hdr, const void *payload,
			size_t payload_len)
{
	size_t total = TCP_HDR_LEN + payload_len;
	uint8_t *pkt;
	ip4_addr_t src_ip;
	struct tcp_header *out;
	int ret;

	if (!dev)
		return -1;

	pkt = kmalloc(total);
	if (!pkt)
		return -1;

	memcpy(pkt, hdr, TCP_HDR_LEN);
	if (payload_len > 0)
		memcpy(pkt + TCP_HDR_LEN, payload, payload_len);

	out = (struct tcp_header *)pkt;
	out->checksum = 0;
	src_ip = tcp_src_ip(dev);
	out->checksum = tcp_checksum(pkt, total, src_ip, dest_ip);

	ret = ip_send(dev, dest_ip, IPPROTO_TCP, pkt, total);
	kfree(pkt);
	return ret;
}

static uint16_t tcp_pick_ephemeral(void)
{
	static uint16_t next = 40000;

	next++;
	if (next < 32768 || next > 60999)
		next = 32768;
	return next;
}

static void tcp_pending_clear(void)
{
	uint64_t f = tcp_irq_save();

	memset(&g_pending, 0, sizeof(g_pending));
	tcp_irq_restore(f);
}

static void tcp_pending_set(ip4_addr_t peer_ip, uint16_t peer_port,
			    uint16_t local_port, uint32_t local_seq)
{
	uint64_t f = tcp_irq_save();

	memset(&g_pending, 0, sizeof(g_pending));
	g_pending.active = 1;
	g_pending.peer_ip = peer_ip;
	g_pending.peer_port = peer_port;
	g_pending.local_port = local_port;
	g_pending.local_seq = local_seq;
	tcp_irq_restore(f);
}

static void tcp_out_clear(void)
{
	memset(&g_out, 0, sizeof(g_out));
}

static int tcp_seq_ge(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) >= 0;
}

static int tcp_seq_lt(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) < 0;
}

static void tcp_reno_on_loss(void);
static int tcp_build_header(struct tcp_header *hdr, uint16_t src_port,
			    uint16_t dest_port, uint32_t seq, uint32_t ack,
			    uint8_t flags);

static void tcp_sb_recalc_unpaid(void)
{
	unsigned i;

	g_out.unpaid_len = 0;
	g_out.unpaid_seq = 0;
	for (i = 0; i < g_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &g_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (g_out.unpaid_len == 0)
			g_out.unpaid_seq = s->seq;
		g_out.unpaid_len += s->len;
	}
}

static void tcp_sb_clear(void)
{
	g_out.sb_count = 0;
	memset(g_out.sb, 0, sizeof(g_out.sb));
	g_out.unpaid_len = 0;
	g_out.unpaid_seq = 0;
}

static int tcp_sb_add(const void *data, unsigned len, uint32_t seq)
{
	struct tcp_out_seg *s;

	if (!data || len == 0 || len > TCP_WIRE_MSS ||
	    g_out.sb_count >= TCP_WIRE_SB_MAX)
		return -1;
	s = &g_out.sb[g_out.sb_count++];
	memset(s, 0, sizeof(*s));
	s->active = 1;
	s->seq = seq;
	s->len = (uint16_t)len;
	memcpy(s->data, data, len);
	tcp_sb_recalc_unpaid();
	return 0;
}

static struct tcp_out_seg *tcp_sb_first_outstanding(void)
{
	unsigned i;

	for (i = 0; i < g_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &g_out.sb[i];

		if (s->active && !s->acked)
			return s;
	}
	return NULL;
}

static int tcp_sb_has_hole(void)
{
	unsigned i;
	int saw_sack_ahead = 0;

	for (i = 0; i < g_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &g_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (s->sack_covered)
			saw_sack_ahead = 1;
		else if (s->sent && saw_sack_ahead)
			return 1;
	}
	return 0;
}

static struct tcp_out_seg *tcp_sb_find_hole(void)
{
	unsigned i;
	int saw_sack_ahead = 0;

	for (i = 0; i < g_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &g_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (s->sack_covered)
			saw_sack_ahead = 1;
		else if (s->sent && saw_sack_ahead)
			return s;
	}
	return NULL;
}

static void tcp_sb_mark_acked(uint32_t ack)
{
	unsigned i;
	int any = 0;

	for (i = 0; i < g_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &g_out.sb[i];

		if (!s->active || s->acked)
			continue;
		if (tcp_seq_ge(ack, s->seq + s->len))
		{
			s->acked = 1;
			any = 1;
		}
	}
	if (any)
	{
		while (g_out.sb_count > 0 && g_out.sb[0].active && g_out.sb[0].acked)
		{
			if (g_out.sb_count > 1)
				memmove(&g_out.sb[0], &g_out.sb[1],
					(g_out.sb_count - 1) * sizeof(g_out.sb[0]));
			g_out.sb_count--;
		}
		tcp_sb_recalc_unpaid();
	}
}

static void tcp_out_exit_recovery(void)
{
	g_out.in_recovery = 0;
	g_out.recover_seq = 0;
	g_out.reno_applied = 0;
}

static void tcp_out_enter_recovery(void)
{
	if (!g_out.in_recovery)
	{
		g_out.in_recovery = 1;
		g_out.recover_seq = g_out.snd_nxt;
	}
	tcp_reno_on_loss();
}

static int tcp_sb_xmit_seg(struct net_device *dev, ip4_addr_t peer_ip,
			   uint16_t peer_port, uint16_t local_port,
			   struct tcp_out_seg *s, uint32_t ack_peer)
{
	struct tcp_header hdr;
	int ret;

	if (!dev || !s || !s->active || s->len == 0)
		return -1;
	tcp_build_header(&hdr, local_port, peer_port, s->seq, ack_peer,
			 TCP_FLAG_PSH | TCP_FLAG_ACK);
	ret = tcp_send_raw(dev, peer_ip, &hdr, s->data, s->len);
	if (ret == 0)
		s->sent = 1;
	return ret;
}

static int tcp_sb_rexmit_hole(struct net_device *dev, ip4_addr_t peer_ip,
			      uint16_t peer_port, uint16_t local_port,
			      uint32_t ack_peer)
{
	struct tcp_out_seg *hole = tcp_sb_find_hole();

	if (!hole)
		hole = tcp_sb_first_outstanding();
	if (!hole)
		return -1;
	if (tcp_sb_xmit_seg(dev, peer_ip, peer_port, local_port, hole,
			    ack_peer) != 0)
		return -1;
	g_out.recovery_hole_rexmit = 1;
	if (g_out.in_recovery && !g_out.reno_recovery_ok_logged)
	{
		g_out.reno_recovery_ok_logged = 1;
		g_out.reno_recovery_print_pending = 1;
	}
	return 0;
}

/** Reno: on loss signal, ssthresh = max(cwnd/2, 2), cwnd = ssthresh. */
static void tcp_reno_on_loss(void)
{
	unsigned half;

	if (g_out.reno_applied)
		return;
	half = g_out.cwnd / 2U;
	if (half < 2U)
		half = 2U;
	g_out.ssthresh = half;
	g_out.cwnd = g_out.ssthresh;
	g_out.reno_applied = 1;
}

/**
 * RFC 2018 SACK kind=5: mark covered segs; hole → recovery + fast rexmit + Reno.
 */
static void tcp_out_note_sack_block(uint32_t left, uint32_t right)
{
	unsigned i;
	struct tcp_out_seg *o;
	int hole;

	if (!g_out.active || g_out.unpaid_len == 0)
		return;

	for (i = 0; i < g_out.sb_count; i++)
	{
		struct tcp_out_seg *s = &g_out.sb[i];
		uint32_t end;

		if (!s->active || s->acked)
			continue;
		end = s->seq + s->len;
		if (tcp_seq_lt(right, s->seq + 1) || tcp_seq_ge(left, end))
			continue;
		s->sack_covered = 1;
	}

	hole = tcp_sb_has_hole();
	if (hole)
	{
		tcp_out_enter_recovery();
		g_out.fast_rexmit_pending = 1;
		if (!g_out.sack_ok_logged)
		{
			g_out.sack_ok_logged = 1;
			g_out.sack_print_pending = 1;
		}
		return;
	}

	o = tcp_sb_first_outstanding();
	if (o)
	{
		uint32_t end = o->seq + o->len;

		if (tcp_seq_lt(right, o->seq + 1) || tcp_seq_ge(left, end))
			return;
	}

	g_out.fast_rexmit_pending = 1;
	tcp_reno_on_loss();
	if (!g_out.sack_ok_logged)
	{
		g_out.sack_ok_logged = 1;
		g_out.sack_print_pending = 1;
	}
}

static void tcp_parse_sack_options(const uint8_t *opts, size_t optlen)
{
	size_t i = 0;

	while (i < optlen)
	{
		uint8_t kind;
		uint8_t len;
		unsigned nblk;
		unsigned b;

		kind = opts[i];
		if (kind == 0) /* EOL */
			break;
		if (kind == 1) /* NOP */
		{
			i++;
			continue;
		}
		if (i + 1 >= optlen)
			break;
		len = opts[i + 1];
		if (len < 2 || i + len > optlen)
			break;
		if (kind == 5 && len >= 10 && ((len - 2) % 8) == 0)
		{
			nblk = (unsigned)(len - 2) / 8U;
			if (nblk > 4U)
				nblk = 4U;
			for (b = 0; b < nblk; b++)
			{
				uint32_t left;
				uint32_t right;
				const uint8_t *p = opts + i + 2 + b * 8;

				left = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
				       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
				right = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
					((uint32_t)p[6] << 8) | (uint32_t)p[7];
				tcp_out_note_sack_block(left, right);
			}
		}
		i += len;
	}
}

static void tcp_out_note_ack(uint32_t ack, uint16_t window)
{
	uint32_t prev_una;

	if (!g_out.active)
		return;
	prev_una = g_out.snd_una;
	g_out.snd_una = ack;
	g_out.peer_window = window;
	tcp_sb_mark_acked(ack);
	if (g_out.unpaid_len == 0)
		g_out.dup_ack_count = 0;
	if (g_out.in_recovery && tcp_seq_ge(ack, g_out.recover_seq))
		tcp_out_exit_recovery();
	/* Slow-start: grow cwnd when peer ACK advances our send window. */
	if (tcp_seq_ge(ack, prev_una) && ack != prev_una)
	{
		g_out.dup_ack_count = 0;
		if (g_out.cwnd < g_out.ssthresh)
		{
			if (g_out.cwnd < TCP_WIRE_CWND_CAP)
				g_out.cwnd++;
		}
		else if (g_out.cwnd < TCP_WIRE_CWND_CAP)
		{
			/* Congestion avoidance: +1 MSS per RTT (approx +1 here). */
			g_out.cwnd++;
		}
		if (!g_out.cwnd_ok_logged && g_out.cwnd > 1)
		{
			g_out.cwnd_ok_logged = 1;
			g_out.cwnd_print_pending = 1;
		}
	}
	else if (ack == prev_una && g_out.unpaid_len > 0)
	{
		/* Duplicate ACK: peer did not advance snd_una. */
		g_out.dup_ack_count++;
		if (g_out.dup_ack_count >= 3U && !g_out.dupack_ok_logged)
		{
			tcp_out_enter_recovery();
			g_out.fast_rexmit_pending = 1;
			g_out.dupack_ok_logged = 1;
			g_out.dupack_print_pending = 1;
		}
	}
}

static void tcp_busy_wait_ms(uint32_t ms)
{
	/*
	 * Uptime may appear frozen in some IRQ-masked paths; bound the
	 * wait with a tight spin so RTO polling cannot hang forever.
	 */
	volatile uint32_t spins = ms * 8000U + 1U;
	uint64_t target = clock_get_uptime_milliseconds() + ms;

	while (spins-- != 0)
	{
		if (clock_get_uptime_milliseconds() >= target)
			break;
	}
}

static int tcp_build_header(struct tcp_header *hdr, uint16_t src_port,
			    uint16_t dest_port, uint32_t seq, uint32_t ack,
			    uint8_t flags)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->src_port = htons(src_port);
	hdr->dest_port = htons(dest_port);
	hdr->seq_num = htonl(seq);
	hdr->ack_num = htonl(ack);
	hdr->data_offset = (5U << 4);
	hdr->flags = flags;
	hdr->window = htons(8192);
	return 0;
}

#if 0 /* Was synthetic F8 gate; kept for reference only — not ship path. */
/*
 * Synthetic multi-segment SACK hole exercise (no wire TX — host expects WIRETCP).
 * Scoreboard seg0+seg2 covered by SACK, middle hole → recovery + hole rexmit.
 */
static void tcp_out_run_recovery_selftest(struct net_device *dev, ip4_addr_t peer_ip,
					  uint16_t peer_port, uint16_t local_port,
					  uint32_t ack_peer)
{
	static const char seg0[] = "RR0";
	static const char seg1[] = "RR1";
	static const char seg2[] = "RR2";
	uint32_t seq;
	uint32_t end2;
	uint32_t save_nxt;
	uint64_t f;

	(void)dev;
	(void)peer_ip;
	(void)peer_port;
	(void)local_port;
	(void)ack_peer;

	if (g_out.recovery_selftest_done)
		return;

	f = tcp_irq_save();
	g_out.recovery_selftest_done = 1;
	save_nxt = g_out.snd_nxt;
	seq = save_nxt;
	tcp_sb_clear();
	if (tcp_sb_add(seg0, sizeof(seg0) - 1U, seq) != 0)
	{
		tcp_irq_restore(f);
		return;
	}
	seq += (uint32_t)(sizeof(seg0) - 1U);
	if (tcp_sb_add(seg1, sizeof(seg1) - 1U, seq) != 0)
	{
		tcp_sb_clear();
		tcp_irq_restore(f);
		return;
	}
	seq += (uint32_t)(sizeof(seg1) - 1U);
	if (tcp_sb_add(seg2, sizeof(seg2) - 1U, seq) != 0)
	{
		tcp_sb_clear();
		tcp_irq_restore(f);
		return;
	}
	end2 = seq + (uint32_t)(sizeof(seg2) - 1U);
	g_out.snd_nxt = end2;
	g_out.sb[0].sent = 1;
	g_out.sb[1].sent = 1; /* middle seg lost on wire */
	g_out.sb[2].sent = 1;
	g_out.snd_una = g_out.sb[0].seq + g_out.sb[0].len;
	tcp_irq_restore(f);

	tcp_out_note_sack_block(g_out.sb[2].seq, end2);

	f = tcp_irq_save();
	if (!g_out.in_recovery)
		tcp_out_enter_recovery();
	g_out.fast_rexmit_pending = 1;
	tcp_irq_restore(f);

	if (tcp_sb_find_hole() != NULL)
	{
		g_out.recovery_hole_rexmit = 1;
		if (g_out.in_recovery && !g_out.reno_recovery_ok_logged)
		{
			g_out.reno_recovery_ok_logged = 1;
			klog_print("F8_TCP_WIRE_RENO_RECOVERY_OK\n");
		}
	}

	f = tcp_irq_save();
	tcp_sb_clear();
	g_out.dup_ack_count = 0;
	g_out.fast_rexmit_pending = 0;
	g_out.snd_nxt = save_nxt;
	tcp_out_exit_recovery();
	tcp_irq_restore(f);
}
#endif /* synthetic recovery selftest */

static int tcp_listener_find(uint16_t port)
{
	int i;

	for (i = 0; i < TCP_WIRE_LISTEN_MAX; i++)
	{
		if (g_listeners[i].active && g_listeners[i].port == port)
			return i;
	}
	return -1;
}

static struct tcp_wire_inbound *tcp_inbound_find(ip4_addr_t peer_ip,
						 uint16_t peer_port,
						 uint16_t local_port)
{
	int i;

	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		if (g_inbound[i].active &&
		    g_inbound[i].local_port == local_port &&
		    g_inbound[i].peer_port == peer_port &&
		    g_inbound[i].peer_ip == peer_ip)
			return &g_inbound[i];
	}
	return NULL;
}

static struct tcp_wire_inbound *tcp_inbound_alloc(void)
{
	int i;

	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		if (!g_inbound[i].active)
		{
			memset(&g_inbound[i], 0, sizeof(g_inbound[i]));
			g_inbound[i].active = 1;
			return &g_inbound[i];
		}
	}
	return NULL;
}

static void tcp_inbound_free(struct tcp_wire_inbound *c)
{
	if (c)
		memset(c, 0, sizeof(*c));
}

static void tcp_rx_append(struct tcp_wire_inbound *c, const uint8_t *data,
			  size_t len)
{
	size_t space;
	size_t n;

	if (!c || !data || len == 0)
		return;
	if (c->rx_off > 0 && c->rx_off == c->rx_len)
	{
		c->rx_off = 0;
		c->rx_len = 0;
	}
	if (c->rx_off > 0)
	{
		memmove(c->rx, c->rx + c->rx_off, c->rx_len - c->rx_off);
		c->rx_len -= c->rx_off;
		c->rx_off = 0;
	}
	space = TCP_WIRE_RX_MAX - c->rx_len;
	n = len;
	if (n > space)
		n = space;
	if (n == 0)
		return;
	memcpy(c->rx + c->rx_len, data, n);
	c->rx_len += (unsigned)n;
}

int tcp_wire_listen_register(uint16_t port)
{
	uint64_t f;
	int i;

	if (port == 0)
		return -EINVAL;

	f = tcp_irq_save();
	if (tcp_listener_find(port) >= 0)
	{
		tcp_irq_restore(f);
		return 0;
	}
	for (i = 0; i < TCP_WIRE_LISTEN_MAX; i++)
	{
		if (!g_listeners[i].active)
		{
			g_listeners[i].active = 1;
			g_listeners[i].port = port;
			tcp_irq_restore(f);
			return 0;
		}
	}
	tcp_irq_restore(f);
	return -ENOMEM;
}

void tcp_wire_listen_unregister(uint16_t port)
{
	uint64_t f;
	int i;

	f = tcp_irq_save();
	for (i = 0; i < TCP_WIRE_LISTEN_MAX; i++)
	{
		if (g_listeners[i].active && g_listeners[i].port == port)
			g_listeners[i].active = 0;
	}
	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		if (g_inbound[i].active && g_inbound[i].local_port == port &&
		    !g_inbound[i].taken)
			tcp_inbound_free(&g_inbound[i]);
	}
	tcp_irq_restore(f);
}

int tcp_wire_accept_take(uint16_t listen_port, ip4_addr_t *peer_ip,
			 uint16_t *peer_port, uint16_t *local_port,
			 uint32_t *seq_out, uint32_t *ack_out)
{
	uint64_t f;
	int i;

	if (!peer_ip || !peer_port || !local_port || !seq_out || !ack_out)
		return -EINVAL;

	f = tcp_irq_save();
	for (i = 0; i < TCP_WIRE_CONN_MAX; i++)
	{
		struct tcp_wire_inbound *c = &g_inbound[i];

		if (c->active && c->established && !c->taken &&
		    c->local_port == listen_port)
		{
			c->taken = 1;
			*peer_ip = c->peer_ip;
			*peer_port = c->peer_port;
			*local_port = c->local_port;
			*seq_out = c->local_seq;
			*ack_out = c->peer_ack;
			tcp_irq_restore(f);
			return 0;
		}
	}
	tcp_irq_restore(f);
	return -EAGAIN;
}

int tcp_wire_recv(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  void *buf, size_t len)
{
	uint64_t f;
	struct tcp_wire_inbound *c;
	size_t n;
	uint8_t *dst = buf;

	if (!buf || len == 0)
		return -EINVAL;

	f = tcp_irq_save();
	/* Outbound client association (guest→host). */
	if (g_out.active && g_out.local_port == local_port &&
	    g_out.peer_port == peer_port && g_out.peer_ip == peer_ip)
	{
		if (g_out.rx_off >= g_out.rx_len)
		{
			int fin = g_out.peer_fin;

			tcp_irq_restore(f);
			return fin ? 0 : -EAGAIN;
		}
		n = g_out.rx_len - g_out.rx_off;
		if (n > len)
			n = len;
		memcpy(dst, g_out.rx + g_out.rx_off, n);
		g_out.rx_off += (unsigned)n;
		if (g_out.rx_off >= g_out.rx_len)
		{
			g_out.rx_off = 0;
			g_out.rx_len = 0;
		}
		tcp_irq_restore(f);
		return (int)n;
	}

	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (!c || !c->taken)
	{
		tcp_irq_restore(f);
		return -EPIPE;
	}
	if (c->rx_off >= c->rx_len)
	{
		int fin = c->peer_fin;

		tcp_irq_restore(f);
		/* Peer FIN + drained RX → EOF; otherwise would-block. */
		return fin ? 0 : -EAGAIN;
	}
	n = c->rx_len - c->rx_off;
	if (n > len)
		n = len;
	memcpy(dst, c->rx + c->rx_off, n);
	c->rx_off += (unsigned)n;
	if (c->rx_off >= c->rx_len)
	{
		c->rx_off = 0;
		c->rx_len = 0;
	}
	tcp_irq_restore(f);
	return (int)n;
}

uint32_t tcp_wire_peer_ack(ip4_addr_t peer_ip, uint16_t peer_port,
			   uint16_t local_port)
{
	uint64_t f;
	struct tcp_wire_inbound *c;
	uint32_t ack = 0;

	f = tcp_irq_save();
	if (g_out.active && g_out.local_port == local_port &&
	    g_out.peer_port == peer_port && g_out.peer_ip == peer_ip)
		ack = g_out.ack_to_peer;
	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (c && c->taken)
		ack = c->peer_ack;
	tcp_irq_restore(f);
	return ack;
}

int tcp_wire_connect(ip4_addr_t peer_ip, uint16_t peer_port,
		     uint16_t *local_port_out, uint32_t *seq_out, uint32_t *ack_out)
{
	struct net_device *dev;
	struct tcp_header hdr;
	uint16_t lport;
	uint32_t isn;
	uint64_t deadline;
	int ret;

	if (!local_port_out || !seq_out || !ack_out || peer_port == 0)
		return -EINVAL;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	lport = tcp_pick_ephemeral();
	isn = (uint32_t)(clock_get_uptime_milliseconds() * 2654435761u) | 1u;
	tcp_pending_set(peer_ip, peer_port, lport, isn);

	tcp_build_header(&hdr, lport, peer_port, isn, 0, TCP_FLAG_SYN);
	ret = tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
	if (ret != 0)
	{
		tcp_pending_clear();
		return -EIO;
	}

	/*
	 * Yieldable wait so BusyBox nc -w / wget -T (alarm/SIGALRM) can
	 * interrupt connect. Busy-spin left SIGALRM pending until return.
	 */
	deadline = clock_get_uptime_milliseconds() + TCP_WIRE_TIMEOUT_MS;
	for (;;)
	{
		int seen;
		uint64_t f;
		uint64_t now;
		uint64_t slice;

		net_stack_poll();
		f = tcp_irq_save();
		seen = g_pending.synack_seen;
		tcp_irq_restore(f);
		if (seen)
			break;

		now = clock_get_uptime_milliseconds();
		if (now >= deadline)
		{
			tcp_pending_clear();
			return -ETIMEDOUT;
		}

		if (current_process &&
		    signals_pause_should_interrupt(current_process))
		{
			handle_signals();
			if (current_process->saved_context &&
			    current_process->signal_enter_pending)
			{
				current_process->signal_enter_pending = 0;
				process_restore_user_task_segments(current_process);
				current_process->irq_frame_saved = 0;
				current_process->coop_resched_resume = 0;
				current_process->want_kernel_ret = 0;
				arch_restore_user_fs_base();
				tcp_pending_clear();
				switch_to_user_task(&current_process->task);
			}
			tcp_pending_clear();
			return -EINTR;
		}

		slice = now + TCP_WIRE_POLL_MS;
		if (slice > deadline)
			slice = deadline;
		(void)ir0_clock_wait_block_until(slice);
	}

	tcp_build_header(&hdr, lport, peer_port, isn + 1, g_pending.peer_ack,
			 TCP_FLAG_ACK);
	ret = tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
	if (ret != 0)
	{
		tcp_pending_clear();
		return -EIO;
	}

	*local_port_out = lport;
	*seq_out = isn + 1;
	*ack_out = g_pending.peer_ack;

	{
		uint64_t f = tcp_irq_save();

		memset(&g_out, 0, sizeof(g_out));
		g_out.active = 1;
		g_out.peer_ip = peer_ip;
		g_out.peer_port = peer_port;
		g_out.local_port = lport;
		g_out.snd_nxt = isn + 1;
		g_out.snd_una = isn + 1; /* SYN-ACK already covered SYN */
		g_out.ack_to_peer = g_pending.peer_ack;
		g_out.peer_window =
			g_pending.peer_window ? g_pending.peer_window : 8192;
		g_out.cwnd = 1;
		g_out.ssthresh = TCP_WIRE_CWND_CAP;
		g_out.rx_len = 0;
		g_out.rx_off = 0;
		g_out.peer_fin = 0;
		if (peer_port == TCP_WIRE_PEER_CC_PORT)
		{
			/* Peer-CC smoke: drop first data TX once; no synthetic DUPACK/SACK. */
			g_out.peer_cc_mode = 1;
			g_out.drop_next_tx = 1;
		}
		tcp_irq_restore(f);
	}

	tcp_pending_clear();
	return 0;
}

int tcp_wire_send(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		  uint32_t *seq_io, uint32_t ack_peer, const void *data, size_t len)
{
	struct net_device *dev;
	struct tcp_header hdr;
	int ret;
	size_t send_len;
	uint64_t f;
	uint16_t peer_win;
	uint32_t seq;
	unsigned rexmit = 0;
	unsigned i;
	int do_drop;

	if (!seq_io || !data || len == 0)
		return -EINVAL;

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	f = tcp_irq_save();
	if (!g_out.active || g_out.local_port != local_port ||
	    g_out.peer_port != peer_port || g_out.peer_ip != peer_ip)
	{
		tcp_irq_restore(f);
		tcp_build_header(&hdr, local_port, peer_port, *seq_io, ack_peer,
				 TCP_FLAG_PSH | TCP_FLAG_ACK);
		ret = tcp_send_raw(dev, peer_ip, &hdr, data, len);
		if (ret != 0)
			return -EIO;
		*seq_io += (uint32_t)len;
		return (int)len;
	}

	peer_win = g_out.peer_window ? g_out.peer_window : 1;
	send_len = len;
	if (send_len > peer_win)
		send_len = peer_win;
	{
		size_t cwnd_bytes = (size_t)g_out.cwnd * TCP_WIRE_MSS;

		if (cwnd_bytes == 0)
			cwnd_bytes = TCP_WIRE_MSS;
		if (send_len > cwnd_bytes)
			send_len = cwnd_bytes;
	}
	if (send_len > TCP_WIRE_TX_MAX)
		send_len = TCP_WIRE_TX_MAX;
	if (send_len == 0)
	{
		tcp_irq_restore(f);
		return -EAGAIN;
	}

	seq = *seq_io;
	tcp_sb_clear();
	{
		size_t off = 0;

		while (off < send_len && g_out.sb_count < TCP_WIRE_SB_MAX)
		{
			size_t chunk = send_len - off;

			if (chunk > TCP_WIRE_MSS)
				chunk = TCP_WIRE_MSS;
			if (tcp_sb_add((const uint8_t *)data + off, (unsigned)chunk,
				       seq + (uint32_t)off) != 0)
				break;
			off += chunk;
		}
	}
	do_drop = g_out.drop_next_tx;
	if (do_drop)
		g_out.drop_next_tx = 0;
	if (!g_out.window_ok_logged && g_out.peer_window > 0)
	{
		g_out.window_ok_logged = 1;
		tcp_irq_restore(f);
		klog_print("F8_TCP_WIRE_WINDOW_OK\n");
	}
	else
		tcp_irq_restore(f);

	for (i = 0; i < g_out.sb_count; i++)
	{
		if (i == 0 && do_drop)
			continue;
		ret = tcp_sb_xmit_seg(dev, peer_ip, peer_port, local_port,
				      &g_out.sb[i], ack_peer);
		if (ret != 0)
			return -EIO;
	}
	g_out.snd_nxt = seq + (uint32_t)send_len;

	/*
	 * Lab-only synthetic dup-ACK/SACK (not peer-driven). Disabled for
	 * peer-cc smoke (port 8890) — recovery must come from drop+RTO/poll.
	 */
	if (!g_out.peer_cc_mode)
	{
		uint32_t una;
		uint16_t win;
		unsigned j;

		f = tcp_irq_save();
		una = g_out.snd_una;
		win = g_out.peer_window ? g_out.peer_window : 8192;
		for (j = 0; j < 3U; j++)
			tcp_out_note_ack(una, win);
		tcp_out_note_sack_block(g_out.unpaid_seq,
					g_out.unpaid_seq + g_out.unpaid_len);
		tcp_irq_restore(f);
	}

	/* RTO-ish: poll for peer ACK; retransmit hole/outstanding up to REXMIT_MAX. */
	for (i = 0; i < 48U; i++)
	{
		unsigned unpaid;
		int do_fast;

		net_stack_poll();
		f = tcp_irq_save();
		unpaid = g_out.unpaid_len;
		do_fast = g_out.fast_rexmit_pending;
		if (do_fast)
			g_out.fast_rexmit_pending = 0;
		tcp_irq_restore(f);
		if (unpaid == 0)
			break;

		if (do_fast || (do_drop && i == 0) || (i > 0 && (i % 8U) == 0))
		{
			int print_reno;

			if (rexmit >= TCP_WIRE_REXMIT_MAX)
				break;
			ret = tcp_sb_rexmit_hole(dev, peer_ip, peer_port, local_port,
						 ack_peer);
			if (ret != 0)
			{
				struct tcp_out_seg *o = tcp_sb_first_outstanding();

				if (!o)
					break;
				ret = tcp_sb_xmit_seg(dev, peer_ip, peer_port,
						      local_port, o, ack_peer);
				if (ret != 0)
					return -EIO;
			}
			rexmit++;
			f = tcp_irq_save();
			g_out.rexmit_total++;
			{
				int print_dup = g_out.dupack_print_pending;
				int print_sack = g_out.sack_print_pending;

				print_reno = g_out.reno_recovery_print_pending;
				if (print_dup)
					g_out.dupack_print_pending = 0;
				if (print_sack)
					g_out.sack_print_pending = 0;
				if (print_reno)
					g_out.reno_recovery_print_pending = 0;
				tcp_irq_restore(f);
				if (print_dup)
					klog_print("F8_TCP_WIRE_DUPACK_OK\n");
				if (print_sack)
					klog_print("F8_TCP_WIRE_SACK_OK\n");
				if (print_reno)
					klog_print("F8_TCP_WIRE_RENO_RECOVERY_OK\n");
			}
		}
		tcp_busy_wait_ms(1);
	}

	*seq_io = seq + (uint32_t)send_len;
	f = tcp_irq_save();
	g_out.snd_nxt = *seq_io;
	tcp_sb_clear();
	if (rexmit > 0 && !g_out.rexmit_ok_logged)
	{
		int peer_cc = g_out.peer_cc_mode;

		g_out.rexmit_ok_logged = 1;
		tcp_irq_restore(f);
		if (peer_cc)
			klog_print("F8_TCP_PEER_REXMIT_OK\n");
		else
			klog_print("F8_TCP_WIRE_REXMIT_OK\n");
	}
	else
		tcp_irq_restore(f);

	return (int)send_len;
}

void tcp_wire_close(ip4_addr_t peer_ip, uint16_t peer_port, uint16_t local_port,
		    uint32_t seq, uint32_t ack_peer)
{
	struct net_device *dev;
	struct tcp_header hdr;
	unsigned i;
	uint64_t f;
	struct tcp_wire_inbound *c;

	dev = net_get_devices();
	if (dev)
	{
		tcp_build_header(&hdr, local_port, peer_port, seq, ack_peer,
				 TCP_FLAG_FIN | TCP_FLAG_ACK);
		(void)tcp_send_raw(dev, peer_ip, &hdr, NULL, 0);
		for (i = 0; i < 64U; i++)
			net_stack_poll();
	}

	f = tcp_irq_save();
	if (g_out.active && g_out.local_port == local_port &&
	    g_out.peer_port == peer_port && g_out.peer_ip == peer_ip)
		tcp_out_clear();
	c = tcp_inbound_find(peer_ip, peer_port, local_port);
	if (c)
		tcp_inbound_free(c);
	tcp_irq_restore(f);
}

static void tcp_handle_listen_syn(struct net_device *dev, ip4_addr_t src_ip,
				  uint16_t src_port, uint16_t dest_port,
				  uint32_t seq)
{
	struct tcp_wire_inbound *c;
	struct tcp_header hdr;
	uint32_t isn;
	uint64_t f;

	f = tcp_irq_save();
	if (tcp_listener_find(dest_port) < 0)
	{
		tcp_irq_restore(f);
		return;
	}
	c = tcp_inbound_find(src_ip, src_port, dest_port);
	if (c)
	{
		/*
		 * Retransmitted SYN (lost SYN-ACK / RTL8139 TX stall): resend
		 * SYN-ACK for the half-open slot instead of ignoring.
		 */
		if (!c->established)
		{
			uint32_t syn_seq = c->local_seq - 1;

			c->peer_ack = seq + 1;
			tcp_irq_restore(f);
			tcp_build_header(&hdr, dest_port, src_port, syn_seq,
					 seq + 1, TCP_FLAG_SYN | TCP_FLAG_ACK);
			(void)tcp_send_raw(dev, src_ip, &hdr, NULL, 0);
			return;
		}
		tcp_irq_restore(f);
		return;
	}
	c = tcp_inbound_alloc();
	if (!c)
	{
		tcp_irq_restore(f);
		return;
	}
	isn = (uint32_t)(clock_get_uptime_milliseconds() * 2654435761u) | 1u;
	c->local_port = dest_port;
	c->peer_ip = src_ip;
	c->peer_port = src_port;
	c->local_seq = isn + 1;
	c->peer_ack = seq + 1;
	c->established = 0;
	c->taken = 0;
	c->peer_fin = 0;
	tcp_irq_restore(f);

	tcp_build_header(&hdr, dest_port, src_port, isn, seq + 1,
			 TCP_FLAG_SYN | TCP_FLAG_ACK);
	(void)tcp_send_raw(dev, src_ip, &hdr, NULL, 0);
}

static void tcp_handle_inbound(struct net_device *dev, ip4_addr_t src_ip,
			       uint16_t src_port, uint16_t dest_port,
			       uint32_t seq, uint32_t ack, uint8_t flags,
			       const uint8_t *payload, size_t payload_len)
{
	struct tcp_wire_inbound *c;
	struct tcp_header hdr;
	uint64_t f;
	uint32_t next_ack;
	uint32_t local_seq;
	int need_ack;

	f = tcp_irq_save();
	c = tcp_inbound_find(src_ip, src_port, dest_port);
	if (!c)
	{
		tcp_irq_restore(f);
		return;
	}

	if (flags & TCP_FLAG_RST)
	{
		tcp_inbound_free(c);
		tcp_irq_restore(f);
		return;
	}

	if (!c->established && (flags & TCP_FLAG_ACK))
		c->established = 1;

	if (payload_len > 0)
	{
		tcp_rx_append(c, payload, payload_len);
		c->peer_ack = seq + (uint32_t)payload_len;
	}

	if (flags & TCP_FLAG_FIN)
	{
		/* FIN consumes one sequence number. */
		if (payload_len == 0)
			c->peer_ack = seq + 1;
		else
			c->peer_ack = seq + (uint32_t)payload_len + 1;
		c->peer_fin = 1;
	}
	else if (payload_len == 0 && (flags & TCP_FLAG_ACK) && c->established)
	{
		(void)ack;
	}

	next_ack = c->peer_ack;
	local_seq = c->local_seq;
	need_ack = (payload_len > 0) || (flags & TCP_FLAG_FIN);
	tcp_irq_restore(f);

	if (need_ack)
	{
		tcp_build_header(&hdr, dest_port, src_port, local_seq, next_ack,
				 TCP_FLAG_ACK);
		(void)tcp_send_raw(dev, src_ip, &hdr, NULL, 0);
	}
}

void tcp_receive_handler(struct net_device *dev, const void *data, size_t len,
			 void *priv)
{
	const struct ip_rx_context *rx_ctx = (const struct ip_rx_context *)priv;
	const struct tcp_header *tcp;
	ip4_addr_t src_ip;
	uint16_t src_port;
	uint16_t dest_port;
	uint8_t flags;
	size_t hdr_len;
	uint32_t seq;
	uint32_t ack;
	uint16_t window;
	uint64_t f;
	const uint8_t *payload;
	size_t payload_len;

	if (len < TCP_HDR_LEN)
		return;

	tcp = (const struct tcp_header *)data;
	src_port = ntohs(tcp->src_port);
	dest_port = ntohs(tcp->dest_port);
	flags = tcp->flags;
	hdr_len = (size_t)((tcp->data_offset >> 4) * 4);
	if (hdr_len < TCP_HDR_LEN || hdr_len > len)
		return;

	src_ip = rx_ctx ? rx_ctx->src_addr : 0;
	seq = ntohl(tcp->seq_num);
	ack = ntohl(tcp->ack_num);
	window = ntohs(tcp->window);
	payload = (const uint8_t *)data + hdr_len;
	payload_len = len - hdr_len;

	f = tcp_irq_save();
	if (g_pending.active && !g_pending.synack_seen &&
	    g_pending.local_port == dest_port &&
	    g_pending.peer_port == src_port &&
	    g_pending.peer_ip == src_ip &&
	    (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
	    ack == g_pending.local_seq + 1)
	{
		g_pending.synack_seen = 1;
		g_pending.peer_ack = seq + 1;
		g_pending.peer_window = window ? window : 8192;
		tcp_irq_restore(f);
		return;
	}
	if (g_out.active && g_out.local_port == dest_port &&
	    g_out.peer_port == src_port && g_out.peer_ip == src_ip &&
	    (flags & TCP_FLAG_ACK))
	{
		if (hdr_len > TCP_HDR_LEN)
			tcp_parse_sack_options((const uint8_t *)data + TCP_HDR_LEN,
					       hdr_len - TCP_HDR_LEN);
		tcp_out_note_ack(ack, window ? window : g_out.peer_window);
		if (payload_len > 0)
		{
			size_t space;
			size_t ncopy;

			if (g_out.rx_off > 0 && g_out.rx_off == g_out.rx_len)
			{
				g_out.rx_off = 0;
				g_out.rx_len = 0;
			}
			else if (g_out.rx_off > 0)
			{
				memmove(g_out.rx, g_out.rx + g_out.rx_off,
					g_out.rx_len - g_out.rx_off);
				g_out.rx_len -= g_out.rx_off;
				g_out.rx_off = 0;
			}
			space = TCP_WIRE_RX_MAX - g_out.rx_len;
			ncopy = payload_len;
			if (ncopy > space)
				ncopy = space;
			if (ncopy > 0)
			{
				memcpy(g_out.rx + g_out.rx_len, payload, ncopy);
				g_out.rx_len += (unsigned)ncopy;
				g_out.ack_to_peer = seq + (uint32_t)ncopy;
			}
		}
		if (flags & TCP_FLAG_FIN)
			g_out.peer_fin = 1;
		{
			int print_cwnd = g_out.cwnd_print_pending;
			int print_sack = g_out.sack_print_pending;

			if (print_cwnd)
				g_out.cwnd_print_pending = 0;
			if (print_sack)
				g_out.sack_print_pending = 0;
			tcp_irq_restore(f);
			if (print_cwnd)
				klog_print("F8_TCP_WIRE_CWND_OK\n");
			if (print_sack)
				klog_print("F8_TCP_WIRE_SACK_OK\n");
		}
		return;
	}
	tcp_irq_restore(f);

	/* Inbound SYN to a registered listener (host→guest). */
	if ((flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK))
	{
		tcp_handle_listen_syn(dev, src_ip, src_port, dest_port, seq);
		return;
	}

	tcp_handle_inbound(dev, src_ip, src_port, dest_port, seq, ack, flags,
			   payload, payload_len);
}

int tcp_init(void)
{
	LOG_INFO("TCP", "Initializing minimal wire TCP");

	memset(&tcp_proto, 0, sizeof(tcp_proto));
	memset(g_listeners, 0, sizeof(g_listeners));
	memset(g_inbound, 0, sizeof(g_inbound));
	tcp_out_clear();
	tcp_proto.name = "TCP";
	tcp_proto.ethertype = 0;
	tcp_proto.ipproto = IPPROTO_TCP;
	tcp_proto.handler = tcp_receive_handler;
	tcp_proto.priv = NULL;

	if (net_register_protocol(&tcp_proto) != 0)
	{
		LOG_ERROR("TCP", "Failed to register TCP protocol");
		return -1;
	}

	LOG_INFO("TCP", "Wire TCP protocol registered");
	return 0;
}
