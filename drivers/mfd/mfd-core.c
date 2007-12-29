/*
 * drivers/mfd/mfd-core.c
 *
 * core MFD support
 * Copyright (c) 2006 Ian Molton
 * Copyright (c) 2007 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mfd-core.h>

#define SIGNED_SHIFT(val, shift) ((shift) >= 0 ?	\
			((val) << (shift)) :		\
			((val) >> -(shift)))

int mfd_add_devices(
		struct platform_device *parent,
		const struct mfd_cell *cells, int n_devs,
		struct resource *mem,
		int relative_addr_shift,
		int irq_base)
{
	int i;

	for (i = 0; i < n_devs; i++) {
		struct resource *res = NULL;
		const struct mfd_cell *cell = cells + i;
		struct platform_device *pdev;
		int ret = -ENOMEM;
		int r;

		pdev = platform_device_alloc(cell->name, -1);
		if (!pdev)
			goto fail_alloc;

		pdev->dev.uevent_suppress = 0;
		pdev->dev.parent = &parent->dev;

		ret = platform_device_add_data(pdev, &cell, sizeof(struct mfd_cell *));
		if (ret)
			goto fail_device;

		res = kzalloc(cell->num_resources * sizeof(struct resource),
							GFP_KERNEL);
		if (!res)
			goto fail_device;

		for (r = 0; r < cell->num_resources; r++) {
			res[r].name = cell->resources[r].name;

			/* Find out base to use */
			if (cell->resources[r].flags & IORESOURCE_MEM) {
				res[r].parent = mem;
				res[r].start = mem->start +
					SIGNED_SHIFT(cell->resources[r].start,
							relative_addr_shift);
				res[r].end   = mem->start +
					SIGNED_SHIFT(cell->resources[r].end,
							relative_addr_shift);
			} else if ((cell->resources[r].flags & IORESOURCE_IRQ) &&
				(cell->resources[r].flags & IORESOURCE_IRQ_MFD_SUBDEVICE)) {
				res[r].start = irq_base +
					cell->resources[r].start;
				res[r].end   = irq_base +
					cell->resources[r].end;
			} else {
				res[r].start = cell->resources[r].start;
				res[r].end   = cell->resources[r].end;
			}

			res[r].flags = cell->resources[r].flags;
		}

		ret = platform_device_add_resources(pdev,
				res,
				cell->num_resources);
		kfree(res);

		if (ret)
			goto fail_device;

		ret = platform_device_add(pdev);

		if (ret) {
			platform_device_del(pdev);
fail_device:
			platform_device_put(pdev);
fail_alloc:
			mfd_remove_devices(parent);
			return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL(mfd_add_devices);

static int mfd_remove_devices_fn(struct device *dev, void *unused)
{
	platform_device_unregister(container_of(dev, struct platform_device, dev));
	return 0;
}

void mfd_remove_devices(struct platform_device *parent)
{
	device_for_each_child(&parent->dev, NULL, mfd_remove_devices_fn);
}
EXPORT_SYMBOL(mfd_remove_devices);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ian Molton, Dmitry Baryshkov");
