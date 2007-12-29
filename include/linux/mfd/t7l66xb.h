/*
 * linux/include/asm-arm/hardware/t7l66xb.h
 *
 * This file contains the definitions for the T7L66XB
 *
 * (C) Copyright 2005 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef _ASM_ARCH_T7L66XB_SOC
#define _ASM_ARCH_T7L66XB_SOC

#include <linux/mfd-core.h>
#include <linux/mfd/tmio.h>


struct t7l66xb_platform_data
{
        int (*enable_clk32k)(struct platform_device *dev);
        int (*disable_clk32k)(struct platform_device *dev);

	int     (*enable)(struct platform_device *dev);
	int     (*disable)(struct platform_device *dev);
	int     (*suspend)(struct platform_device *dev);
	int     (*resume)(struct platform_device *dev);

	int     irq_base;       /* a base for cascaded irq */

	struct tmio_nand_data   *nand_data;
};


#define T7L66XB_NAND_CNF_BASE  (0x000100)
#define T7L66XB_NAND_CTL_BASE  (0x001000)

#define IRQ_T7L66XB_NAND       (3)
#define IRQ_T7L66XB_MMC        (1)
#define IRQ_T7L66XB_OHCI       (2)

#define T7L66XB_NR_IRQS	8

#endif
