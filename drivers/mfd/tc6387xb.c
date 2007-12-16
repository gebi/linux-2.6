/*
 * Toshiba TC6387XB support
 * Copyright (c) 2005 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains TC6387XB base support.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>

#include <linux/mfd-core.h>
#include <linux/mfd/tc6387xb.h>

#ifdef CONFIG_PM
static int tc6387xb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct tc6387xb_platform_data *pdata = platform_get_drvdata(dev);

	if (pdata && pdata->suspend)
		pdata->suspend(dev);

	return 0;
}

static int tc6387xb_resume(struct platform_device *dev)
{
	struct tc6387xb_platform_data *pdata = platform_get_drvdata(dev);

	if (pdata && pdata->resume)
		pdata->resume(dev);

	return 0;
}
#else
#define tc6387xb_suspend  NULL
#define tc6387xb_resume   NULL
#endif

/*--------------------------------------------------------------------------*/

static int tc6387xb_mmc_enable(struct platform_device *mmc) {
	struct platform_device *dev      = to_platform_device(mmc->dev.parent);
	struct tc6387xb_platform_data *tc6387xb = dev->dev.platform_data;

	if(tc6387xb->enable_mmc_clock)
		tc6387xb->enable_mmc_clock(dev);

	return 0;
}

static int tc6387xb_mmc_disable(struct platform_device *mmc) {
	struct platform_device *dev      = to_platform_device(mmc->dev.parent);
	struct tc6387xb_platform_data *tc6387xb = dev->dev.platform_data;

	if(tc6387xb->disable_mmc_clock)
		tc6387xb->disable_mmc_clock(dev);

	return 0;
}


/*--------------------------------------------------------------------------*/

static struct resource tc6387xb_mmc_resources[] = {
	{
		.name = TMIO_MMC_CONTROL,
		.start = 0x800,
		.end   = 0x9ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = TMIO_MMC_CONFIG,
		.start = 0x200,
		.end   = 0x2ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = TMIO_MMC_IRQ,
		.start = 0,
		.end   = 0,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_MFD_SUBDEVICE,
	},
};

static struct mfd_cell tc6387xb_cells[] = {
	{
		.name = "tmio-mmc",
		.enable = tc6387xb_mmc_enable,
		.disable = tc6387xb_mmc_disable,
		.num_resources = ARRAY_SIZE(tc6387xb_mmc_resources),
		.resources = tc6387xb_mmc_resources,
	},
};

static int tc6387xb_probe(struct platform_device *dev)
{
	struct tc6387xb_platform_data *data = platform_get_drvdata(dev);
	struct resource *iomem;
	int irq;

	iomem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -EINVAL;

	irq   = platform_get_irq(dev, 0);

	if(data && data->enable)
		data->enable(dev);

	printk(KERN_INFO "Toshiba tc6393xb initialised\n");

	return mfd_add_devices(dev, tc6387xb_cells, ARRAY_SIZE(tc6387xb_cells),
	                       iomem, 0, irq);
}

static int tc6387xb_remove(struct platform_device *dev)
{
	struct tc6387xb_platform_data *data = platform_get_drvdata(dev);

	if(data && data->disable)
		data->disable(dev);

	return 0;
}


static struct platform_driver tc6387xb_platform_driver = {
	.driver = {
		.name		= "tc6387xb",
	},
	.probe		= tc6387xb_probe,
	.remove		= tc6387xb_remove,
	.suspend        = tc6387xb_suspend,
	.resume         = tc6387xb_resume,
};


static int __init tc6387xb_init(void)
{
	return platform_driver_register (&tc6387xb_platform_driver);
}

static void __exit tc6387xb_exit(void)
{
	platform_driver_unregister(&tc6387xb_platform_driver);
}

module_init(tc6387xb_init);
module_exit(tc6387xb_exit);

MODULE_DESCRIPTION("Toshiba TC6387XB core driver");
MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("Ian Molton");
