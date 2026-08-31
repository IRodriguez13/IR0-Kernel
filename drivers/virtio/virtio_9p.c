/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_9p.c
 * Description: Minimal virtio-pci legacy + 9P2000.L for QEMU host directory share.
 *
 * Smoke expects:
 *   -fsdev local,id=ir0fs,path=...,security_model=none
 *   -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on
 *
 * Sources: virtio 0.9.5 legacy PCI; Linux include/net/9p/9p.h message ids;
 * QEMU wiki Documentation/9psetup.
 */

#include "virtio_9p.h"

#include <ir0/arch_io.h>
#include <ir0/cpu.h>
#include <ir0/cpu.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <string.h>
#include <stddef.h>
#include <ir0/ktm/klog.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_ENABLE_BIT     0x80000000U

#define VIRTIO_VENDOR      0x1AF4
#define VIRTIO_DEV_9P      0x1009

#define VIRTIO_PCI_HOST_FEATURES 0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN     8
#define VIRTIO_PCI_QUEUE_NUM     12
#define VIRTIO_PCI_QUEUE_SEL     14
#define VIRTIO_PCI_QUEUE_NOTIFY  16
#define VIRTIO_PCI_STATUS        18
#define VIRTIO_PCI_ISR           19
#define VIRTIO_PCI_CONFIG        20

#define VIRTIO_STATUS_ACK        1
#define VIRTIO_STATUS_DRIVER     2
#define VIRTIO_STATUS_DRIVER_OK  4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED     128

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* 9P2000.L message ids (Linux include/net/9p/9p.h) */
#define P9_RLERROR  7
#define P9_TSTATFS  8
#define P9_RSTATFS  9
#define P9_TLOPEN   12
#define P9_RLOPEN   13
#define P9_TLCREATE 14
#define P9_RLCREATE 15
#define P9_TSYMLINK 16
#define P9_RSYMLINK 17
#define P9_TRENAME  20
#define P9_RRENAME  21
#define P9_TREADLINK 22
#define P9_RREADLINK 23
#define P9_TGETATTR 24
#define P9_RGETATTR 25
#define P9_TSETATTR 26
#define P9_RSETATTR 27
#define P9_TREADDIR 40
#define P9_RREADDIR 41
#define P9_TLINK    70
#define P9_RLINK    71
#define P9_TMKDIR   72
#define P9_RMKDIR   73
#define P9_TRENAMEAT 74
#define P9_RRENAMEAT 75
#define P9_TUNLINKAT 76
#define P9_RUNLINKAT 77
#define P9_TVERSION 100
#define P9_RVERSION 101
#define P9_TATTACH  104
#define P9_RATTACH  105
#define P9_RERROR   107
#define P9_TWALK    110
#define P9_RWALK    111
#define P9_TREAD    116
#define P9_RREAD    117
#define P9_TWRITE   118
#define P9_RWRITE   119
#define P9_TCLUNK   120
#define P9_RCLUNK   121

/* 9P2000.L getattr / setattr (Linux include/net/9p/9p.h) */
#define P9_STATS_MODE  0x00000001ULL
#define P9_STATS_SIZE  0x00000200ULL
#define P9_STATS_BASIC 0x000007ffULL
#define P9_ATTR_SIZE   (1u << 3)

/* 9P2000.L open / unlinkat flags */
#define P9_DOTL_RDONLY 0x00000000u
#define P9_DOTL_WRONLY 0x00000001u
#define P9_DOTL_RDWR   0x00000002u
#define P9_DOTL_CREAT  0x00000040u
#define P9_DOTL_TRUNC  0x00000200u
#define P9_DOTL_DIRECTORY 0x00010000u
#define P9_DOTL_AT_REMOVEDIR 0x200u

#define P9_QTDIR 0x80u

#define V9P_QUEUE_MAX  128
#define V9P_BUF_SIZE   8192
#define V9P_MSIZE      8192
#define V9P_READ_CHUNK 4096u

struct vring_desc
{
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

struct vring_used_elem
{
	uint32_t id;
	uint32_t len;
};

static uint16_t g_iobase;
static int g_ready;
static uint16_t g_qsz;
static uint16_t g_last_used_idx;
static uint16_t g_p9_tag = 1;
static uint32_t g_root_fid = 1;

static struct vring_desc *g_desc;
static uint16_t *g_avail_flags;
static uint16_t *g_avail_idx;
static uint16_t *g_avail_ring;
static uint16_t *g_used_flags;
static uint16_t *g_used_idx;
static struct vring_used_elem *g_used_ring;
static uint8_t *g_req;
static uint8_t *g_resp;
static void *g_vring_mem;

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
	uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			((uint32_t)func << 8) | (off & 0xFCu);
	outl(PCI_CONFIG_ADDRESS, addr);
	return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val)
{
	uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			((uint32_t)func << 8) | (off & 0xFCu);
	outl(PCI_CONFIG_ADDRESS, addr);
	outl(PCI_CONFIG_DATA, val);
}

static void vp_outb(uint16_t off, uint8_t v)
{
	outb((uint16_t)(g_iobase + off), v);
}

static void vp_outw(uint16_t off, uint16_t v)
{
	outw((uint16_t)(g_iobase + off), v);
}

static void vp_outl(uint16_t off, uint32_t v)
{
	outl((uint16_t)(g_iobase + off), v);
}

static uint8_t vp_inb(uint16_t off)
{
	return inb((uint16_t)(g_iobase + off));
}

static uint16_t vp_inw(uint16_t off)
{
	return inw((uint16_t)(g_iobase + off));
}

static uint32_t vp_inl(uint16_t off)
{
	return inl((uint16_t)(g_iobase + off));
}

static void p9_put8(uint8_t **p, uint8_t v)
{
	*(*p)++ = v;
}

static void p9_put16(uint8_t **p, uint16_t v)
{
	*(*p)++ = (uint8_t)(v & 0xFF);
	*(*p)++ = (uint8_t)((v >> 8) & 0xFF);
}

static void p9_put32(uint8_t **p, uint32_t v)
{
	*(*p)++ = (uint8_t)(v & 0xFF);
	*(*p)++ = (uint8_t)((v >> 8) & 0xFF);
	*(*p)++ = (uint8_t)((v >> 16) & 0xFF);
	*(*p)++ = (uint8_t)((v >> 24) & 0xFF);
}

static void p9_put64(uint8_t **p, uint64_t v)
{
	p9_put32(p, (uint32_t)(v & 0xFFFFFFFFu));
	p9_put32(p, (uint32_t)(v >> 32));
}

static void p9_putstr(uint8_t **p, const char *s)
{
	uint16_t n = (uint16_t)strlen(s);
	p9_put16(p, n);
	memcpy(*p, s, n);
	*p += n;
}

static uint8_t p9_get8(uint8_t **p)
{
	return *(*p)++;
}

static uint16_t p9_get16(uint8_t **p)
{
	uint16_t v = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
	*p += 2;
	return v;
}

static uint32_t p9_get32(uint8_t **p)
{
	uint32_t v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
		     ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
	*p += 4;
	return v;
}

static uint64_t p9_get64(uint8_t **p)
{
	uint64_t lo = p9_get32(p);
	uint64_t hi = p9_get32(p);
	return lo | (hi << 32);
}

static int v9p_exchange(uint32_t req_len, uint32_t *resp_len)
{
	uint16_t desc_idx = 0;
	uint16_t avail_idx;
	int spins;

	if (!g_ready || !g_desc || !g_qsz)
		return -ENODEV;

	g_desc[0].addr = (uint64_t)(uintptr_t)g_req;
	g_desc[0].len = req_len;
	g_desc[0].flags = VRING_DESC_F_NEXT;
	g_desc[0].next = 1;

	g_desc[1].addr = (uint64_t)(uintptr_t)g_resp;
	g_desc[1].len = V9P_BUF_SIZE;
	g_desc[1].flags = VRING_DESC_F_WRITE;
	g_desc[1].next = 0;

	avail_idx = *g_avail_idx;
	g_avail_ring[avail_idx % g_qsz] = desc_idx;
	smp_mb();
	*g_avail_idx = (uint16_t)(avail_idx + 1);
	smp_mb();

	vp_outw(VIRTIO_PCI_QUEUE_NOTIFY, 0);

	for (spins = 0; spins < 2000000; spins++)
	{
		smp_mb();
		if (*g_used_idx != g_last_used_idx)
		{
			uint16_t u = (uint16_t)(g_last_used_idx % g_qsz);
			if (resp_len)
				*resp_len = g_used_ring[u].len;
			g_last_used_idx++;
			(void)vp_inb(VIRTIO_PCI_ISR);
			return 0;
		}
	}
	klog_smoke("HOSTSHARE_9P_NOTIFY_TIMEOUT");
	return -EIO;
}

static int v9p_rpc(uint8_t type, uint8_t *body, uint32_t body_len,
		   uint8_t expect_type, uint8_t **out_body, uint32_t *out_body_len)
{
	uint8_t *p = g_req;
	uint32_t size = 7 + body_len;
	uint32_t resp_len = 0;
	uint8_t *rp;
	uint32_t rsize;
	uint8_t rtype;
	uint16_t rtag;
	int rc;

	if (size > V9P_BUF_SIZE)
		return -EFBIG;

	p9_put32(&p, size);
	p9_put8(&p, type);
	p9_put16(&p, g_p9_tag);
	if (body_len)
		memcpy(p, body, body_len);

	rc = v9p_exchange(size, &resp_len);
	if (rc < 0)
		return rc;
	if (resp_len < 7)
		return -EIO;

	rp = g_resp;
	rsize = p9_get32(&rp);
	rtype = p9_get8(&rp);
	rtag = p9_get16(&rp);
	(void)rsize;
	(void)rtag;
	g_p9_tag++;

	if (rtype == P9_RLERROR)
	{
		uint32_t ecode;

		if (resp_len < 11)
			return -EIO;
		ecode = p9_get32(&rp);
		if (ecode == 0 || ecode > 255)
			return -EIO;
		return -(int)ecode;
	}
	if (rtype == P9_RERROR)
		return -EIO;
	if (rtype != expect_type)
		return -EIO;

	if (out_body)
		*out_body = rp;
	if (out_body_len)
		*out_body_len = resp_len - 7;
	return 0;
}

static int v9p_version(void)
{
	uint8_t body[64];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	int rc;

	p9_put32(&p, V9P_MSIZE);
	p9_putstr(&p, "9P2000.L");
	rc = v9p_rpc(P9_TVERSION, body, (uint32_t)(p - body), P9_RVERSION, &rb, &rblen);
	return rc;
}

static int v9p_attach(uint32_t fid)
{
	uint8_t body[96];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	p9_put32(&p, fid);
	p9_put32(&p, (~0U)); /* afid NOFID */
	p9_putstr(&p, "");
	p9_putstr(&p, "");
	p9_put32(&p, 0); /* n_uname */
	return v9p_rpc(P9_TATTACH, body, (uint32_t)(p - body), P9_RATTACH, &rb, &rblen);
}

static int v9p_clunk(uint32_t fid)
{
	uint8_t body[8];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	p9_put32(&p, fid);
	return v9p_rpc(P9_TCLUNK, body, (uint32_t)(p - body), P9_RCLUNK, &rb, &rblen);
}

static int v9p_walk(uint32_t fid, uint32_t newfid, const char *name)
{
	uint8_t body[96];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	p9_put32(&p, fid);
	p9_put32(&p, newfid);
	if (name && name[0])
	{
		p9_put16(&p, 1);
		p9_putstr(&p, name);
	}
	else
	{
		p9_put16(&p, 0);
	}
	return v9p_rpc(P9_TWALK, body, (uint32_t)(p - body), P9_RWALK, &rb, &rblen);
}

/*
 * Walk a relative path (may contain '/') from the share root into @newfid.
 * Rejects empty components, "." and ".." (MVP: no parent traversal).
 */
static int v9p_walk_path(uint32_t newfid, const char *relpath)
{
	char path[256];
	char *p;
	char *slash;
	int rc;
	int started = 0;

	if (!relpath)
		return -EINVAL;
	while (*relpath == '/')
		relpath++;
	if (!relpath[0])
		return v9p_walk(g_root_fid, newfid, NULL);

	if (strlen(relpath) >= sizeof(path))
		return -ENAMETOOLONG;
	memcpy(path, relpath, strlen(relpath) + 1);

	p = path;
	while (*p)
	{
		while (*p == '/')
			p++;
		if (!*p)
			break;
		slash = strchr(p, '/');
		if (slash)
			*slash = '\0';
		if (!p[0] || strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
			return -EINVAL;

		if (!started)
		{
			rc = v9p_walk(g_root_fid, newfid, p);
			started = 1;
		}
		else
			rc = v9p_walk(newfid, newfid, p);
		if (rc < 0)
		{
			/*
			 * 9P2000: Twalk with newfid==fid that fails mid-path leaves
			 * the fid undefined — clunk before returning.
			 */
			if (started)
				(void)v9p_clunk(newfid);
			return rc;
		}
		if (!slash)
			break;
		p = slash + 1;
	}

	if (!started)
		return v9p_walk(g_root_fid, newfid, NULL);
	return 0;
}

/* Split "a/b/c" into parent "a/b" and base "c". Parent may be empty. */
static int v9p_split_parent_base(const char *relpath, char *parent, size_t psz,
				 char *base, size_t bsz)
{
	const char *start;
	const char *slash;
	size_t blen;
	size_t plen;

	if (!relpath || !parent || !base || psz == 0 || bsz == 0)
		return -EINVAL;
	start = relpath;
	while (*start == '/')
		start++;
	if (!start[0])
		return -EINVAL;
	slash = strrchr(start, '/');
	if (!slash)
	{
		parent[0] = '\0';
		blen = strlen(start);
		if (blen >= bsz)
			return -ENAMETOOLONG;
		memcpy(base, start, blen + 1);
		return 0;
	}
	plen = (size_t)(slash - start);
	blen = strlen(slash + 1);
	if (plen >= psz || blen == 0 || blen >= bsz)
		return -ENAMETOOLONG;
	memcpy(parent, start, plen);
	parent[plen] = '\0';
	memcpy(base, slash + 1, blen + 1);
	if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
		return -EINVAL;
	return 0;
}

static int v9p_lopen(uint32_t fid, uint32_t flags)
{
	uint8_t body[12];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tlopen: fid + flags (u32) — Linux p9_client_open "dd" */
	p9_put32(&p, fid);
	p9_put32(&p, flags);
	return v9p_rpc(P9_TLOPEN, body, (uint32_t)(p - body), P9_RLOPEN, &rb, &rblen);
}

static int v9p_lcreate(uint32_t fid, const char *name, uint32_t flags, uint32_t mode,
		       uint32_t gid)
{
	uint8_t body[128];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tlcreate: fid, name, flags, mode, gid — Linux "dsddg" */
	p9_put32(&p, fid);
	p9_putstr(&p, name);
	p9_put32(&p, flags);
	p9_put32(&p, mode);
	p9_put32(&p, gid);
	return v9p_rpc(P9_TLCREATE, body, (uint32_t)(p - body), P9_RLCREATE, &rb, &rblen);
}

static int v9p_write(uint32_t fid, uint64_t offset, const void *data, uint32_t count,
		     uint32_t *written)
{
	/*
	 * The message body carries a full msize payload. On the stack it made
	 * this frame 8 KiB, and a write to the host share reaches it under
	 * sys_write, which is another 4 KiB: measured peak stack for an
	 * ordinary session write was 14 KiB of 32 KiB before this moved off.
	 */
	uint8_t *body;
	uint8_t *p;
	uint8_t *rb;
	uint32_t rblen;
	int rc;

	if (count + 16 > (uint32_t)V9P_BUF_SIZE)
		return -EFBIG;

	body = kmalloc_try(V9P_BUF_SIZE);
	if (!body)
		return -ENOMEM;

	p = body;
	p9_put32(&p, fid);
	p9_put64(&p, offset);
	p9_put32(&p, count);
	memcpy(p, data, count);
	p += count;
	rc = v9p_rpc(P9_TWRITE, body, (uint32_t)(p - body), P9_RWRITE, &rb, &rblen);
	kfree(body);
	if (rc < 0)
		return rc;
	if (rblen < 4)
		return -EIO;
	if (written)
		*written = p9_get32(&rb);
	return 0;
}

static int v9p_read(uint32_t fid, uint64_t offset, void *data, uint32_t count, uint32_t *got)
{
	uint8_t body[24];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	uint32_t n;
	int rc;

	p9_put32(&p, fid);
	p9_put64(&p, offset);
	p9_put32(&p, count);
	rc = v9p_rpc(P9_TREAD, body, (uint32_t)(p - body), P9_RREAD, &rb, &rblen);
	if (rc < 0)
		return rc;
	if (rblen < 4)
		return -EIO;
	n = p9_get32(&rb);
	if (n > count)
		n = count;
	if (n && data)
		memcpy(data, rb, n);
	if (got)
		*got = n;
	return 0;
}

static int v9p_getattr(uint32_t fid, uint64_t request_mask, uint32_t *mode_out,
		       uint64_t *size_out)
{
	uint8_t body[16];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	uint64_t valid;
	uint32_t mode;
	uint64_t size;
	int rc;

	/* Tgetattr: fid[4] request_mask[8] */
	p9_put32(&p, fid);
	p9_put64(&p, request_mask);
	rc = v9p_rpc(P9_TGETATTR, body, (uint32_t)(p - body), P9_RGETATTR, &rb, &rblen);
	if (rc < 0)
		return rc;
	/* Rgetattr: valid[8] qid[13] mode[4] uid[4] gid[4] nlink[8] rdev[8] size[8] … */
	if (rblen < 8 + 13 + 4 + 4 + 4 + 8 + 8 + 8)
		return -EIO;
	valid = p9_get64(&rb);
	rb += 13; /* skip qid */
	mode = p9_get32(&rb);
	rb += 4 + 4; /* uid gid */
	rb += 8 + 8; /* nlink rdev */
	size = p9_get64(&rb);
	(void)valid;
	if (mode_out)
		*mode_out = mode;
	if (size_out)
		*size_out = size;
	return 0;
}

/*
 * Tstatfs: fid[4]
 * Rstatfs: type[4] bsize[4] blocks[8] bfree[8] bavail[8] files[8] ffree[8]
 *          fsid[8] namelen[4]
 *
 * Without it statfs(2) on a 9p mount reported f_blocks = 0 and df printed the
 * share with zero size.
 */
static int v9p_statfs(uint32_t fid, struct virtio_9p_statfs *out)
{
	uint8_t body[8];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	int rc;

	if (!out)
		return -EINVAL;

	p9_put32(&p, fid);
	rc = v9p_rpc(P9_TSTATFS, body, (uint32_t)(p - body), P9_RSTATFS, &rb,
		     &rblen);
	if (rc < 0)
		return rc;
	if (rblen < 4 + 4 + 8 + 8 + 8 + 8 + 8 + 8 + 4)
		return -EIO;

	out->type = p9_get32(&rb);
	out->bsize = p9_get32(&rb);
	out->blocks = p9_get64(&rb);
	out->bfree = p9_get64(&rb);
	out->bavail = p9_get64(&rb);
	out->files = p9_get64(&rb);
	out->ffree = p9_get64(&rb);
	rb += 8; /* fsid */
	out->namelen = p9_get32(&rb);
	return 0;
}

int virtio_9p_statfs(struct virtio_9p_statfs *out)
{
	if (!virtio_9p_ready())
		return -ENODEV;
	return v9p_statfs(g_root_fid, out);
}

static int v9p_mkdir(uint32_t dfid, const char *name, uint32_t mode, uint32_t gid)
{
	uint8_t body[128];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tmkdir: dfid, name, mode, gid — Linux "dsdg" */
	p9_put32(&p, dfid);
	p9_putstr(&p, name);
	p9_put32(&p, mode);
	p9_put32(&p, gid);
	return v9p_rpc(P9_TMKDIR, body, (uint32_t)(p - body), P9_RMKDIR, &rb, &rblen);
}

static int v9p_unlinkat(uint32_t dfid, const char *name, uint32_t flags)
{
	uint8_t body[128];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tunlinkat: dfid, name, flags — Linux "dsd" */
	p9_put32(&p, dfid);
	p9_putstr(&p, name);
	p9_put32(&p, flags);
	return v9p_rpc(P9_TUNLINKAT, body, (uint32_t)(p - body), P9_RUNLINKAT, &rb,
			&rblen);
}

static int v9p_renameat(uint32_t olddirfid, const char *oldname, uint32_t newdirfid,
			const char *newname)
{
	uint8_t body[256];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Trenameat: olddirfid, oldname, newdirfid, newname — Linux "dsds" */
	p9_put32(&p, olddirfid);
	p9_putstr(&p, oldname);
	p9_put32(&p, newdirfid);
	p9_putstr(&p, newname);
	return v9p_rpc(P9_TRENAMEAT, body, (uint32_t)(p - body), P9_RRENAMEAT, &rb,
			&rblen);
}

static int v9p_link(uint32_t dfid, uint32_t fid, const char *name)
{
	uint8_t body[128];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tlink: dfid, fid, name — Linux "dds" */
	p9_put32(&p, dfid);
	p9_put32(&p, fid);
	p9_putstr(&p, name);
	return v9p_rpc(P9_TLINK, body, (uint32_t)(p - body), P9_RLINK, &rb, &rblen);
}

static int v9p_readdir(uint32_t fid, uint64_t offset, void *data, uint32_t count,
		       uint32_t *got)
{
	uint8_t body[24];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	uint32_t n;
	int rc;

	/* Treaddir: fid, offset, count — same shape as Tread */
	p9_put32(&p, fid);
	p9_put64(&p, offset);
	p9_put32(&p, count);
	rc = v9p_rpc(P9_TREADDIR, body, (uint32_t)(p - body), P9_RREADDIR, &rb, &rblen);
	if (rc < 0)
		return rc;
	if (rblen < 4)
		return -EIO;
	n = p9_get32(&rb);
	if (n > count)
		n = count;
	if (n && data)
		memcpy(data, rb, n);
	if (got)
		*got = n;
	return 0;
}

static int v9p_setattr_size(uint32_t fid, uint64_t size)
{
	uint8_t body[64];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tsetattr: fid + iattr_dotl (valid, mode, uid, gid, size, times…) */
	p9_put32(&p, fid);
	p9_put32(&p, P9_ATTR_SIZE);
	p9_put32(&p, 0); /* mode */
	p9_put32(&p, 0); /* uid */
	p9_put32(&p, 0); /* gid */
	p9_put64(&p, size);
	p9_put64(&p, 0);
	p9_put64(&p, 0);
	p9_put64(&p, 0);
	p9_put64(&p, 0);
	return v9p_rpc(P9_TSETATTR, body, (uint32_t)(p - body), P9_RSETATTR, &rb, &rblen);
}

int virtio_9p_stat_file(const char *relpath, uint64_t *size_out, uint32_t *mode_out)
{
	uint32_t fid = 4;
	uint32_t mode = 0;
	uint64_t size = 0;
	int rc;

	if (!g_ready || !relpath)
		return -EINVAL;

	rc = v9p_walk_path(fid, relpath);
	if (rc < 0)
		return rc;
	rc = v9p_getattr(fid, P9_STATS_BASIC, &mode, &size);
	(void)v9p_clunk(fid);
	if (rc < 0)
		return rc;
	if (size_out)
		*size_out = size;
	if (mode_out)
		*mode_out = mode;
	return 0;
}

int virtio_9p_write_at(const char *relpath, const void *buf, size_t len, uint64_t offset)
{
	uint32_t fid = 2;
	uint32_t written = 0;
	int rc;
	char parent[240];
	char base[64];
	size_t done = 0;

	if (!g_ready || !relpath || (!buf && len > 0))
		return -EINVAL;
	rc = v9p_split_parent_base(relpath, parent, sizeof(parent), base, sizeof(base));
	if (rc < 0)
		return rc;

	rc = v9p_walk_path(fid, relpath);
	if (rc == 0)
	{
		rc = v9p_lopen(fid, P9_DOTL_WRONLY);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
	}
	else
	{
		(void)v9p_clunk(fid);
		rc = v9p_walk_path(fid, parent);
		if (rc < 0)
			return rc;
		rc = v9p_lcreate(fid, base, P9_DOTL_WRONLY, 0666u, 0u);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
	}

	while (done < len)
	{
		uint32_t chunk = (uint32_t)(len - done);

		if (chunk > V9P_READ_CHUNK)
			chunk = V9P_READ_CHUNK;
		written = 0;
		rc = v9p_write(fid, offset + done, (const uint8_t *)buf + done, chunk,
			       &written);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
		if (written == 0)
		{
			(void)v9p_clunk(fid);
			return -EIO;
		}
		done += written;
	}

	(void)v9p_clunk(fid);
	return 0;
}

int virtio_9p_write_file(const char *relpath, const void *buf, size_t len)
{
	uint32_t fid = 2;
	int rc;
	char parent[240];
	char base[64];

	if (!g_ready || !relpath || (!buf && len > 0))
		return -EINVAL;
	rc = v9p_split_parent_base(relpath, parent, sizeof(parent), base, sizeof(base));
	if (rc < 0)
		return rc;

	rc = v9p_walk_path(fid, relpath);
	if (rc == 0)
	{
		rc = v9p_lopen(fid, P9_DOTL_WRONLY | P9_DOTL_TRUNC);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
	}
	else
	{
		(void)v9p_clunk(fid);
		rc = v9p_walk_path(fid, parent);
		if (rc < 0)
			return rc;
		rc = v9p_lcreate(fid, base, P9_DOTL_WRONLY, 0666u, 0u);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
	}

	if (len > 0)
	{
		uint32_t written = 0;
		size_t done = 0;

		while (done < len)
		{
			uint32_t chunk = (uint32_t)(len - done);

			if (chunk > V9P_READ_CHUNK)
				chunk = V9P_READ_CHUNK;
			written = 0;
			rc = v9p_write(fid, done, (const uint8_t *)buf + done, chunk,
				       &written);
			if (rc < 0)
			{
				(void)v9p_clunk(fid);
				return rc;
			}
			if (written == 0)
			{
				(void)v9p_clunk(fid);
				return -EIO;
			}
			done += written;
		}
	}

	(void)v9p_clunk(fid);
	return 0;
}

int virtio_9p_truncate(const char *relpath, uint64_t length)
{
	uint32_t fid = 11;
	int rc;

	if (!g_ready || !relpath)
		return -EINVAL;
	rc = v9p_walk_path(fid, relpath);
	if (rc < 0)
		return rc;
	rc = v9p_setattr_size(fid, length);
	(void)v9p_clunk(fid);
	return rc;
}

int virtio_9p_mkdir(const char *relpath, uint32_t mode)
{
	uint32_t fid = 6;
	char parent[240];
	char base[64];
	int rc;

	if (!g_ready || !relpath || !relpath[0])
		return -EINVAL;
	rc = v9p_split_parent_base(relpath, parent, sizeof(parent), base, sizeof(base));
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(fid, parent);
	if (rc < 0)
		return rc;
	rc = v9p_mkdir(fid, base, mode ? mode : 0755u, 0u);
	(void)v9p_clunk(fid);
	return rc;
}

int virtio_9p_unlink(const char *relpath, int is_dir)
{
	uint32_t fid = 6;
	char parent[240];
	char base[64];
	int rc;
	uint32_t flags = is_dir ? P9_DOTL_AT_REMOVEDIR : 0u;

	if (!g_ready || !relpath || !relpath[0])
		return -EINVAL;
	rc = v9p_split_parent_base(relpath, parent, sizeof(parent), base, sizeof(base));
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(fid, parent);
	if (rc < 0)
		return rc;
	rc = v9p_unlinkat(fid, base, flags);
	(void)v9p_clunk(fid);
	return rc;
}

int virtio_9p_rename(const char *oldpath, const char *newpath)
{
	uint32_t olddir = 7;
	uint32_t newdir = 8;
	char old_parent[240];
	char old_base[64];
	char new_parent[240];
	char new_base[64];
	int rc;

	if (!g_ready || !oldpath || !newpath)
		return -EINVAL;
	rc = v9p_split_parent_base(oldpath, old_parent, sizeof(old_parent), old_base,
				   sizeof(old_base));
	if (rc < 0)
		return rc;
	rc = v9p_split_parent_base(newpath, new_parent, sizeof(new_parent), new_base,
				   sizeof(new_base));
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(olddir, old_parent);
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(newdir, new_parent);
	if (rc < 0)
	{
		(void)v9p_clunk(olddir);
		return rc;
	}
	rc = v9p_renameat(olddir, old_base, newdir, new_base);
	(void)v9p_clunk(olddir);
	(void)v9p_clunk(newdir);
	return rc;
}

int virtio_9p_link(const char *oldpath, const char *newpath)
{
	uint32_t src = 10;
	uint32_t dstdir = 6;
	char new_parent[240];
	char new_base[64];
	int rc;

	if (!g_ready || !oldpath || !newpath)
		return -EINVAL;
	rc = v9p_split_parent_base(newpath, new_parent, sizeof(new_parent), new_base,
				   sizeof(new_base));
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(src, oldpath);
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(dstdir, new_parent);
	if (rc < 0)
	{
		(void)v9p_clunk(src);
		return rc;
	}
	rc = v9p_link(dstdir, src, new_base);
	(void)v9p_clunk(src);
	(void)v9p_clunk(dstdir);
	return rc;
}

int virtio_9p_symlink(const char *linkpath, const char *target)
{
	uint32_t fid = 12;
	char parent[240];
	char base[64];
	uint8_t body[384];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	int rc;

	if (!g_ready || !linkpath || !target || !linkpath[0] || !target[0])
		return -EINVAL;
	rc = v9p_split_parent_base(linkpath, parent, sizeof(parent), base, sizeof(base));
	if (rc < 0)
		return rc;
	rc = v9p_walk_path(fid, parent);
	if (rc < 0)
		return rc;
	/* Tsymlink: dfid, name, symtgt, gid — Linux "dssg" */
	p9_put32(&p, fid);
	p9_putstr(&p, base);
	p9_putstr(&p, target);
	p9_put32(&p, 0);
	rc = v9p_rpc(P9_TSYMLINK, body, (uint32_t)(p - body), P9_RSYMLINK, &rb, &rblen);
	(void)v9p_clunk(fid);
	return rc;
}

int virtio_9p_readlink(const char *relpath, char *buf, size_t buflen)
{
	uint32_t fid = 13;
	uint8_t body[8];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	uint16_t n;
	int rc;

	if (!g_ready || !relpath || !buf || buflen == 0)
		return -EINVAL;
	rc = v9p_walk_path(fid, relpath);
	if (rc < 0)
		return rc;
	p9_put32(&p, fid);
	rc = v9p_rpc(P9_TREADLINK, body, (uint32_t)(p - body), P9_RREADLINK, &rb, &rblen);
	if (rc < 0)
	{
		(void)v9p_clunk(fid);
		return rc;
	}
	if (rblen < 2)
	{
		(void)v9p_clunk(fid);
		return -EIO;
	}
	n = p9_get16(&rb);
	if ((uint32_t)(2 + n) > rblen)
	{
		(void)v9p_clunk(fid);
		return -EIO;
	}
	if (n >= buflen)
		n = (uint16_t)(buflen - 1);
	memcpy(buf, rb, n);
	buf[n] = '\0';
	(void)v9p_clunk(fid);
	return (int)n;
}

int virtio_9p_readdir(const char *relpath, virtio_9p_dirent_t *entries, int max)
{
	uint32_t fid = 9;
	uint64_t off = 0;
	/* 4 KiB of directory data; on the stack it left a getdents chain with
	 * no room for an interrupt frame (see sys_getdents_common). */
	uint8_t *buf;
	int n = 0;
	int rc;

	if (!g_ready || !entries || max <= 0)
		return -EINVAL;

	buf = kmalloc_try(V9P_READ_CHUNK);
	if (!buf)
		return -ENOMEM;

	rc = v9p_walk_path(fid, relpath ? relpath : "");
	if (rc < 0)
	{
		kfree(buf);
		return rc;
	}
	rc = v9p_lopen(fid, P9_DOTL_RDONLY | P9_DOTL_DIRECTORY);
	if (rc < 0)
	{
		/* Some servers accept plain RDONLY on dirs */
		rc = v9p_lopen(fid, P9_DOTL_RDONLY);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			kfree(buf);
			return rc;
		}
	}

	while (n < max)
	{
		uint32_t got = 0;
		uint8_t *p;
		uint8_t *end;

		rc = v9p_readdir(fid, off, buf, V9P_READ_CHUNK, &got);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			kfree(buf);
			return rc;
		}
		if (got == 0)
			break;

		p = buf;
		end = buf + got;
		while (p + 13 + 8 + 1 + 2 <= end && n < max)
		{
			uint8_t qtype;
			uint64_t next_off;
			uint8_t dtype;
			uint16_t namelen;

			qtype = p[0];
			p += 13; /* qid */
			next_off = p9_get64(&p);
			dtype = p9_get8(&p);
			namelen = p9_get16(&p);
			if (p + namelen > end)
				break;
			if (namelen > 0 && namelen < sizeof(entries[n].name))
			{
				memcpy(entries[n].name, p, namelen);
				entries[n].name[namelen] = '\0';
				if (!(namelen == 1 && entries[n].name[0] == '.') &&
				    !(namelen == 2 && entries[n].name[0] == '.' &&
				      entries[n].name[1] == '.'))
				{
					if (qtype & P9_QTDIR)
						entries[n].type = 4;
					else if (dtype == 4)
						entries[n].type = 4;
					else
						entries[n].type = 8;
					n++;
				}
			}
			p += namelen;
			off = next_off;
		}
		if (got < V9P_READ_CHUNK)
			break;
	}

	(void)v9p_clunk(fid);
	kfree(buf);
	return n;
}

int virtio_9p_read_file(const char *relpath, void *buf, size_t maxlen)
{
	uint32_t fid = 3;
	uint64_t off = 0;
	size_t total = 0;
	int rc;

	if (!g_ready || !relpath || !buf)
		return -EINVAL;

	rc = v9p_walk_path(fid, relpath);
	if (rc < 0)
		return rc;
	rc = v9p_lopen(fid, P9_DOTL_RDONLY);
	if (rc < 0)
	{
		(void)v9p_clunk(fid);
		return rc;
	}

	while (total < maxlen)
	{
		uint32_t want;
		uint32_t got = 0;

		want = (uint32_t)(maxlen - total);
		if (want > V9P_READ_CHUNK)
			want = V9P_READ_CHUNK;
		rc = v9p_read(fid, off, (uint8_t *)buf + total, want, &got);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
		if (got == 0)
			break;
		total += got;
		off += got;
		if (got < want)
			break;
	}

	(void)v9p_clunk(fid);
	if (total > (size_t)0x7fffffff)
		return -EFBIG;
	return (int)total;
}

int virtio_9p_read_at(const char *relpath, void *buf, size_t count, uint64_t offset,
		      size_t *got_out)
{
	uint32_t fid = 5;
	uint64_t off = offset;
	size_t total = 0;
	int rc;

	if (!g_ready || !relpath || !buf)
		return -EINVAL;

	rc = v9p_walk_path(fid, relpath);
	if (rc < 0)
		return rc;
	rc = v9p_lopen(fid, P9_DOTL_RDONLY);
	if (rc < 0)
	{
		(void)v9p_clunk(fid);
		return rc;
	}

	while (total < count)
	{
		uint32_t want;
		uint32_t got = 0;

		want = (uint32_t)(count - total);
		if (want > V9P_READ_CHUNK)
			want = V9P_READ_CHUNK;
		rc = v9p_read(fid, off, (uint8_t *)buf + total, want, &got);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
		if (got == 0)
			break;
		total += got;
		off += got;
		if (got < want)
			break;
	}

	(void)v9p_clunk(fid);
	if (got_out)
		*got_out = total;
	return 0;
}

static int find_virtio_9p(uint8_t *bus, uint8_t *slot)
{
	uint16_t b;
	uint8_t s;

	for (b = 0; b < 256; b++)
	{
		for (s = 0; s < 32; s++)
		{
			uint32_t id = pci_read((uint8_t)b, s, 0, 0);
			if ((id & 0xFFFF) == VIRTIO_VENDOR && (id >> 16) == VIRTIO_DEV_9P)
			{
				*bus = (uint8_t)b;
				*slot = s;
				return 0;
			}
		}
	}
	return -1;
}

static int setup_vring(uint16_t qsz)
{
	size_t desc_sz;
	size_t avail_sz;
	size_t used_off;
	size_t need;
	uintptr_t base;

	if (qsz == 0 || qsz > V9P_QUEUE_MAX)
		return -EINVAL;

	g_qsz = qsz;
	desc_sz = sizeof(struct vring_desc) * qsz;
	/* flags + idx + ring[qsz] + used_event (legacy layout) */
	avail_sz = 6u + 2u * qsz;
	used_off = (desc_sz + avail_sz + 4095u) & ~(size_t)4095u;
	need = used_off + 6u + sizeof(struct vring_used_elem) * qsz;
	need = (need + 4095u) & ~(size_t)4095u;

	g_vring_mem = kmalloc_aligned(need, 4096);
	if (!g_vring_mem)
		return -ENOMEM;
	memset(g_vring_mem, 0, need);
	base = (uintptr_t)g_vring_mem;
	g_desc = (struct vring_desc *)base;
	g_avail_flags = (uint16_t *)(base + desc_sz);
	g_avail_idx = g_avail_flags + 1;
	g_avail_ring = g_avail_idx + 1;
	g_used_flags = (uint16_t *)(base + used_off);
	g_used_idx = g_used_flags + 1;
	g_used_ring = (struct vring_used_elem *)(g_used_idx + 1);

	g_req = (uint8_t *)kmalloc_aligned(V9P_BUF_SIZE, 16);
	g_resp = (uint8_t *)kmalloc_aligned(V9P_BUF_SIZE, 16);
	if (!g_req || !g_resp)
		return -ENOMEM;
	memset(g_req, 0, V9P_BUF_SIZE);
	memset(g_resp, 0, V9P_BUF_SIZE);
	return 0;
}

int virtio_9p_init(void)
{
	uint8_t bus, slot;
	uint32_t bar0;
	uint32_t cmd;
	uint16_t qsz;
	uintptr_t pfn;
	int rc;

	g_ready = 0;
	if (find_virtio_9p(&bus, &slot) != 0)
	{
		klog_smoke("HOSTSHARE_9P_ABSENT");
		return -ENODEV;
	}

	cmd = pci_read(bus, slot, 0, 0x04);
	pci_write(bus, slot, 0, 0x04, cmd | 0x5); /* IO + BusMaster */

	bar0 = pci_read(bus, slot, 0, 0x10);
	if (!(bar0 & 1))
	{
		klog_smoke("HOSTSHARE_9P_NO_IOBAR");
		return -ENODEV;
	}
	g_iobase = (uint16_t)(bar0 & ~0x3u);

	vp_outb(VIRTIO_PCI_STATUS, 0);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

	(void)vp_inl(VIRTIO_PCI_HOST_FEATURES);
	vp_outl(VIRTIO_PCI_GUEST_FEATURES, 0);

	vp_outw(VIRTIO_PCI_QUEUE_SEL, 0);
	qsz = vp_inw(VIRTIO_PCI_QUEUE_NUM);
	if (qsz == 0 || qsz > V9P_QUEUE_MAX)
	{
		klog_smoke("HOSTSHARE_9P_BAD_QSZ");
		vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
		return -EINVAL;
	}

	rc = setup_vring(qsz);
	if (rc < 0)
	{
		vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
		return rc;
	}

	/* Queue PFN: physical page of descriptor table (identity map). */
	pfn = (uintptr_t)g_desc >> 12;
	vp_outl(VIRTIO_PCI_QUEUE_PFN, (uint32_t)pfn);

	vp_outb(VIRTIO_PCI_STATUS,
		VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

	g_last_used_idx = 0;
	g_ready = 1;

	rc = v9p_version();
	if (rc < 0)
	{
		klog_smoke("HOSTSHARE_9P_VERSION_FAIL");
		g_ready = 0;
		return rc;
	}
	rc = v9p_attach(g_root_fid);
	if (rc < 0)
	{
		klog_smoke("HOSTSHARE_9P_ATTACH_FAIL");
		g_ready = 0;
		return rc;
	}

	klog_smoke("HOSTSHARE_9P_READY");
	return 0;
}

int virtio_9p_ready(void)
{
	return g_ready;
}

