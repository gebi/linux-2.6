/*
 * OHCI HCD(Host Controller Driver) for USB.
 *
 *(C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *(C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 *(C) Copyright 2002 Hewlett-Packard Company
 *
 * Bus glue for Toshiba Mobile IO(TMIO) Controller's OHCI core
 *(C) Copyright 2005 Chris Humbert <mahadri-usb@drigon.com>
 *
 * This is known to work with the following variants:
 *	TC6393XB revision 3	(32kB SRAM)
 *
 * The TMIO's OHCI core DMAs through a small internal buffer that
 * is directly addressable by the CPU.  dma_declare_coherent_memory
 * and DMA bounce buffers allow the higher-level OHCI host driver to
 * work.  However, the dma API doesn't handle dma mapping failures
 * well(dma_sg_map() is a prime example), so it is unusable.
 *
 * This HC pretends be a PIO-ish controller and uses the kernel's
 * generic allocator for the entire SRAM.  Using the USB core's
 * usb_operations, we provide hcd_buffer_alloc/free.  Using the OHCI's
 * ohci_ops, we provide memory management for OHCI's TDs and EDs.  We
 * internally queue a URB's TDs until enough dma memory is available
 * to enqueue them with the HC.
 *
 * Written from sparse documentation from Toshiba and Sharp's driver
 * for the 2.4 kernel,
 *	usb-ohci-tc6393.c(C) Copyright 2004 Lineo Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/sched.h>*/
#include <linux/platform_device.h>
#include <linux/mfd-core.h>
#include <linux/mfd/tmio.h>
#include <linux/dma-mapping.h>

/*-------------------------------------------------------------------------*/

/*
 * USB Host Controller Configuration Register
 */
struct tmio_uhccr {
	u8 x00[8];
	u8	revid;	/* 0x08 Revision ID				*/
	u8 x01[7];
	u16	basel;	/* 0x10 USB Control Register Base Address Low	*/
	u16	baseh;	/* 0x12 USB Control Register Base Address High	*/
	u8 x02[0x2c];
	u8	ilme;	/* 0x40 Internal Local Memory Enable		*/
	u8 x03[0x0b];
	u16	pm;	/* 0x4c Power Management			*/
	u8 x04[2];
	u8	intc;	/* 0x50 INT Control				*/
	u8 x05[3];
	u16	lmw1l;	/* 0x54 Local Memory Window 1 LMADRS Low	*/
	u16	lmw1h;	/* 0x56 Local Memory Window 1 LMADRS High	*/
	u16	lmw1bl;	/* 0x58 Local Memory Window 1 Base Address Low	*/
	u16	lmw1bh;	/* 0x5A Local Memory Window 1 Base Address High	*/
	u16	lmw2l;	/* 0x5C Local Memory Window 2 LMADRS Low	*/
	u16	lmw2h;	/* 0x5E Local Memory Window 2 LMADRS High	*/
	u16	lmw2bl;	/* 0x60 Local Memory Window 2 Base Address Low	*/
	u16	lmw2bh;	/* 0x62 Local Memory Window 2 Base Address High	*/
	u8 x06[0x98];
	u8	misc;	/* 0xFC MISC					*/
	u8 x07[3];
} __attribute__((packed));

union tmio_uhccr_pm {
	u16		raw;
struct {
	unsigned	gcken:1;	/* D0 */
	unsigned	ckrnen:1;	/* D1 */
	unsigned	uspw1:1;	/* D2 USB Port 1 Power Disable	*/
	unsigned	uspw2:1;	/* D3 USB Port 2 Power Disable	*/
	unsigned	x00:4;
	unsigned	pmee:1;		/* D8 */
	unsigned	x01:6;
	unsigned	pmes:1;		/* D15 */
} __attribute__((packed));
} __attribute__((packed));

/*-------------------------------------------------------------------------*/

struct tmio_hcd {
	struct tmio_uhccr __iomem *ccr;
};

#define hcd_to_tmio(hcd)	((struct tmio_hcd *)(hcd_to_ohci(hcd) + 1))
#define ohci_to_tmio(ohci)	((struct tmio_hcd *)(ohci + 1))

/*-------------------------------------------------------------------------*/

static void tmio_stop_hc(struct platform_device *dev)
{
	struct mfd_cell			*cell	= mfd_get_cell(dev);
	struct usb_hcd			*hcd	= platform_get_drvdata(dev);
	struct tmio_hcd			*tmio	= hcd_to_tmio(hcd);
	struct tmio_uhccr __iomem	*ccr	= tmio->ccr;
	union tmio_uhccr_pm		pm	= {0};

	pm.gcken	= 1;
	pm.ckrnen	= 1;
	pm.uspw1	= 1;
	pm.uspw2	= 1;

	iowrite8(0,		&ccr->intc);
	iowrite8(0,		&ccr->ilme);
	iowrite16(0,		&ccr->basel);
	iowrite16(0,		&ccr->baseh);
	iowrite16(pm.raw,	&ccr->pm);

	cell->disable(dev);
}

static void tmio_start_hc(struct platform_device *dev)
{
	struct mfd_cell			*cell	= mfd_get_cell(dev);
	struct usb_hcd			*hcd	= platform_get_drvdata(dev);
	struct tmio_hcd			*tmio	= hcd_to_tmio(hcd);
	struct tmio_uhccr __iomem	*ccr	= tmio->ccr;
	union tmio_uhccr_pm		pm	= {0};
	unsigned long			base	= hcd->rsrc_start;

	pm.pmes		= 1;
	pm.pmee		= 1;
	pm.ckrnen	= 1;
	pm.gcken	= 1;

	cell->enable(dev);

	iowrite16(pm.raw,	&ccr->pm);
	iowrite16(base,		&ccr->basel);
	iowrite16(base >> 16,	&ccr->baseh);
	iowrite8(1,		&ccr->ilme);
	iowrite8(2,		&ccr->intc);

	dev_info(&dev->dev, "revision %d @ 0x%08llx, irq %d\n",
			ioread8(&ccr->revid), hcd->rsrc_start, hcd->irq);
}

static int usb_hcd_tmio_probe(const struct hc_driver *driver,
		struct platform_device *dev)
{
	struct resource		*config	= platform_get_resource_byname(dev, IORESOURCE_MEM, TMIO_OHCI_CONFIG);
	struct resource		*regs	= platform_get_resource_byname(dev, IORESOURCE_MEM, TMIO_OHCI_CONTROL);
	struct resource		*sram	= platform_get_resource_byname(dev, IORESOURCE_MEM, TMIO_OHCI_SRAM);
	int			irq	= platform_get_irq(dev, 0);
	struct tmio_hcd		*tmio;
	struct ohci_hcd		*ohci;
	struct usb_hcd		*hcd;
	int			retval;

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(driver, &dev->dev, dev->dev.bus_id);
	if (!hcd) {
		retval = -ENOMEM;
		goto err_usb_create_hcd;
	}

	hcd->rsrc_start	= regs->start;
	hcd->rsrc_len	= regs->end - regs->start + 1;

	tmio		= hcd_to_tmio(hcd);

	tmio->ccr = ioremap(config->start, config->end - config->start + 1);
	if (!tmio->ccr) {
		retval = -ENOMEM;
		goto err_ioremap_ccr;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		retval = -ENOMEM;
		goto err_ioremap_regs;
	}

	if (dma_declare_coherent_memory(&dev->dev, sram->start,
				sram->start,
				sram->end - sram->start + 1,
				DMA_MEMORY_MAP) != DMA_MEMORY_MAP) {
		retval = -EBUSY;
		goto err_dma_declare;
	}

	retval = dmabounce_register_dev(&dev->dev, 512, 4096);
	if (retval)
		goto err_dmabounce_register_dev;

	tmio_start_hc(dev);
	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	retval = usb_add_hcd(hcd, irq, IRQF_DISABLED);

	if (retval == 0)
		return retval;

	tmio_stop_hc(dev);

	dmabounce_unregister_dev(&dev->dev);
err_dmabounce_register_dev:
	dma_release_declared_memory(&dev->dev);
err_dma_declare:
	iounmap(hcd->regs);
err_ioremap_regs:
	iounmap(tmio->ccr);
err_ioremap_ccr:
	usb_put_hcd(hcd);
err_usb_create_hcd:

	return retval;
}

static void usb_hcd_tmio_remove(struct usb_hcd *hcd, struct platform_device *dev)
{
	struct tmio_hcd		*tmio	= hcd_to_tmio(hcd);

	usb_remove_hcd(hcd);
	tmio_stop_hc(dev);
	dmabounce_unregister_dev(&dev->dev);
	dma_release_declared_memory(&dev->dev);
	iounmap(hcd->regs);
	iounmap(tmio->ccr);
	usb_put_hcd(hcd);
}

static int __devinit
ohci_tmio_start(struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci	= hcd_to_ohci(hcd);
	int			retval;

	if ((retval = ohci_init(ohci)) < 0)
		return retval;

	if ((retval = ohci_run(ohci)) < 0) {
		err("can't start %s", hcd->self.bus_name);
		ohci_stop(hcd);
		return retval;
	}

	return 0;
}

static const struct hc_driver ohci_tmio_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"TMIO OHCI USB Host Controller",
	.hcd_priv_size =	sizeof(struct ohci_hcd) + sizeof (struct tmio_hcd),

	/* generic hardware linkage */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/* basic lifecycle operations */
	.start =		ohci_tmio_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

	/* managing i/o requests and associated device resources */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/* scheduling support */
	.get_frame_number =	ohci_get_frame,

	/* root hub support */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
	.hub_irq_enable =	ohci_rhsc_enable,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/
static struct platform_driver ohci_hcd_tmio_driver;

static int
tmio_dmabounce_check(struct device *dev, dma_addr_t dma, size_t size, void *data)
{
	struct resource		*sram	= data;
#ifdef DEBUG
	printk(KERN_ERR "tmio_dmabounce_check: %08x %d\n", dma, size);
#endif

	if (dev->driver != &ohci_hcd_tmio_driver.driver)
		return 0;

	if (sram->start <= dma && dma + size <= sram->end)
		return 0;

	return 1;
}

static u64 dma_mask = DMA_32BIT_MASK;

static int ohci_hcd_tmio_drv_probe(struct platform_device *dev)
{
	struct resource		*sram	= platform_get_resource_byname(dev, IORESOURCE_MEM, TMIO_OHCI_SRAM);

	dev->dev.dma_mask = &dma_mask;
	dev->dev.coherent_dma_mask = DMA_32BIT_MASK;

	dmabounce_register_checker(tmio_dmabounce_check, sram);

	return usb_hcd_tmio_probe(&ohci_tmio_hc_driver, dev);
}

static int ohci_hcd_tmio_drv_remove(struct platform_device *dev)
{
	struct usb_hcd		*hcd	= platform_get_drvdata(dev);
	struct resource		*sram	= platform_get_resource_byname(dev, IORESOURCE_MEM, TMIO_OHCI_SRAM);

	usb_hcd_tmio_remove(hcd, dev);

	platform_set_drvdata(dev, NULL);

	dmabounce_remove_checker(tmio_dmabounce_check, sram);

	return 0;
}

#ifdef	CONFIG_PM
static int ohci_hcd_tmio_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct usb_hcd		*hcd	= platform_get_drvdata(dev);
	struct ohci_hcd		*ohci	= hcd_to_ohci(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	tmio_stop_hc(dev);
	hcd->state = HC_STATE_SUSPENDED;
	dev->dev.power.power_state = PMSG_SUSPEND;

	return 0;
}

static int ohci_hcd_tmio_drv_resume(struct platform_device *dev)
{
	struct usb_hcd		*hcd	= platform_get_drvdata(dev);
	struct ohci_hcd		*ohci	= hcd_to_ohci(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	tmio_start_hc(dev);

	dev->dev.power.power_state = PMSG_ON;
	usb_hcd_resume_root_hub(hcd);

	return 0;
}
#endif

static struct platform_driver ohci_hcd_tmio_driver = {
	.probe		= ohci_hcd_tmio_drv_probe,
	.remove		= ohci_hcd_tmio_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef CONFIG_PM
	.suspend	= ohci_hcd_tmio_drv_suspend,
	.resume		= ohci_hcd_tmio_drv_resume,
#endif
	.driver		= {
		.name	= "tmio-ohci",
	},
};
