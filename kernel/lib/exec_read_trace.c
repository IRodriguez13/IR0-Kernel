// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — EXEC-only busybox read path serial diagnostics.
 */

#include <ir0/exec_read_trace.h>
#include <ir0/ktm/klog.h>

int vfs_exec_audit_is_active(void);

static int exec_read_trace_on(void)
{
	return vfs_exec_audit_is_active();
}

void exec_read_trace_vfs_read_file(const char *path, uint64_t ino, int64_t size,
				     off_t offset, size_t req, int ret)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][VFS] stage=read_file path=%s inode=0x%llx size=0x%llx offset=0x%llx req=0x%llx ret=0x%llx",
		      path ? path : "(null)", (unsigned long long)ino,
		      (unsigned long long)size, (unsigned long long)offset,
		      (unsigned long long)req, (unsigned long long)(int64_t)ret);
}

void exec_read_trace_minix_file_begin(const char *path, uint16_t inode_num,
				      uint16_t mode, uint32_t size)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][MINIX] stage=read_file_begin path=%s inode=0x%x mode=0x%x size=0x%x",
		      path ? path : "(null)", (unsigned)inode_num, (unsigned)mode,
		      (unsigned)size);
}

void exec_read_trace_minix_zones(const uint16_t zones[9])
{
	if (!exec_read_trace_on() || !zones)
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][MINIX] stage=zones z0=0x%x z1=0x%x z2=0x%x z3=0x%x z4=0x%x z5=0x%x z6=0x%x z7=0x%x z8=0x%x",
		      (unsigned)zones[0], (unsigned)zones[1], (unsigned)zones[2],
		      (unsigned)zones[3], (unsigned)zones[4], (unsigned)zones[5],
		      (unsigned)zones[6], (unsigned)zones[7], (unsigned)zones[8]);
}

void exec_read_trace_minix_block(const char *kind, int block_index,
				 uint32_t zone_num, uint32_t disk_block,
				 uint32_t lba, size_t file_offset,
				 size_t req, size_t copied)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][MINIX] stage=block kind=%s blk_idx=0x%x zone=0x%x disk_block=0x%x lba=0x%x file_off=0x%llx req=0x%llx copied=0x%llx cache=none shared_buf=0",
		      kind ? kind : "?", (unsigned)block_index, (unsigned)zone_num,
		      (unsigned)disk_block, (unsigned)lba,
		      (unsigned long long)file_offset, (unsigned long long)req,
		      (unsigned long long)copied);
}

void exec_read_trace_minix_eio(const char *classify, const char *kind,
			       int block_index, uint32_t zone_num,
			       uint32_t disk_block, uint32_t lba,
			       size_t file_offset, void *buf)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][MINIX] stage=eio errno=-EIO kind=%s blk_idx=0x%x zone=0x%x disk_block=0x%x lba=0x%x file_off=0x%llx buf=0x%llx",
		      kind ? kind : "?", (unsigned)block_index, (unsigned)zone_num,
		      (unsigned)disk_block, (unsigned)lba,
		      (unsigned long long)file_offset,
		      (unsigned long long)(uintptr_t)buf);
	if (classify)
		klog_info_fmt("EXEC", "CLASSIFY %s", classify);
}

void exec_read_trace_device_read(uint32_t lba, uint8_t n_sectors, int ok,
				 void *buf)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][DEV] stage=read_sectors lba=0x%x n=0x%x ret=%s buf=0x%llx cache=none",
		      (unsigned)lba, (unsigned)n_sectors, ok ? "ok" : "fail",
		      (unsigned long long)(uintptr_t)buf);
	if (!ok)
		klog_info("EXEC", "CLASSIFY DEVICE_READ_FLAKE");
}
