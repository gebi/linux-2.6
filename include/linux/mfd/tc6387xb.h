/*
 * linux/include/asm-arm/hardware/tc6387xb.h
 *
 * This file contains the definitions for the TC6393XB
 *
 * (C) Copyright 2005 Ian Molton <spyro@f2s.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 */
#ifndef MFD_T7L66XB_H
#define MFD_T7L66XB_H

#include <linux/mfd-core.h>
#include <linux/mfd/tmio.h>

struct tc6387xb_platform_data
{
	int (*enable_mmc_clock)(struct platform_device *dev);
	int (*disable_mmc_clock)(struct platform_device *dev);
	int (*enable)(struct platform_device *dev);
	int (*disable)(struct platform_device *dev);
	int (*suspend)(struct platform_device *dev);
	int (*resume)(struct platform_device *dev);
};

#endif
