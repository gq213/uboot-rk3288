// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_BLK

#include <common.h>
#include <blk.h>
#include <dm.h>
#include <log.h>
#include <malloc.h>
#include <part.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/uclass-internal.h>
#include <linux/err.h>

static struct {
	enum uclass_id id;
	const char *name;
} uclass_idname_str[] = {
	{ UCLASS_IDE, "ide" },
	{ UCLASS_SCSI, "scsi" },
	{ UCLASS_USB, "usb" },
	{ UCLASS_MMC,  "mmc" },
	{ UCLASS_AHCI, "sata" },
	{ UCLASS_ROOT, "host" },
	{ UCLASS_NVME, "nvme" },
	{ UCLASS_EFI_MEDIA, "efi" },
	{ UCLASS_EFI_LOADER, "efiloader" },
	{ UCLASS_VIRTIO, "virtio" },
	{ UCLASS_PVBLOCK, "pvblock" },
};

static enum uclass_id uclass_name_to_iftype(const char *uclass_idname)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(uclass_idname_str); i++) {
		if (!strcmp(uclass_idname, uclass_idname_str[i].name))
			return uclass_idname_str[i].id;
	}

	return UCLASS_INVALID;
}

static enum uclass_id conv_uclass_id(enum uclass_id uclass_id)
{
	/*
	 * This strange adjustment is used because we use UCLASS_MASS_STORAGE
	 * for USB storage devices, so need to return this as the uclass to
	 * use for USB. In fact USB_UCLASS is for USB controllers, not
	 * peripherals.
	 *
	 * The name of the UCLASS_MASS_STORAGE uclass driver is
	 * "usb_mass_storage", but we want to use "usb" in things like the
	 * 'part list' command and when showing interfaces.
	 *
	 * So for now we have this one-way conversion.
	 *
	 * The fix for this is possibly to:
	 *    - rename UCLASS_MASS_STORAGE name to "usb"
	 *    - rename UCLASS_USB name to "usb_ctlr"
	 *    - use UCLASS_MASS_STORAGE instead of UCLASS_USB in if_typename_str
	 */
	if (uclass_id == UCLASS_USB)
		return UCLASS_MASS_STORAGE;
	return uclass_id;
}

const char *blk_get_uclass_name(enum uclass_id uclass_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(uclass_idname_str); i++) {
		if (uclass_idname_str[i].id == uclass_id)
			return uclass_idname_str[i].name;
	}

	return "(none)";
}

struct blk_desc *blk_get_devnum_by_uclass_id(enum uclass_id uclass_id, int devnum)
{
	struct blk_desc *desc;
	struct udevice *dev;
	int ret;

	ret = blk_get_device(uclass_id, devnum, &dev);
	if (ret)
		return NULL;
	desc = dev_get_uclass_plat(dev);

	return desc;
}

/*
 * This function is complicated with driver model. We look up the interface
 * name in a local table. This gives us an interface type which we can match
 * against the uclass of the block device's parent.
 */
struct blk_desc *blk_get_devnum_by_uclass_idname(const char *uclass_idname, int devnum)
{
	enum uclass_id uclass_id;
	enum uclass_id type;
	struct udevice *dev;
	struct uclass *uc;
	int ret;

	type = uclass_name_to_iftype(uclass_idname);
	if (type == UCLASS_INVALID) {
		debug("%s: Unknown interface type '%s'\n", __func__,
		      uclass_idname);
		return NULL;
	}
	uclass_id = conv_uclass_id(type);
	if (uclass_id == UCLASS_INVALID) {
		debug("%s: Unknown uclass for interface type'\n",
		      blk_get_uclass_name(type));
		return NULL;
	}

	ret = uclass_get(UCLASS_BLK, &uc);
	if (ret)
		return NULL;
	uclass_foreach_dev(dev, uc) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		debug("%s: uclass_id=%d, devnum=%d: %s, %d, %d\n", __func__,
		      type, devnum, dev->name, desc->uclass_id, desc->devnum);
		if (desc->devnum != devnum)
			continue;

		/* Find out the parent device uclass */
		if (device_get_uclass_id(dev->parent) != uclass_id) {
			debug("%s: parent uclass %d, this dev %d\n", __func__,
			      device_get_uclass_id(dev->parent), uclass_id);
			continue;
		}

		if (device_probe(dev))
			return NULL;

		debug("%s: Device desc %p\n", __func__, desc);
		return desc;
	}
	debug("%s: No device found\n", __func__);

	return NULL;
}

/**
 * blk_get_by_device() - Get the block device descriptor for the given device
 * @dev:	Instance of a storage device
 *
 * Return: With block device descriptor on success , NULL if there is no such
 *	   block device.
 */
struct blk_desc *blk_get_by_device(struct udevice *dev)
{
	struct udevice *child_dev;

	device_foreach_child(child_dev, dev) {
		if (device_get_uclass_id(child_dev) != UCLASS_BLK)
			continue;

		return dev_get_uclass_plat(child_dev);
	}

	debug("%s: No block device found\n", __func__);

	return NULL;
}

/**
 * get_desc() - Get the block device descriptor for the given device number
 *
 * @uclass_id:	Interface type
 * @devnum:	Device number (0 = first)
 * @descp:	Returns block device descriptor on success
 * Return: 0 on success, -ENODEV if there is no such device and no device
 * with a higher device number, -ENOENT if there is no such device but there
 * is one with a higher number, or other -ve on other error.
 */
static int get_desc(enum uclass_id uclass_id, int devnum, struct blk_desc **descp)
{
	bool found_more = false;
	struct udevice *dev;
	struct uclass *uc;
	int ret;

	*descp = NULL;
	ret = uclass_get(UCLASS_BLK, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev(dev, uc) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		debug("%s: uclass_id=%d, devnum=%d: %s, %d, %d\n", __func__,
		      uclass_id, devnum, dev->name, desc->uclass_id, desc->devnum);
		if (desc->uclass_id == uclass_id) {
			if (desc->devnum == devnum) {
				ret = device_probe(dev);
				if (ret)
					return ret;

				*descp = desc;
				return 0;
			} else if (desc->devnum > devnum) {
				found_more = true;
			}
		}
	}

	return found_more ? -ENOENT : -ENODEV;
}

int blk_select_hwpart_devnum(enum uclass_id uclass_id, int devnum, int hwpart)
{
	struct udevice *dev;
	int ret;

	ret = blk_get_device(uclass_id, devnum, &dev);
	if (ret)
		return ret;

	return blk_select_hwpart(dev, hwpart);
}

int blk_list_part(enum uclass_id uclass_id)
{
	struct blk_desc *desc;
	int devnum, ok;
	int ret;

	for (ok = 0, devnum = 0;; ++devnum) {
		ret = get_desc(uclass_id, devnum, &desc);
		if (ret == -ENODEV)
			break;
		else if (ret)
			continue;
		if (desc->part_type != PART_TYPE_UNKNOWN) {
			++ok;
			if (devnum)
				putc('\n');
			part_print(desc);
		}
	}
	if (!ok)
		return -ENODEV;

	return 0;
}

int blk_print_part_devnum(enum uclass_id uclass_id, int devnum)
{
	struct blk_desc *desc;
	int ret;

	ret = get_desc(uclass_id, devnum, &desc);
	if (ret)
		return ret;
	if (desc->type == DEV_TYPE_UNKNOWN)
		return -ENOENT;
	part_print(desc);

	return 0;
}

void blk_list_devices(enum uclass_id uclass_id)
{
	struct blk_desc *desc;
	int ret;
	int i;

	for (i = 0;; ++i) {
		ret = get_desc(uclass_id, i, &desc);
		if (ret == -ENODEV)
			break;
		else if (ret)
			continue;
		if (desc->type == DEV_TYPE_UNKNOWN)
			continue;  /* list only known devices */
		printf("Device %d: ", i);
		dev_print(desc);
	}
}

int blk_print_device_num(enum uclass_id uclass_id, int devnum)
{
	struct blk_desc *desc;
	int ret;

	ret = get_desc(uclass_id, devnum, &desc);
	if (ret)
		return ret;
	printf("\nIDE device %d: ", devnum);
	dev_print(desc);

	return 0;
}

int blk_show_device(enum uclass_id uclass_id, int devnum)
{
	struct blk_desc *desc;
	int ret;

	printf("\nDevice %d: ", devnum);
	ret = get_desc(uclass_id, devnum, &desc);
	if (ret == -ENODEV || ret == -ENOENT) {
		printf("unknown device\n");
		return -ENODEV;
	}
	if (ret)
		return ret;
	dev_print(desc);

	if (desc->type == DEV_TYPE_UNKNOWN)
		return -ENOENT;

	return 0;
}

ulong blk_read_devnum(enum uclass_id uclass_id, int devnum, lbaint_t start,
		      lbaint_t blkcnt, void *buffer)
{
	struct blk_desc *desc;
	ulong n;
	int ret;

	ret = get_desc(uclass_id, devnum, &desc);
	if (ret)
		return ret;
	n = blk_dread(desc, start, blkcnt, buffer);
	if (IS_ERR_VALUE(n))
		return n;

	return n;
}

ulong blk_write_devnum(enum uclass_id uclass_id, int devnum, lbaint_t start,
		       lbaint_t blkcnt, const void *buffer)
{
	struct blk_desc *desc;
	int ret;

	ret = get_desc(uclass_id, devnum, &desc);
	if (ret)
		return ret;
	return blk_dwrite(desc, start, blkcnt, buffer);
}

int blk_select_hwpart(struct udevice *dev, int hwpart)
{
	const struct blk_ops *ops = blk_get_ops(dev);

	if (!ops)
		return -ENOSYS;
	if (!ops->select_hwpart)
		return 0;

	return ops->select_hwpart(dev, hwpart);
}

int blk_dselect_hwpart(struct blk_desc *desc, int hwpart)
{
	return blk_select_hwpart(desc->bdev, hwpart);
}

int blk_first_device(int uclass_id, struct udevice **devp)
{
	struct blk_desc *desc;
	int ret;

	ret = uclass_find_first_device(UCLASS_BLK, devp);
	if (ret)
		return ret;
	if (!*devp)
		return -ENODEV;
	do {
		desc = dev_get_uclass_plat(*devp);
		if (desc->uclass_id == uclass_id)
			return 0;
		ret = uclass_find_next_device(devp);
		if (ret)
			return ret;
	} while (*devp);

	return -ENODEV;
}

int blk_next_device(struct udevice **devp)
{
	struct blk_desc *desc;
	int ret, uclass_id;

	desc = dev_get_uclass_plat(*devp);
	uclass_id = desc->uclass_id;
	do {
		ret = uclass_find_next_device(devp);
		if (ret)
			return ret;
		if (!*devp)
			return -ENODEV;
		desc = dev_get_uclass_plat(*devp);
		if (desc->uclass_id == uclass_id)
			return 0;
	} while (1);
}

int blk_find_device(int uclass_id, int devnum, struct udevice **devp)
{
	struct uclass *uc;
	struct udevice *dev;
	int ret;

	ret = uclass_get(UCLASS_BLK, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev(dev, uc) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		debug("%s: uclass_id=%d, devnum=%d: %s, %d, %d\n", __func__,
		      uclass_id, devnum, dev->name, desc->uclass_id, desc->devnum);
		if (desc->uclass_id == uclass_id && desc->devnum == devnum) {
			*devp = dev;
			return 0;
		}
	}

	return -ENODEV;
}

int blk_get_device(int uclass_id, int devnum, struct udevice **devp)
{
	int ret;

	ret = blk_find_device(uclass_id, devnum, devp);
	if (ret)
		return ret;

	return device_probe(*devp);
}

unsigned long blk_dread(struct blk_desc *block_dev, lbaint_t start,
			lbaint_t blkcnt, void *buffer)
{
	struct udevice *dev = block_dev->bdev;
	const struct blk_ops *ops = blk_get_ops(dev);
	ulong blks_read;

	if (!ops->read)
		return -ENOSYS;

	if (blkcache_read(block_dev->uclass_id, block_dev->devnum,
			  start, blkcnt, block_dev->blksz, buffer))
		return blkcnt;
	blks_read = ops->read(dev, start, blkcnt, buffer);
	if (blks_read == blkcnt)
		blkcache_fill(block_dev->uclass_id, block_dev->devnum,
			      start, blkcnt, block_dev->blksz, buffer);

	return blks_read;
}

unsigned long blk_dwrite(struct blk_desc *block_dev, lbaint_t start,
			 lbaint_t blkcnt, const void *buffer)
{
	struct udevice *dev = block_dev->bdev;
	const struct blk_ops *ops = blk_get_ops(dev);

	if (!ops->write)
		return -ENOSYS;

	blkcache_invalidate(block_dev->uclass_id, block_dev->devnum);
	return ops->write(dev, start, blkcnt, buffer);
}

unsigned long blk_derase(struct blk_desc *block_dev, lbaint_t start,
			 lbaint_t blkcnt)
{
	struct udevice *dev = block_dev->bdev;
	const struct blk_ops *ops = blk_get_ops(dev);

	if (!ops->erase)
		return -ENOSYS;

	blkcache_invalidate(block_dev->uclass_id, block_dev->devnum);
	return ops->erase(dev, start, blkcnt);
}

int blk_get_from_parent(struct udevice *parent, struct udevice **devp)
{
	struct udevice *dev;
	enum uclass_id id;
	int ret;

	device_find_first_child(parent, &dev);
	if (!dev) {
		debug("%s: No block device found for parent '%s'\n", __func__,
		      parent->name);
		return -ENODEV;
	}
	id = device_get_uclass_id(dev);
	if (id != UCLASS_BLK) {
		debug("%s: Incorrect uclass %s for block device '%s'\n",
		      __func__, uclass_get_name(id), dev->name);
		return -ENOTBLK;
	}
	ret = device_probe(dev);
	if (ret)
		return ret;
	*devp = dev;

	return 0;
}

const char *blk_get_devtype(struct udevice *dev)
{
	struct udevice *parent = dev_get_parent(dev);

	return uclass_get_name(device_get_uclass_id(parent));
};

int blk_find_max_devnum(enum uclass_id uclass_id)
{
	struct udevice *dev;
	int max_devnum = -ENODEV;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_BLK, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev(dev, uc) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		if (desc->uclass_id == uclass_id && desc->devnum > max_devnum)
			max_devnum = desc->devnum;
	}

	return max_devnum;
}

int blk_next_free_devnum(enum uclass_id uclass_id)
{
	int ret;

	ret = blk_find_max_devnum(uclass_id);
	if (ret == -ENODEV)
		return 0;
	if (ret < 0)
		return ret;

	return ret + 1;
}

static int blk_flags_check(struct udevice *dev, enum blk_flag_t req_flags)
{
	const struct blk_desc *desc = dev_get_uclass_plat(dev);
	enum blk_flag_t flags;

	flags = desc->removable ? BLKF_REMOVABLE : BLKF_FIXED;

	return flags & req_flags ? 0 : 1;
}

int blk_find_first(enum blk_flag_t flags, struct udevice **devp)
{
	int ret;

	for (ret = uclass_find_first_device(UCLASS_BLK, devp);
	     *devp && !blk_flags_check(*devp, flags);
	     ret = uclass_find_next_device(devp))
		return 0;

	return -ENODEV;
}

int blk_find_next(enum blk_flag_t flags, struct udevice **devp)
{
	int ret;

	for (ret = uclass_find_next_device(devp);
	     *devp && !blk_flags_check(*devp, flags);
	     ret = uclass_find_next_device(devp))
		return 0;

	return -ENODEV;
}

int blk_first_device_err(enum blk_flag_t flags, struct udevice **devp)
{
	int ret;

	for (ret = uclass_first_device_err(UCLASS_BLK, devp);
	     !ret;
	     ret = uclass_next_device_err(devp)) {
		if (!blk_flags_check(*devp, flags))
			return 0;
	}

	return -ENODEV;
}

int blk_next_device_err(enum blk_flag_t flags, struct udevice **devp)
{
	int ret;

	for (ret = uclass_next_device_err(devp);
	     !ret;
	     ret = uclass_next_device_err(devp)) {
		if (!blk_flags_check(*devp, flags))
			return 0;
	}

	return -ENODEV;
}

int blk_count_devices(enum blk_flag_t flag)
{
	struct udevice *dev;
	int count = 0;

	blk_foreach_probe(flag, dev)
		count++;

	return count;
}

static int blk_claim_devnum(enum uclass_id uclass_id, int devnum)
{
	struct udevice *dev;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_BLK, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev(dev, uc) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		if (desc->uclass_id == uclass_id && desc->devnum == devnum) {
			int next = blk_next_free_devnum(uclass_id);

			if (next < 0)
				return next;
			desc->devnum = next;
			return 0;
		}
	}

	return -ENOENT;
}

int blk_create_device(struct udevice *parent, const char *drv_name,
		      const char *name, int uclass_id, int devnum, int blksz,
		      lbaint_t lba, struct udevice **devp)
{
	struct blk_desc *desc;
	struct udevice *dev;
	int ret;

	if (devnum == -1) {
		devnum = blk_next_free_devnum(uclass_id);
	} else {
		ret = blk_claim_devnum(uclass_id, devnum);
		if (ret < 0 && ret != -ENOENT)
			return ret;
	}
	if (devnum < 0)
		return devnum;
	ret = device_bind_driver(parent, drv_name, name, &dev);
	if (ret)
		return ret;
	desc = dev_get_uclass_plat(dev);
	desc->uclass_id = uclass_id;
	desc->blksz = blksz;
	desc->log2blksz = LOG2(desc->blksz);
	desc->lba = lba;
	desc->part_type = PART_TYPE_UNKNOWN;
	desc->bdev = dev;
	desc->devnum = devnum;
	*devp = dev;

	return 0;
}

int blk_create_devicef(struct udevice *parent, const char *drv_name,
		       const char *name, int uclass_id, int devnum, int blksz,
		       lbaint_t lba, struct udevice **devp)
{
	char dev_name[30], *str;
	int ret;

	snprintf(dev_name, sizeof(dev_name), "%s.%s", parent->name, name);
	str = strdup(dev_name);
	if (!str)
		return -ENOMEM;

	ret = blk_create_device(parent, drv_name, str, uclass_id, devnum,
				blksz, lba, devp);
	if (ret) {
		free(str);
		return ret;
	}
	device_set_name_alloced(*devp);

	return 0;
}

int blk_probe_or_unbind(struct udevice *dev)
{
	int ret;

	ret = device_probe(dev);
	if (ret) {
		log_debug("probing %s failed\n", dev->name);
		device_unbind(dev);
	}

	return ret;
}

int blk_unbind_all(int uclass_id)
{
	struct uclass *uc;
	struct udevice *dev, *next;
	int ret;

	ret = uclass_get(UCLASS_BLK, &uc);
	if (ret)
		return ret;
	uclass_foreach_dev_safe(dev, next, uc) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		if (desc->uclass_id == uclass_id) {
			ret = device_remove(dev, DM_REMOVE_NORMAL);
			if (ret)
				return ret;
			ret = device_unbind(dev);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int blk_post_probe(struct udevice *dev)
{
	if (CONFIG_IS_ENABLED(PARTITIONS) && blk_enabled()) {
		struct blk_desc *desc = dev_get_uclass_plat(dev);

		part_init(desc);

		if (desc->part_type != PART_TYPE_UNKNOWN &&
		    part_create_block_devices(dev))
			debug("*** creating partitions failed\n");
	}

	return 0;
}

UCLASS_DRIVER(blk) = {
	.id		= UCLASS_BLK,
	.name		= "blk",
	.post_probe	= blk_post_probe,
	.per_device_plat_auto	= sizeof(struct blk_desc),
};
