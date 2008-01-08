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

#include <asm/mach-types.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>

#include <asm/arch/eseries-irq.h>
#include <asm/arch/eseries-gpio.h>

#include <linux/mfd/tc6393xb.h>

static struct resource e750_tc6393xb_resources[] = {
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

static int e750_tc6393xb_enable(struct platform_device *dev) {
//	pxa_gpio_mode(GPIO11_3_6MHz_MD);

	GPCR(45) = GPIO_bit(45); /* #SUSPEND low      */
	GPCR(19) = GPIO_bit(19); /* #PCLR low */
	mdelay(1);
	GPSR(45) = GPIO_bit(45); /* #SUSPEND high  (reset)   */
	mdelay(10);
	GPSR(19) = GPIO_bit(19); /* #PCLR high        */
	return 0;
}

static int e750_tc6393xb_disable(struct platform_device *dev) {
	GPCR(45) = GPIO_bit(45); /* #SUSPEND high */
	mdelay(10);
	GPCR(19) = GPIO_bit(19); /* #PCLR high    */
	
//	pxa_gpio_mode(GPIO11_3_6MHz_MD|GPIO_OUT);
//	GPSR0 = GPIO_bit(GPIO11_3_6MHz);
	return 0;
}

static int e750_tc6393xb_suspend(struct platform_device *dev) {
        //GPCR(45) = GPIO_bit(45); // #SUSPEND low
        mdelay(10);

        return 0;
}

static int e750_tc6393xb_resume(struct platform_device *dev) {
       // GPSR(45) = GPIO_bit(45); // #SUSPEND high
        mdelay(10);
        return 0;
}

/*
 * These values obtained by reverse engineering.
 * MCR : 80aa
 * CCR: 1310
 * PLL2CR: 0c01
 * PLL1CR1: f743
 * PLL1CR2: 00f2
 * SYS_DCR: 1033
 */

static struct tc6393xb_platform_data e750_tc6393xb_info = {
	.irq_base 		= IRQ_BOARD_START,
	.scr_pll2cr             = 0x0cc1,
	.scr_ccr                = 0x1310,
	.scr_mcr                = 0x80aa,
	.scr_gper		= 0,
	.scr_gpo_dsr            = 0,
	.scr_gpo_doecr          = 0,
	.suspend                = &e750_tc6393xb_suspend,
	.resume                 = &e750_tc6393xb_resume,
	.enable                 = &e750_tc6393xb_enable,
	.disable                = &e750_tc6393xb_disable,
};

static struct platform_device e750_tc6393xb_device = {
	.name           = "tc6393xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e750_tc6393xb_info, 
	},
	.num_resources = ARRAY_SIZE(e750_tc6393xb_resources),
	.resource      = e750_tc6393xb_resources,
}; 

static int __init e750_tc6393xb_init(void)
{
	if(!(machine_is_e750() || machine_is_e800()))
		return -ENODEV;

	platform_device_register(&e750_tc6393xb_device);
	return 0;
}

module_init(e750_tc6393xb_init);

MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("e750 and e800 tc6393 device support");
MODULE_LICENSE("GPLv2");

