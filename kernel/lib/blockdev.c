// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Block device registry and I/O facade.
 */

#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#define IR0_BLOCKDEV_MAX 8
#define IR0_BLOCKDEV_ID_BASE 0x00000100u

struct ir0_block_slot
{
	struct ir0_block_device dev;
	int used;
};

static struct ir0_block_slot ir0_block_slots[IR0_BLOCKDEV_MAX];
static int ir0_block_slot_count;
static dev_t ir0_block_next_id = IR0_BLOCKDEV_ID_BASE;
static int ir0_block_facade_classified;

static struct ir0_block_device *ir0_block_find(dev_t id)
{
	int i;

	for (i = 0; i < IR0_BLOCKDEV_MAX; i++)
	{
		if (ir0_block_slots[i].used && ir0_block_slots[i].dev.id == id)
			return &ir0_block_slots[i].dev;
	}
	return NULL;
}

static void ir0_block_emit_facade_ok(void)
{
	if (ir0_block_facade_classified)
		return;
	ir0_block_facade_classified = 1;
	klog_info("BLOCKDEV", "CLASSIFY BLOCKDEV_FACADE_OK");
	klog_info("BLOCKDEV", "CLASSIFY DEVICE_POLICY_NOT_IN_VFS");
}

int ir0_block_register(struct ir0_block_device *dev)
{
	int i;
	struct ir0_block_slot *slot;

	if (!dev || !dev->ops || !dev->ops->read)
		return -EINVAL;

	for (i = 0; i < IR0_BLOCKDEV_MAX; i++)
	{
		if (!ir0_block_slots[i].used)
			break;
		if (dev->info.name[0] &&
		    strncmp(ir0_block_slots[i].dev.info.name, dev->info.name,
			    IR0_BLOCKDEV_NAME_MAX) == 0)
			return -EEXIST;
	}
	if (i >= IR0_BLOCKDEV_MAX)
		return -ENOMEM;

	slot = &ir0_block_slots[i];
	slot->dev = *dev;
	if (slot->dev.id == 0)
	{
		slot->dev.id = ir0_block_next_id;
		ir0_block_next_id++;
	}
	if (slot->dev.info.sector_size == 0)
		slot->dev.info.sector_size = 512;
	if (slot->dev.info.max_sectors_per_io == 0)
		slot->dev.info.max_sectors_per_io = 1;
	slot->used = 1;
	if (i >= ir0_block_slot_count)
		ir0_block_slot_count = i + 1;
	ir0_block_emit_facade_ok();
	return 0;
}

dev_t ir0_block_lookup_by_name(const char *name)
{
	int i;

	if (!name)
		return 0;
	for (i = 0; i < IR0_BLOCKDEV_MAX; i++)
	{
		if (!ir0_block_slots[i].used)
			continue;
		if (strncmp(ir0_block_slots[i].dev.info.name, name,
			    IR0_BLOCKDEV_NAME_MAX) == 0)
			return ir0_block_slots[i].dev.id;
	}
	return 0;
}

bool ir0_block_is_present(dev_t dev)
{
	return ir0_block_find(dev) != NULL;
}

static int ir0_block_io(dev_t dev, uint64_t lba, uint32_t count, void *buf,
			int write_path)
{
	struct ir0_block_device *bd;
	uint32_t max_io;
	uint32_t done = 0;

	bd = ir0_block_find(dev);
	if (!bd || !buf || count == 0)
		return -EINVAL;
	if (write_path && !bd->ops->write)
		return -EROFS;
	if (write_path && (bd->info.flags & IR0_BLOCK_FLAG_READONLY))
		return -EROFS;

	max_io = bd->info.max_sectors_per_io;
	if (max_io == 0)
		max_io = 1;

	while (done < count)
	{
		uint32_t chunk = count - done;
		uint64_t chunk_lba = lba + done;
		size_t byte_off = (size_t)done * bd->info.sector_size;
		int ret;

		if (chunk > max_io)
			chunk = max_io;

		if (write_path)
		{
			ret = bd->ops->write(bd->ctx, chunk_lba, chunk,
					     (const uint8_t *)buf + byte_off);
		}
		else
		{
			ret = bd->ops->read(bd->ctx, chunk_lba, chunk,
					    (uint8_t *)buf + byte_off);
		}
		if (ret != 0)
			return ret;
		done += chunk;
	}
	return 0;
}

int ir0_block_read(dev_t dev, uint64_t lba, uint32_t count, void *buf)
{
	return ir0_block_io(dev, lba, count, buf, 0);
}

int ir0_block_write(dev_t dev, uint64_t lba, uint32_t count, const void *buf)
{
	if (!buf)
		return -EINVAL;
	return ir0_block_io(dev, lba, count, (void *)buf, 1);
}

int ir0_block_flush(dev_t dev)
{
	struct ir0_block_device *bd = ir0_block_find(dev);

	if (!bd)
		return -ENODEV;
	if (!bd->ops->flush)
		return 0;
	return bd->ops->flush(bd->ctx);
}

void ir0_block_flush_all(void)
{
	int i;

	for (i = 0; i < IR0_BLOCKDEV_MAX; i++)
	{
		dev_t id = ir0_block_dev_at(i);

		if (id)
			(void)ir0_block_flush(id);
	}
}

int ir0_block_get_info(dev_t dev, struct ir0_block_info *out)
{
	struct ir0_block_device *bd = ir0_block_find(dev);

	if (!bd || !out)
		return -EINVAL;
	*out = bd->info;
	return 0;
}

int ir0_block_count(void)
{
	return ir0_block_slot_count;
}

const char *ir0_block_name_at(int index)
{
	if (index < 0 || index >= IR0_BLOCKDEV_MAX || !ir0_block_slots[index].used)
		return NULL;
	return ir0_block_slots[index].dev.info.name;
}

dev_t ir0_block_dev_at(int index)
{
	if (index < 0 || index >= IR0_BLOCKDEV_MAX || !ir0_block_slots[index].used)
		return 0;
	return ir0_block_slots[index].dev.id;
}

const char *ir0_block_legacy_name(uint8_t disk_id)
{
	static const char *const legacy[] = { "hda", "hdb", "hdc", "hdd" };

	if (disk_id >= (sizeof(legacy) / sizeof(legacy[0])))
		return NULL;
	return legacy[disk_id];
}

int ir0_block_name_is_present(const char *name)
{
	if (!name)
		return 0;
	return ir0_block_lookup_by_name(name) != 0;
}

uint64_t ir0_block_sector_count_by_name(const char *name)
{
	struct ir0_block_info info;
	dev_t dev;

	if (!name)
		return 0;
	dev = ir0_block_lookup_by_name(name);
	if (dev == 0 || ir0_block_get_info(dev, &info) != 0)
		return 0;
	return info.sector_count;
}

int ir0_block_read_by_name(const char *name, uint64_t lba, uint32_t count,
			   void *buf)
{
	dev_t dev;

	if (!name || !buf || count == 0)
		return -EINVAL;
	dev = ir0_block_lookup_by_name(name);
	if (dev == 0)
		return -ENODEV;
	return ir0_block_read(dev, lba, count, buf);
}

int ir0_block_write_by_name(const char *name, uint64_t lba, uint32_t count,
			    const void *buf)
{
	dev_t dev;

	if (!name || !buf || count == 0)
		return -EINVAL;
	dev = ir0_block_lookup_by_name(name);
	if (dev == 0)
		return -ENODEV;
	return ir0_block_write(dev, lba, count, buf);
}

#ifdef TEST_HOST
void ir0_block_reset_for_test(void)
{
	memset(ir0_block_slots, 0, sizeof(ir0_block_slots));
	ir0_block_slot_count = 0;
	ir0_block_next_id = IR0_BLOCKDEV_ID_BASE;
	ir0_block_facade_classified = 0;
}
#endif
