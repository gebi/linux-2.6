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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/mfd-core.h>
#include <linux/mfd/tmio.h>
#include <linux/mfd/tc6393xb.h>

struct tc6393xb_scr {
	u8 x00[8];
	u8	revid;		/* 0x08 Revision ID			*/
	u8 x01[0x47];
	u8	isr;		/* 0x50 Interrupt Status		*/
	u8 x02;
	u8	imr;		/* 0x52 Interrupt Mask			*/
	u8 x03;
	u8	irr;		/* 0x54 Interrupt Routing		*/
	u8 x04[0x0b];
	u16	gper;		/* 0x60 GP Enable			*/
	u8 x05[2];
	u16	gpi_sr[2];	/* 0x64 GPI Status			*/
	u16	gpi_imr[2];	/* 0x68 GPI INT Mask			*/
	u16	gpi_eder[2];	/* 0x6c GPI Edge Detect Enable		*/
	u16	gpi_lir[4];	/* 0x70 GPI Level Invert		*/
	u16	gpo_dsr[2];	/* 0x78 GPO Data Set			*/
	u16	gpo_doecr[2];	/* 0x7c GPO Data OE Control		*/
	u16	gp_iarcr[2];	/* 0x80 GP Internal Active Reg Control	*/
	u16	gp_iarlcr[2];	/* 0x84 GP Internal Active Reg Level Con*/
	u8	gpi_bcr[4];	/* 0x88 GPI Buffer Control		*/
	u16	gpa_iarcr;	/* 0x8c GPa Internal Active Reg Control	*/
	u8 x06[2];
	u16	gpa_iarlcr;	/* 0x90 GPa Internal Active Reg Level Co*/
	u8 x07[2];
	u16	gpa_bcr;	/* 0x94 GPa Buffer Control		*/
	u8 x08[2];
	u16	ccr;		/* 0x98 Clock Control			*/
	u16	pll2cr;		/* 0x9a PLL2 Control			*/
	u16	pll1cr[2];	/* 0x9c PLL1 Control			*/
	u8	diarcr;		/* 0xa0 Device Internal Active Reg Contr*/
	u8	dbocr;		/* 0xa1 Device Buffer Off Control	*/
	u8 x09[0x3e];
	u8	fer;		/* 0xe0 Function Enable			*/
	u8 x10[3];
	u16	mcr;		/* 0xe4 Mode Control			*/
	u8 x11[0x14];
	u8	config;		/* 0xfc Configuration Control		*/
	u8 x12[2];
	u8	debug;		/* 0xff Debug				*/
} __attribute__ ((packed));

/*--------------------------------------------------------------------------*/

struct tc6393xb {
	struct tc6393xb_scr __iomem	*scr;

	spinlock_t			lock; /* protects RMW cycles */

	struct {
		union tc6393xb_scr_fer	fer;
		union tc6393xb_scr_ccr	ccr;
		u8			gpi_bcr[4];
	} suspend_state;

	struct resource			rscr;
	struct resource			*iomem;
	int				irq;
};

/*--------------------------------------------------------------------------*/

static int tc6393xb_mmc_enable(struct platform_device *mmc) {
	struct platform_device		*dev	= to_platform_device(mmc->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr = tc6393xb->scr;
	union tc6393xb_scr_ccr		ccr;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);
	ccr.raw = ioread16(&scr->ccr);
	ccr.bits.ck32ken = 1;
	iowrite16(ccr.raw, &scr->ccr);
	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

static int tc6393xb_mmc_disable(struct platform_device *mmc) {
	struct platform_device		*dev	= to_platform_device(mmc->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	union tc6393xb_scr_ccr		ccr;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);
	ccr.raw = ioread16(&scr->ccr);
	ccr.bits.ck32ken = 0;
	iowrite16(ccr.raw, &scr->ccr);
	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

/*--------------------------------------------------------------------------*/

static int tc6393xb_nand_disable(struct platform_device *nand)
{
	return 0;
}

static int tc6393xb_nand_enable(struct platform_device *nand)
{
	struct platform_device		*dev	= to_platform_device(nand->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);

	/* SMD buffer on */
	dev_dbg(&dev->dev, "SMD buffer on\n");
	iowrite8(0xff, scr->gpi_bcr + 1);

	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

int tc6393xb_lcd_set_power(struct platform_device *fb, bool on)
{
	struct platform_device		*dev	= to_platform_device(fb->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	union tc6393xb_scr_fer		fer;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);

	fer.raw	= ioread8(&scr->fer);
	fer.bits.slcden = on ? 1 : 0;
	iowrite8(fer.raw, &scr->fer);

	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}
EXPORT_SYMBOL(tc6393xb_lcd_set_power);

int tc6393xb_lcd_mode(struct platform_device *fb_dev,
					struct fb_videomode *mode) {
	struct tc6393xb			*tc6393xb =
		platform_get_drvdata(to_platform_device(fb_dev->dev.parent));
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;

	iowrite16(mode->pixclock,		scr->pll1cr + 0);
	iowrite16(mode->pixclock >> 16,		scr->pll1cr + 1);

	return 0;
}
EXPORT_SYMBOL(tc6393xb_lcd_mode);

static int tc6393xb_ohci_disable(struct platform_device *ohci)
{
	struct platform_device		*dev	= to_platform_device(ohci->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	union tc6393xb_scr_ccr		ccr;
	union tc6393xb_scr_fer		fer;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);

	fer.raw = ioread8(&scr->fer);
	fer.bits.usben = 0;
	iowrite8(fer.raw, &scr->fer);

	ccr.raw = ioread16(&scr->ccr);
	ccr.bits.usbcken = 0;
	iowrite16(ccr.raw, &scr->ccr);

	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

static int tc6393xb_ohci_enable(struct platform_device *ohci)
{
	struct platform_device		*dev	= to_platform_device(ohci->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	union tc6393xb_scr_ccr		ccr;
	union tc6393xb_scr_fer		fer;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);

	ccr.raw = ioread16(&scr->ccr);
	ccr.bits.usbcken = 1;
	iowrite16(ccr.raw, &scr->ccr);

	fer.raw = ioread8(&scr->fer);
	fer.bits.usben = 1;
	iowrite8(fer.raw, &scr->fer);

	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

static int tc6393xb_fb_disable(struct platform_device *fb)
{
	struct platform_device		*dev	= to_platform_device(fb->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	union tc6393xb_scr_ccr		ccr;
	union tc6393xb_scr_fer		fer;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);

	/*
	 * FIXME: is this correct or it should be moved to other _disable?
	 */
	fer.raw	= ioread8(&scr->fer);
	fer.bits.slcden = 0;
	fer.bits.lcdcven = 0;
	iowrite8(fer.raw, &scr->fer);

	ccr.raw = ioread16(&scr->ccr);
	ccr.bits.mclksel = disable;
	iowrite16(ccr.raw, &scr->ccr);

	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

static int tc6393xb_fb_enable(struct platform_device *fb)
{
	struct platform_device		*dev	= to_platform_device(fb->dev.parent);
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	union tc6393xb_scr_ccr		ccr;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);

	ccr.raw = ioread16(&scr->ccr);
	ccr.bits.mclksel = m48MHz;
	iowrite16(ccr.raw, &scr->ccr);

	spin_unlock_irqrestore(&tc6393xb->lock, flags);

	return 0;
}

static struct resource tc6393xb_mmc_resources[] = {
	{
		.name	= TMIO_MMC_CONTROL,
		.start	= 0x800,
		.end	= 0x9ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_MMC_CONFIG,
		.start	= 0x200,
		.end	= 0x2ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_MMC_IRQ,
		.start	= IRQ_TC6393_MMC,
		.end	= IRQ_TC6393_MMC,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_MFD_SUBDEVICE,
	},
};

const static struct resource tc6393xb_nand_resources[] = {
	{
		.name	= TMIO_NAND_CONFIG,
		.start	= 0x0100,
		.end	= 0x01ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_NAND_CONTROL,
		.start	= 0x1000,
		.end	= 0x1007,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_NAND_IRQ,
		.start	= IRQ_TC6393_NAND,
		.end	= IRQ_TC6393_NAND,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_MFD_SUBDEVICE,
	},
};

const static struct resource tc6393xb_ohci_resources[] = {
	{
		.name	= TMIO_OHCI_CONFIG,
		.start	= 0x0300,
		.end	= 0x03ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_OHCI_CONTROL,
		.start	= 0x3000,
		.end	= 0x31ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_OHCI_SRAM,
		.start	= 0x010000,
		.end	= 0x017fff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_OHCI_SRAM_ALIAS,
		.start	= 0x018000,
		.end	= 0x01ffff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_OHCI_IRQ,
		.start	= IRQ_TC6393_OHCI,
		.end	= IRQ_TC6393_OHCI,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_MFD_SUBDEVICE,
	},
};

const static struct resource tc6393xb_fb_resources[] = {
	{
		.name	= TMIO_FB_CONFIG,
		.start	= 0x0500,
		.end	= 0x05ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_FB_CONTROL,
		.start	= 0x5000,
		.end	= 0x51ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_FB_VRAM,
		.start	= 0x100000,
		.end	= 0x1fffff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= TMIO_FB_IRQ,
		.start	= IRQ_TC6393_FB,
		.end	= IRQ_TC6393_FB,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_MFD_SUBDEVICE,
	},
};

static struct mfd_cell tc6393xb_cells[] = {
	{
		.name = "tmio-nand",
		.enable = tc6393xb_nand_enable,
		.disable = tc6393xb_nand_disable,
		.num_resources = ARRAY_SIZE(tc6393xb_nand_resources),
		.resources = tc6393xb_nand_resources,
	},
	{
		.name = "tmio-ohci",
		.enable = tc6393xb_ohci_enable,
		.disable = tc6393xb_ohci_disable,
		.num_resources = ARRAY_SIZE(tc6393xb_ohci_resources),
		.resources = tc6393xb_ohci_resources,
	},
	{
		.name = "tmio-fb",
		.enable = tc6393xb_fb_enable,
		.disable = tc6393xb_fb_disable,
		.num_resources = ARRAY_SIZE(tc6393xb_fb_resources),
		.resources = tc6393xb_fb_resources,
	},
	{
		.name = "tmio-mmc",
		.enable = tc6393xb_mmc_enable,
		.disable = tc6393xb_mmc_disable,
		.num_resources = ARRAY_SIZE(tc6393xb_mmc_resources),
		.resources = tc6393xb_mmc_resources,
	},
};

/*--------------------------------------------------------------------------*/

static void
tc6393xb_irq(unsigned int irq, struct irq_desc *desc)
{
	struct platform_device		*dev	= get_irq_chip_data(irq);
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	unsigned int			isr;
	unsigned int			i;

	desc->chip->ack(irq);

	while ((isr = ioread8(&scr->isr) & ~ioread8(&scr->imr)))
		for (i = 0; i < TC6393XB_NR_IRQS; i++) {
			if (isr & (1 << i))
				desc_handle_irq(tcpd->irq_base + i,
					irq_desc + tcpd->irq_base + i);
		}
}

static void tc6393xb_irq_ack(unsigned int irq)
{
}

static void tc6393xb_irq_mask(unsigned int irq)
{
	struct platform_device		*dev	= get_irq_chip_data(irq);
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);
	iowrite8(ioread8(&scr->imr) | (1 << (irq - tcpd->irq_base)),
								&scr->imr);
	spin_unlock_irqrestore(&tc6393xb->lock, flags);
}

static void tc6393xb_irq_unmask(unsigned int irq)
{
	struct platform_device		*dev	= get_irq_chip_data(irq);
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	unsigned long			flags;

	spin_lock_irqsave(&tc6393xb->lock, flags);
	iowrite8(ioread8(&scr->imr) & ~(1 << (irq - tcpd->irq_base)),
								&scr->imr);
	spin_unlock_irqrestore(&tc6393xb->lock, flags);
}

static struct irq_chip tc6393xb_chip = {
	.name	= "tc6393xb",
	.ack	= tc6393xb_irq_ack,
	.mask	= tc6393xb_irq_mask,
	.unmask	= tc6393xb_irq_unmask,
};

static void tc6393xb_attach_irq(struct platform_device *dev)
{
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	unsigned int			irq;

	for (
			irq = tcpd->irq_base;
			irq <= tcpd->irq_base + TC6393XB_NR_IRQS;
			irq++) {
		set_irq_chip(irq, &tc6393xb_chip);
		set_irq_chip_data(irq, dev);
		set_irq_handler(irq, handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	set_irq_type(tc6393xb->irq, IRQT_FALLING);
	set_irq_chip_data(tc6393xb->irq, dev);
	set_irq_chained_handler(tc6393xb->irq, tc6393xb_irq);
}

static void tc6393xb_detach_irq(struct platform_device *dev)
{
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	unsigned int			irq;

	set_irq_chained_handler(tc6393xb->irq, NULL);
	set_irq_chip_data(tc6393xb->irq, NULL);

	for (
			irq = tcpd->irq_base;
			irq <= tcpd->irq_base + TC6393XB_NR_IRQS;
			irq++) {
		set_irq_flags(irq, 0);
		set_irq_chip(irq, NULL);
		set_irq_chip_data(irq, NULL);
	}
}

static int tc6393xb_hw_init(struct platform_device *dev, int resume)
{
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr	= tc6393xb->scr;
	int				ret;
	int				i;

	if (resume)
		ret = tcpd->resume(dev);
	else
		ret = tcpd->enable(dev);
	if (ret)
		return ret;

	iowrite8(resume ?
		tc6393xb->suspend_state.fer.raw :
		0,				&scr->fer);
	iowrite16(tcpd->scr_pll2cr,		&scr->pll2cr);
	iowrite16(resume?
		tc6393xb->suspend_state.ccr.raw :
		tcpd->scr_ccr.raw,		&scr->ccr);
	iowrite16(tcpd->scr_mcr.raw,		&scr->mcr);
	iowrite16(tcpd->scr_gper,		&scr->gper);
	iowrite8(0,				&scr->irr);
	iowrite8(0xbf,				&scr->imr);
	iowrite16(tcpd->scr_gpo_dsr,		scr->gpo_dsr + 0);
	iowrite16(tcpd->scr_gpo_dsr >> 16,	scr->gpo_dsr + 1);
	iowrite16(tcpd->scr_gpo_doecr,		scr->gpo_doecr + 0);
	iowrite16(tcpd->scr_gpo_doecr >> 16,	scr->gpo_doecr + 1);

	if (resume)
		for (i = 0; i < 4; i++)
			iowrite8(tc6393xb->suspend_state.gpi_bcr[i],
						scr->gpi_bcr + i);

	return 0;
}

static int __devinit tc6393xb_probe(struct platform_device *dev)
{
	struct tc6393xb_platform_data *tcpd	= dev->dev.platform_data;
	struct tc6393xb		*tc6393xb;
	struct resource		*iomem;
	struct resource		*rscr;
	int			retval;

	iomem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -EINVAL;

	tc6393xb = kzalloc(sizeof *tc6393xb, GFP_KERNEL);
	if (!tc6393xb) {
		retval = -ENOMEM;
		goto err_kzalloc;
	}

	spin_lock_init(&tc6393xb->lock);

	platform_set_drvdata(dev, tc6393xb);
	tc6393xb->iomem	= iomem;
	tc6393xb->irq	= platform_get_irq(dev, 0);

	rscr		= &tc6393xb->rscr;
	rscr->name	= "tc6393xb-core";
	rscr->start	= iomem->start;
	rscr->end	= iomem->start + 0xff;
	rscr->flags	= IORESOURCE_MEM;

	retval = request_resource(iomem, rscr);
	if (retval)
		goto err_request_scr;

	tc6393xb->scr	= ioremap(rscr->start, rscr->end - rscr->start + 1);
	if (!tc6393xb->scr) {
		retval = -ENOMEM;
		goto err_ioremap;
	}

	retval = tc6393xb_hw_init(dev, 0);
	if (retval)
		goto err_hw_init;

	printk(KERN_INFO "Toshiba tc6393xb revision %d at 0x%08lx, irq %d\n",
			ioread8(&tc6393xb->scr->revid),
			(unsigned long) iomem->start, tc6393xb->irq);

	if (tc6393xb->irq)
		tc6393xb_attach_irq(dev);

	tc6393xb_cells[0].driver_data = tcpd->nand_data;
	tc6393xb_cells[1].driver_data = NULL; /* tcpd->ohci_data; */
	tc6393xb_cells[2].driver_data = tcpd->fb_data;

	retval = mfd_add_devices(dev,
			tc6393xb_cells, ARRAY_SIZE(tc6393xb_cells),
			iomem, 0, tcpd->irq_base);

	if (retval == 0)
		return 0;

	if (tc6393xb->irq)
		tc6393xb_detach_irq(dev);

err_hw_init:
	iounmap(tc6393xb->scr);
err_ioremap:
	release_resource(rscr);
err_request_scr:
	kfree(tc6393xb);
err_kzalloc:
	release_resource(iomem);
	return retval;
}

static int __devexit tc6393xb_remove(struct platform_device *dev) {
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	int ret;

	if (tc6393xb->irq)
		tc6393xb_detach_irq(dev);

	ret = tcpd->disable(dev);

	iounmap(tc6393xb->scr);
	release_resource(&tc6393xb->rscr);
	release_resource(tc6393xb->iomem);

	mfd_remove_devices(dev);

	platform_set_drvdata(dev, NULL);

	kfree(tc6393xb);

	return ret;
}

#ifdef CONFIG_PM
static int tc6393xb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct tc6393xb_platform_data	*tcpd	= dev->dev.platform_data;
	struct tc6393xb			*tc6393xb = platform_get_drvdata(dev);
	struct tc6393xb_scr __iomem	*scr = tc6393xb->scr;
	int i;


	tc6393xb->suspend_state.ccr.raw		= ioread16(&scr->ccr);
	tc6393xb->suspend_state.fer.raw		= ioread8(&scr->fer);
	for (i = 0; i < 4; i++)
		tc6393xb->suspend_state.gpi_bcr[i] =
			ioread8(scr->gpi_bcr + i);

	return tcpd->suspend(dev);
}

static int tc6393xb_resume(struct platform_device *dev)
{
	return tc6393xb_hw_init(dev, 1);
}
#else
#define tc6393xb_suspend NULL
#define tc6393xb_resume NULL
#endif

static struct platform_driver tc6393xb_driver = {
	.probe = tc6393xb_probe,
	.remove = __devexit_p(tc6393xb_remove),
	.suspend = tc6393xb_suspend,
	.resume = tc6393xb_resume,

	.driver = {
		.name = "tc6393xb",
		.owner		= THIS_MODULE,
	},
};

static int __init tc6393xb_init(void)
{
	return platform_driver_register(&tc6393xb_driver);
}

static void __exit tc6393xb_exit(void)
{
	platform_driver_unregister(&tc6393xb_driver);
}

module_init(tc6393xb_init);
module_exit(tc6393xb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ian Molton, Dmitry Baryshkov and Dirk Opfer");
MODULE_DESCRIPTION("tc6393xb Toshiba Mobile IO Controller");
