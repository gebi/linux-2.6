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

#include <linux/mfd/t7l66xb.h>

static struct resource e740_t7l66xb_resources[] = {
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

static int e740_t7l66xb_enable(struct platform_device *dev) {

	GPCR(19) = GPIO_bit(19); // #SUSPEND low
	GPCR(7) = GPIO_bit(7); // #PCLR low (reset)
	mdelay(10);
	GPSR(19) = GPIO_bit(19); // #SUSPEND high
	mdelay(10);
	GPSR(7) = GPIO_bit(7); // #PCLR high
	mdelay(10);
	return 0;
}

static int e740_t7l66xb_suspend(struct platform_device *dev) {
	GPCR(19) = GPIO_bit(19); // #SUSPEND low
	mdelay(10);
	
	return 0;
}

static int e740_t7l66xb_resume(struct platform_device *dev) {
	GPSR(19) = GPIO_bit(19); // #SUSPEND high
	mdelay(10);
	return 0;
}

static int e740_t7l66xb_enable_clk32k(struct platform_device *dev) {
        pxa_gpio_mode(GPIO12_32KHz_MD);
        mdelay(10);

        return 0;
}

static struct t7l66xb_platform_data e740_t7l66xb_info = {
	.irq_base 		= IRQ_BOARD_START,
	.enable_clk32k          = &e740_t7l66xb_enable_clk32k,
	.enable                 = &e740_t7l66xb_enable,
	.suspend                = &e740_t7l66xb_suspend,
	.resume                 = &e740_t7l66xb_resume,
};

static struct platform_device e740_t7l66xb_device = {
	.name           = "t7l66xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e740_t7l66xb_info, 
	},
	.num_resources = ARRAY_SIZE(e740_t7l66xb_resources),
	.resource      = e740_t7l66xb_resources,
}; 

static int __init e740_t7l66xb_init(void)
{
	if(!machine_is_e740())
		return -ENODEV;

	platform_device_register(&e740_t7l66xb_device);
	return 0;
}

module_init(e740_t7l66xb_init);

MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("e740 t7l66xb device support");
MODULE_LICENSE("GPLv2");


