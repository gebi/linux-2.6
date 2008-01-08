/*
 * (c) 2005 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>

#include <asm/arch/eseries-irq.h>
#include <asm/arch/eseries-gpio.h>

#include <linux/mfd/t7l66xb.h>

static struct resource e400_t7l66xb_resources[] = {
	[0] = {
		.start  = PXA_CS4_PHYS,
		.end    = PXA_CS4_PHYS + 0x1fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_GPIO(GPIO_ESERIES_TMIO_IRQ),
		.end    = IRQ_GPIO(GPIO_ESERIES_TMIO_IRQ),
		.flags  = IORESOURCE_IRQ,
	},

};

static struct mtd_partition partition_a = {
        .name = "Internal NAND flash",
        .offset =  0,
        .size =  MTDPART_SIZ_FULL,
};

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr e400_tc6393xb_nand_bbt = {
        .options = 0,
        .offs = 4,
        .len = 2,
        .pattern = scan_ff_pattern
};

static struct tmio_nand_data e400_tc6393xb_nand_config = {
        .num_partitions = 1,
        .partition = &partition_a,
        .badblock_pattern = &e400_tc6393xb_nand_bbt,
};

static int e400_t7l66xb_enable(struct platform_device *dev) {

//	GPCR(45) = GPIO_bit(45); // #SUSPEND low
//	GPCR(19) = GPIO_bit(19); // #PCLR low (reset)
	mdelay(10);
//	GPSR(45) = GPIO_bit(45); // #SUSPEND high
	mdelay(10);
//	GPSR(19) = GPIO_bit(19); // #PCLR high
	mdelay(10);
	return 0;
}

static int e400_t7l66xb_suspend(struct platform_device *dev) {
//	GPCR(45) = GPIO_bit(45); // #SUSPEND low
	mdelay(10);
	
	return 0;
}

static int e400_t7l66xb_resume(struct platform_device *dev) {
//	GPSR(45) = GPIO_bit(45); // #SUSPEND high
	mdelay(10);
	return 0;
}

static int e400_t7l66xb_enable_clk32k(struct platform_device *dev) {
        pxa_gpio_mode(GPIO12_32KHz_MD);
        mdelay(10);

        return 0;
}

static struct t7l66xb_platform_data e400_t7l66xb_info = {
	.irq_base 		= IRQ_BOARD_START,
	.enable_clk32k          = &e400_t7l66xb_enable_clk32k,
	.enable                 = &e400_t7l66xb_enable,
	.suspend                = &e400_t7l66xb_suspend,
	.resume                 = &e400_t7l66xb_resume,

	.nand_data              = &e400_tc6393xb_nand_config,
};

static struct platform_device e400_t7l66xb_device = {
	.name           = "t7l66xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e400_t7l66xb_info, 
	},
	.num_resources = ARRAY_SIZE(e400_t7l66xb_resources),
	.resource      = e400_t7l66xb_resources,
}; 

static int __init e400_t7l66xb_init(void)
{
	if(!machine_is_e400() && !machine_is_e350())
		return -ENODEV;

	platform_device_register(&e400_t7l66xb_device);
	return 0;
}

module_init(e400_t7l66xb_init);

MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("e740 t7l66xb device support");
MODULE_LICENSE("GPLv2");


