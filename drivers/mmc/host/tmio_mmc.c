/*
 *  linux/drivers/mmc/tmio_mmc.c
 *
 *  Copyright (C) 2004 Ian Molton
 *  Copyright (C) 2007 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the MMC / SD / SDIO cell found in:
 *
 * TC6393XB TC6391XB TC6387XB T7L66XB
 *
 * This driver draws mainly on scattered spec sheets, Reverse engineering
 * of the toshiba e800  SD driver and some parts of the 2.4 ASIC3 driver (4 bit
 * support). (Further 4 bit support from a later datasheet).
 *
 * TODO:
 *   Investigate using a workqueue for PIO transfers
 *   Eliminate FIXMEs
 *   SDIO support
 *   Better Power management
 *   Handle MMC errors better
 *   double buffer support
 *
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mfd-core.h>
#include <linux/mfd/tmio.h>

#include "tmio_mmc.h"

/*
 * Fixme - documentation conflicts on what the clock values are for the
 * various dividers.
 * One document I have says that its a divisor of a 24MHz clock, another 33.
 * This probably depends on HCLK for a given platform, so we may need to
 * require HCLK be passed to us from the MFD core.
 *
 */

static void tmio_mmc_set_clock (struct tmio_mmc_host *host, int new_clock) {
	struct tmio_mmc_cnf __iomem *cnf = host->cnf;
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;
	u32 clk = 0, clock;

	if (new_clock) {
		for(clock = 46875, clk = 0x100; new_clock >= (clock<<1); ){
			clock <<= 1;
			clk >>= 1;
		}
		if(clk & 0x1)
			clk = 0x20000;

		clk >>= 2;
		if(clk & 0x8000) /* For full speed we disable the divider. */
			writeb(0, &cnf->sd_clk_mode);
		else
			writeb(1, &cnf->sd_clk_mode);
		clk |= 0x100;
	}

	writew(clk, &ctl->sd_card_clk_ctl);
}

static void tmio_mmc_clk_stop (struct tmio_mmc_host *host) {
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;

	writew(0x0000, &ctl->clk_and_wait_ctl);
	msleep(10);
	writew(readw(&ctl->sd_card_clk_ctl) & ~0x0100, &ctl->sd_card_clk_ctl);
	msleep(10);
}

static void tmio_mmc_clk_start (struct tmio_mmc_host *host) {
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;

	writew(readw(&ctl->sd_card_clk_ctl) | 0x0100, &ctl->sd_card_clk_ctl);
	msleep(10);
	writew(0x0100, &ctl->clk_and_wait_ctl);
	msleep(10);
}

static void reset(struct tmio_mmc_host *host) {
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;

	/* FIXME - should we set stop clock reg here */
	writew(0x0000, &ctl->reset_sd);
	writew(0x0000, &ctl->reset_sdio);
	msleep(10);
	writew(0x0001, &ctl->reset_sd);
	writew(0x0001, &ctl->reset_sdio);
	msleep(10);
}

static void
tmio_mmc_finish_request(struct tmio_mmc_host *host)
{
	struct mmc_request *mrq = host->mrq;

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmc_request_done(host->mmc, mrq);
}

/* These are the bitmasks the tmio chip requires to implement the MMC response
 * types. Note that R1 and R6 are the same in this scheme. */
#define APP_CMD        0x0040
#define RESP_NONE      0x0300
#define RESP_R1        0x0400
#define RESP_R1B       0x0500
#define RESP_R2        0x0600
#define RESP_R3        0x0700
#define DATA_PRESENT   0x0800
#define TRANSFER_READ  0x1000
#define TRANSFER_MULTI 0x2000
#define SECURITY_CMD   0x4000

static void
tmio_mmc_start_command(struct tmio_mmc_host *host, struct mmc_command *cmd)
{
        struct tmio_mmc_ctl __iomem *ctl = host->ctl;
	struct mmc_data *data = host->data;
	int c = cmd->opcode;

	if(cmd->opcode == MMC_STOP_TRANSMISSION) {
		writew(0x001, &ctl->stop_internal_action);
		return;
	}

	switch(mmc_resp_type(cmd)) {
		case MMC_RSP_NONE: c |= RESP_NONE; break;
		case MMC_RSP_R1:   c |= RESP_R1;   break;
		case MMC_RSP_R1B:  c |= RESP_R1B;  break;
		case MMC_RSP_R2:   c |= RESP_R2;   break;
		case MMC_RSP_R3:   c |= RESP_R3;   break;
		default:
			DBG("Unknown response type %d\n", mmc_resp_type(cmd));
	}

	host->cmd = cmd;

/* FIXME - this seems to be ok comented out but the spec suggest this bit should
 *         be set when issuing app commands.
 *	if(cmd->flags & MMC_FLAG_ACMD)
 *		c |= APP_CMD;
 */
	if(data) {
		c |= DATA_PRESENT;
		if(data->blocks > 1) {
			writew(0x100, &ctl->stop_internal_action);
			c |= TRANSFER_MULTI;
		}
		if(data->flags & MMC_DATA_READ)
			c |= TRANSFER_READ;
	}

	enable_mmc_irqs(ctl, TMIO_MASK_CMD);

	/* Fire off the command */
	tmio_iowrite32(cmd->arg, ctl->arg_reg);
	writew(c, &ctl->sd_cmd);
}

/* This chip always returns (at least?) as much data as you ask for.
 * Im unsure what happens if you ask for less than a block. This should be
 * looked into to ensure that a funny length read doesnt hose the controller.
 *
 * FIXME - this chip cannot do 1 and 2 byte data requests in 4 bit mode
 */
static inline void tmio_mmc_pio_irq(struct tmio_mmc_host *host) {
        struct tmio_mmc_ctl __iomem *ctl = host->ctl;
	struct mmc_data *data = host->data;
        unsigned short *buf;
        unsigned int count;
        unsigned long flags;

        if(!data){
		DBG("Spurious PIO IRQ\n");
                return;
        }

	buf = (unsigned short *)(tmio_mmc_kmap_atomic(host, &flags) +
	      host->sg_off);

	/* Ensure we dont read more than one block. The chip will interrupt us
	 * When the next block is available.
	 * FIXME - this is probably not true now IRQ handling is fixed
	 */
	count = host->sg_ptr->length - host->sg_off;
	if(count > data->blksz)
		count = data->blksz;

	DBG("count: %08x offset: %08x flags %08x\n",
	    count, host->sg_off, data->flags);

	/* Transfer the data */
	if(data->flags & MMC_DATA_READ)
		readsw(&ctl->sd_data_port[0], buf, count >> 1);
	else
		writesw(&ctl->sd_data_port[0], buf, count >> 1);

	host->sg_off += count;

	tmio_mmc_kunmap_atomic(host, &flags);

	if(host->sg_off == host->sg_ptr->length)
		tmio_mmc_next_sg(host);

	return;
}

static inline void tmio_mmc_data_irq(struct tmio_mmc_host *host) {
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;
	struct mmc_data *data = host->data;

	host->data = NULL;

	if(!data){
		DBG("Spurious data end IRQ\n");
		return;
	}

	/* FIXME - return correct transfer count on errors */
	if (!data->error)
		data->bytes_xfered = data->blocks * data->blksz;
	else
		data->bytes_xfered = 0;

	DBG("Completed data request\n");

	/*FIXME - other drivers allow an optional stop command of any given type
	 *        which we dont do, as the chip can auto generate them.
	 *        Perhaps we can be smarter about when to use auto CMD12 and
	 *        only issue the auto request when we know this is the desired
	 *        stop command, allowing fallback to the stop command the
	 *        upper layers expect. For now, we do what works.
	 */

	writew(0x000, &ctl->stop_internal_action);

	if(data->flags & MMC_DATA_READ)
		disable_mmc_irqs(ctl, TMIO_MASK_READOP);
	else
		disable_mmc_irqs(ctl, TMIO_MASK_WRITEOP);

	tmio_mmc_finish_request(host);
}

static inline void tmio_mmc_cmd_irq(struct tmio_mmc_host *host, unsigned int stat) {
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;
	struct mmc_command *cmd = host->cmd;

	if(!host->cmd) {
		DBG("Spurious CMD irq\n");
		return;
	}

	host->cmd = NULL;

	/* This controller is sicker than the PXA one. not only do we need to
	 * drop the top 8 bits of the first response word, we also need to
	 * modify the order of the response for short response command types.
	 */

	/* FIXME - this works but readl is wrong and will break on asic3... */
	cmd->resp[3] = tmio_ioread32(&ctl->response[0]);
	cmd->resp[2] = tmio_ioread32(&ctl->response[2]);
	cmd->resp[1] = tmio_ioread32(&ctl->response[4]);
	cmd->resp[0] = tmio_ioread32(&ctl->response[6]);

	if(cmd->flags &  MMC_RSP_136) {
		cmd->resp[0] = (cmd->resp[0] <<8) | (cmd->resp[1] >>24);
		cmd->resp[1] = (cmd->resp[1] <<8) | (cmd->resp[2] >>24);
		cmd->resp[2] = (cmd->resp[2] <<8) | (cmd->resp[3] >>24);
		cmd->resp[3] <<= 8;
	}
	else if(cmd->flags & MMC_RSP_R3) {
		cmd->resp[0] = cmd->resp[3];
	}

	if (stat & TMIO_STAT_CMDTIMEOUT)
		cmd->error = -ETIMEDOUT;
	else if (stat & TMIO_STAT_CRCFAIL && cmd->flags & MMC_RSP_CRC)
		cmd->error = -EILSEQ;

	/* If there is data to handle we enable data IRQs here, and
	 * we will ultimatley finish the request in the data_end handler.
	 * If theres no data or we encountered an error, finish now.
	 */
	if(host->data && !cmd->error){
		if(host->data->flags & MMC_DATA_READ)
			enable_mmc_irqs(ctl, TMIO_MASK_READOP);
		else
			enable_mmc_irqs(ctl, TMIO_MASK_WRITEOP);
	}
	else {
		tmio_mmc_finish_request(host);
	}

	return;
}


static irqreturn_t tmio_mmc_irq(int irq, void *devid)
{
	struct tmio_mmc_host *host = devid;
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;
	unsigned int ireg, irq_mask, status;

	DBG("MMC IRQ begin\n");

	status = tmio_ioread32(ctl->status);
	irq_mask   = tmio_ioread32(ctl->irq_mask);
	ireg   = status & TMIO_MASK_IRQ & ~irq_mask;

#ifdef CONFIG_MMC_DEBUG
	debug_status(status);
	debug_status(ireg);
#endif
	if (!ireg) {
		disable_mmc_irqs(ctl, status & ~irq_mask);
#ifdef CONFIG_MMC_DEBUG
		WARN("tmio_mmc: Spurious MMC irq, disabling! 0x%08x 0x%08x 0x%08x\n", status, irq_mask, ireg);
		debug_status(status);
#endif
		goto out;
	}

	while (ireg) {
		/* Card insert / remove attempts */
		if (ireg & (TMIO_STAT_CARD_INSERT | TMIO_STAT_CARD_REMOVE)){
			ack_mmc_irqs(ctl, TMIO_STAT_CARD_INSERT | TMIO_STAT_CARD_REMOVE);
			mmc_detect_change(host->mmc,0);
		}

		/* CRC and other errors */
/*		if (ireg & TMIO_STAT_ERR_IRQ)
 *			handled |= tmio_error_irq(host, irq, stat);
 */

		/* Command completion */
			if (ireg & TMIO_MASK_CMD) {
			tmio_mmc_cmd_irq(host, status);
			ack_mmc_irqs(ctl, TMIO_MASK_CMD);
		}

		/* Data transfer */
		if (ireg & (TMIO_STAT_RXRDY | TMIO_STAT_TXRQ)) {
			ack_mmc_irqs(ctl, TMIO_STAT_RXRDY | TMIO_STAT_TXRQ);
			tmio_mmc_pio_irq(host);
		}

		/* Data transfer completion */
		if (ireg & TMIO_STAT_DATAEND) {
			tmio_mmc_data_irq(host);
			ack_mmc_irqs(ctl, TMIO_STAT_DATAEND);
		}

		/* Check status - keep going until we've handled it all */
		status = tmio_ioread32(ctl->status);
		irq_mask   = tmio_ioread32(ctl->irq_mask);
		ireg   = status & TMIO_MASK_IRQ & ~irq_mask;

#ifdef CONFIG_MMC_DEBUG
		DBG("Status at end of loop: %08x\n", status);
		debug_status(status);
#endif
	}
	DBG("MMC IRQ end\n");

out:
	return IRQ_HANDLED;
}

static void tmio_mmc_start_data(struct tmio_mmc_host *host, struct mmc_data *data)
{
	struct tmio_mmc_ctl __iomem *ctl = host->ctl;

	DBG("setup data transfer: blocksize %08x  nr_blocks %d\n",
	    data->blksz, data->blocks);

	tmio_mmc_init_sg(host, data);
	host->data = data;

	/* Set transfer length / blocksize */
	writew(data->blksz,  &ctl->sd_xfer_len);
        writew(data->blocks, &ctl->xfer_blk_count);
}

/* Process requests from the MMC layer */
static void tmio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);

	host->mrq = mrq;

	/* If we're performing a data request we need to setup some
	   extra information */
	if (mrq->data)
		tmio_mmc_start_data(host, mrq->data);

	tmio_mmc_start_command(host, mrq->cmd);
}

/* Set MMC clock / power.
 * Note: This controller uses a simple divider scheme therefore it cannot
 * run a MMC card at full speed (20MHz). The max clock is 24MHz on SD, but as
 * MMC wont run that fast, it has to be clocked at 12MHz which is the next
 * slowest setting.
 */
static void tmio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct tmio_mmc_cnf __iomem *cnf = host->cnf;
        struct tmio_mmc_ctl __iomem *ctl = host->ctl;

	if(ios->clock)
		tmio_mmc_set_clock (host, ios->clock);

	/* Power sequence - OFF -> ON -> UP */
	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		writeb(0x00, &cnf->pwr_ctl[1]);  /* power down SD bus */
		tmio_mmc_clk_stop(host);
		break;
	case MMC_POWER_ON:
		writeb(0x02, &cnf->pwr_ctl[1]);  /* power up SD bus */
		break;
	case MMC_POWER_UP:
		tmio_mmc_clk_start(host);         /* start bus clock */
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		writew(0x80e0, &ctl->sd_mem_card_opt);
	break;
	case MMC_BUS_WIDTH_4:
		writew(0x00e0, &ctl->sd_mem_card_opt);
	break;
	}

	/* Potentially we may need a 140us pause here. FIXME */
	udelay(140);
}

static int tmio_mmc_get_ro(struct mmc_host *mmc) {
	struct tmio_mmc_host *host = mmc_priv(mmc);
        struct tmio_mmc_ctl __iomem *ctl = host->ctl;

	return (readw(&ctl->status[0]) & TMIO_STAT_WRPROTECT)?0:1;
}

static struct mmc_host_ops tmio_mmc_ops = {
	.request	= tmio_mmc_request,
	.set_ios	= tmio_mmc_set_ios,
	.get_ro         = tmio_mmc_get_ro,
};

static int tmio_mmc_suspend(struct platform_device *dev, pm_message_t state) {
	struct mfd_cell	*cell	= mfd_get_cell(dev);
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret;

	ret = mmc_suspend_host(mmc, state);

        /* Tell MFD core it can disable us now.*/
	if(!ret && cell->disable)
		cell->disable(dev);

	return ret;
}

static int tmio_mmc_resume(struct platform_device *dev) {
	struct mfd_cell	*cell	= mfd_get_cell(dev);
	struct mmc_host *mmc = platform_get_drvdata(dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct tmio_mmc_cnf __iomem *cnf = host->cnf;

	/* Enable the MMC/SD Control registers */
	writew(SDCREN, &cnf->cmd);
	msleep(10);
	writel(dev->resource[0].start & 0xfffe, &cnf->ctl_base);

	msleep(10);
	/* Tell the MFD core we are ready to be enabled */
	if(cell->enable)
		cell->enable(dev);

	msleep(10);
	printk("%08x \n", dev->resource[0].start);
	printk("%08x \n", &cnf->cmd);
	printk("%08x \n", &cnf->ctl_base);
	printk("%08x \n", host->cnf);
	printk("%08x \n", host->ctl);
	printk("%08x \n", ioread16(&host->ctl->irq_mask[0]));
	printk("%08x \n", ioread16(&host->ctl->irq_mask[1]));

	mmc_resume_host(mmc);

	return 0;
}

static int __devinit tmio_mmc_probe(struct platform_device *dev)
{
	struct mfd_cell	*cell	= mfd_get_cell(dev);
	struct tmio_mmc_cnf __iomem *cnf;
	struct tmio_mmc_ctl __iomem *ctl;
	struct tmio_mmc_host *host;
	struct mmc_host *mmc;
	int ret = -ENOMEM;

	mmc = mmc_alloc_host(sizeof(struct tmio_mmc_host), &dev->dev);
	if (!mmc) {
		goto out;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	platform_set_drvdata(dev, mmc); /* Used so we can de-init safely. */

	host->cnf = cnf = ioremap((unsigned long)dev->resource[1].start,
	                          (unsigned long)dev->resource[1].end -
	                          (unsigned long)dev->resource[1].start);
	if(!host->cnf)
		goto host_free;

	host->ctl = ctl = ioremap((unsigned long)dev->resource[0].start,
	                          (unsigned long)dev->resource[0].end -
	                          (unsigned long)dev->resource[0].start);
	if (!host->ctl) {
		goto unmap_cnf;
	}

	mmc->ops = &tmio_mmc_ops;
	mmc->caps = MMC_CAP_4_BIT_DATA;
	mmc->f_min = 46875;
	mmc->f_max = 24000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/* Enable the MMC/SD Control registers */
	writew(SDCREN, &cnf->cmd);
	writel(dev->resource[0].start & 0xfffe, &cnf->ctl_base);

	/* Tell the MFD core we are ready to be enabled */
	if(cell->enable)
		cell->enable(dev);

	writeb(0x01,&cnf->pwr_ctl[2]);    /* Disable SD power during suspend */
	writeb(0x1f, &cnf->stop_clk_ctl); /* Route clock to SDIO??? FIXME */
	writeb(0x0, &cnf->pwr_ctl[1]);    /* Power down SD bus*/
	tmio_mmc_clk_stop(host);          /* Stop bus clock */
	reset(host);                      /* Reset MMC HC */

	host->irq = (unsigned long)dev->resource[2].start;
	ret = request_irq(host->irq, tmio_mmc_irq, IRQF_DISABLED, "tmio-mmc", host);
	if (ret){
		ret = -ENODEV;
		DBG("Failed to allocate IRQ.\n");
		goto unmap_ctl;
	}
	set_irq_type(host->irq, IRQT_FALLING);

	mmc_add_host(mmc);

	printk(KERN_INFO "%s at 0x%08lx irq %d\n", mmc_hostname(host->mmc),
	     (unsigned long)host->ctl, host->irq);

	/* Lets unmask the IRQs we want to know about */
	disable_mmc_irqs(ctl, TMIO_MASK_ALL);
	enable_mmc_irqs(ctl,  TMIO_MASK_IRQ);

	return 0;

unmap_ctl:
	iounmap(host->ctl);
unmap_cnf:
	iounmap(host->cnf);
host_free:
	mmc_free_host(mmc);
out:
	return ret;
}

static int __devexit tmio_mmc_remove(struct platform_device *dev)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (mmc) {
		struct tmio_mmc_host *host = mmc_priv(mmc);
		mmc_remove_host(mmc);
		free_irq(host->irq, host);
		/* FIXME - we might want to consider stopping the chip here. */
		iounmap(host->ctl);
		iounmap(host->cnf);
		mmc_free_host(mmc); /* FIXME - why does this call hang ? */
	}
	return 0;
}

/* ------------------- device registration ----------------------- */

static struct platform_driver tmio_mmc_driver = {
	.driver = {
		.name = "tmio-mmc",
	},
	.probe = tmio_mmc_probe,
	.remove = __devexit_p(tmio_mmc_remove),
#ifdef CONFIG_PM
	.suspend = tmio_mmc_suspend,
	.resume = tmio_mmc_resume,
#endif
};


static int __init tmio_mmc_init(void)
{
	return platform_driver_register (&tmio_mmc_driver);
}

static void __exit tmio_mmc_exit(void)
{
	platform_driver_unregister (&tmio_mmc_driver);
}

module_init(tmio_mmc_init);
module_exit(tmio_mmc_exit);

MODULE_DESCRIPTION("Toshiba TMIO SD/MMC driver");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_LICENSE("GPLv2");
