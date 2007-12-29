/*
 * Toshiba TC6393XB SoC support
 *
 * Copyright(c) 2005-2006 Chris Humbert
 * Copyright(c) 2005 Dirk Opfer
 * Copyright(c) 2005 Ian Molton <spyro@f2s.com>
 * Copyright(c) 2007 Dmitry Baryshkov
 *
 * Based on code written by Sharp/Lineo for 2.4 kernels
 * Based on locomo.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TC6393XB_H
#define TC6393XB_H

#include <linux/mfd-core.h>
#include <linux/mfd/tmio.h>

union tc6393xb_scr_fer {
	u8		raw;
struct {
	unsigned	usben:1;	/* D0	USB enable		*/
	unsigned	lcdcven:1;	/* D1	polysylicon TFT enable	*/
	unsigned	slcden:1;	/* D2	SLCD enable		*/
} __attribute__ ((packed)) bits;
} __attribute__ ((packed));

union tc6393xb_scr_ccr {
	u16		raw;
struct {
	unsigned	ck32ken:1;	/* D0	SD host clock enable	*/
	unsigned	usbcken:1;	/* D1	USB host clock enable	*/
	unsigned	x00:2;
	unsigned	sharp:1;	/* D4	??? set in Sharp's code	*/
	unsigned	x01:3;
	enum {				disable	= 0,
					m12MHz	= 1,
					m24MHz	= 2,
					m48MHz	= 3,
	}		mclksel:3;	/* D10-D8  LCD controller clock	*/
	unsigned	x02:1;
	enum {				h24MHz	= 0,
					h48MHz	= 1,
	}		hclksel:2;	/* D13-D12 host bus clock	*/
	unsigned	x03:2;
} __attribute__ ((packed)) bits;
} __attribute__ ((packed));

enum pincontrol {
	opendrain	= 0,
	tristate	= 1,
	pushpull	= 2,
	/* reserved	= 3, */
};

union tc6393xb_scr_mcr {
	u16		raw;
struct {
	enum pincontrol	rdyst:2;	/* D1-D0   HRDY control		*/
	unsigned	x00:1;
	unsigned	aren:1;		/* D3	   HRDY pull up resistance cut off */
	enum pincontrol	intst:2;	/* D5-D4   #HINT control	*/
	unsigned	x01:1;
	unsigned	aien:1;		/* D7      #HINT pull up resitance cut off */
	unsigned	x02:8;
} __attribute__ ((packed)) bits;
} __attribute__ ((packed));

struct tc6393xb_platform_data {
	u16	scr_pll2cr;	/* PLL2 Control */
	union tc6393xb_scr_ccr	scr_ccr;	/* Clock Control */
	union tc6393xb_scr_mcr	scr_mcr;	/* Mode Control */
	u16	scr_gper;	/* GP Enable */
	u32	scr_gpo_doecr;	/* GPO Data OE Control */
	u32	scr_gpo_dsr;	/* GPO Data Set */

	int	(*enable)(struct platform_device *dev);
	int	(*disable)(struct platform_device *dev);
	int	(*suspend)(struct platform_device *dev);
	int	(*resume)(struct platform_device *dev);

	int	irq_base;	/* a base for cascaded irq */

	struct tmio_nand_data	*nand_data;
	struct tmio_fb_data	*fb_data;
};

extern int tc6393xb_lcd_set_power(struct platform_device *fb_dev, bool on);
extern int tc6393xb_lcd_mode(struct platform_device *fb_dev,
					struct fb_videomode *mode);


/*
 * Relative to irq_base
 */
#define	IRQ_TC6393_NAND		0
#define	IRQ_TC6393_MMC		1
#define	IRQ_TC6393_OHCI		2
#define	IRQ_TC6393_SERIAL	3
#define	IRQ_TC6393_FB		4

#define	TC6393XB_NR_IRQS	8

#endif
