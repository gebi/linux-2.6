/* Definitons for use with the tmio_mmc.c
 *
 * (c) 2005 Ian Molton <spyro@f2s.com>
 * (c) 2007 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

struct tmio_mmc_cnf {
	u8 x00[4];
	u16	cmd;
	u8 x01[10];
	u32	ctl_base;
	u8 x02[41];
	u8	int_pin;
	u8 x03[2];
	u8	stop_clk_ctl;
	u8	gclk_ctl;     /* Gated Clock Control */
	u8	sd_clk_mode;  /* 0x42 */
	u8 x04;
	u16	pin_status;
	u8 x05[2];
	u8	pwr_ctl[3];
	u8 x06;
	u8	card_detect_mode;
	u8 x07[3];
	u8	sd_slot;
	u8 x08[159];
	u8	ext_gclk_ctl_1; /* Extended Gated Clock Control 1 */
	u8	ext_gclk_ctl_2; /* Extended Gated Clock Control 2 */
	u8 x09[7];
	u8	ext_gclk_ctl_3; /* Extended Gated Clock Control 3 */
	u8	sd_led_en_1;
	u8 x10[3];
	u8	sd_led_en_2;
	u8 x11;
} __attribute__ ((packed));

#define   SDCREN 0x2   /* Enable access to MMC CTL regs. (flag in COMMAND_REG)*/

struct tmio_mmc_ctl {
	u16	sd_cmd;
	u16	x00;
	u16	arg_reg[2];
	u16	stop_internal_action;
	u16	xfer_blk_count;
	u16	response[8];
	u16	status[2];
	u16	irq_mask[2];
	u16	sd_card_clk_ctl;
	u16	sd_xfer_len;
	u16	sd_mem_card_opt;
	u16	x01;
	u16	sd_error_detail_status[2];
	u16	sd_data_port[2];
	u16	transaction_ctl;
	u16	x02[85];
	u16	reset_sd;
	u16	x03[15];
	u16	sdio_regs[28];
	u16	clk_and_wait_ctl;
	u16	x04[83];
	u16	reset_sdio;
	u16	x05[15];
} __attribute__ ((packed));

/* Definitions for values the CTRL_STATUS register can take. */
#define TMIO_STAT_CMDRESPEND    0x00000001
#define TMIO_STAT_DATAEND       0x00000004
#define TMIO_STAT_CARD_REMOVE   0x00000008
#define TMIO_STAT_CARD_INSERT   0x00000010
#define TMIO_STAT_SIGSTATE      0x00000020
#define TMIO_STAT_WRPROTECT     0x00000080
#define TMIO_STAT_CARD_REMOVE_A 0x00000100
#define TMIO_STAT_CARD_INSERT_A 0x00000200
#define TMIO_STAT_SIGSTATE_A    0x00000400
#define TMIO_STAT_CMD_IDX_ERR   0x00010000
#define TMIO_STAT_CRCFAIL       0x00020000
#define TMIO_STAT_STOPBIT_ERR   0x00040000
#define TMIO_STAT_DATATIMEOUT   0x00080000
#define TMIO_STAT_RXOVERFLOW    0x00100000
#define TMIO_STAT_TXUNDERRUN    0x00200000
#define TMIO_STAT_CMDTIMEOUT    0x00400000
#define TMIO_STAT_RXRDY         0x01000000
#define TMIO_STAT_TXRQ          0x02000000
#define TMIO_STAT_ILL_FUNC      0x20000000
#define TMIO_STAT_CMD_BUSY      0x40000000
#define TMIO_STAT_ILL_ACCESS    0x80000000

/* Define some IRQ masks */
/* This is the mask used at reset by the chip */
#define TMIO_MASK_ALL           0x837f031d
#define TMIO_MASK_READOP  (TMIO_STAT_RXRDY | TMIO_STAT_DATAEND | \
                           TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_WRITEOP (TMIO_STAT_TXRQ | TMIO_STAT_DATAEND | \
                           TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_CMD     (TMIO_STAT_CMDRESPEND | TMIO_STAT_CMDTIMEOUT | \
                           TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_IRQ     (TMIO_MASK_READOP | TMIO_MASK_WRITEOP | TMIO_MASK_CMD)

#define enable_mmc_irqs(ctl, i) \
	do { \
		u32 mask;\
		mask  = tmio_ioread32((ctl)->irq_mask); \
		mask &= ~((i) & TMIO_MASK_IRQ); \
		tmio_iowrite32(mask, (ctl)->irq_mask); \
	} while (0)

#define disable_mmc_irqs(ctl, i) \
	do { \
		u32 mask;\
		mask  = tmio_ioread32((ctl)->irq_mask); \
		mask |= ((i) & TMIO_MASK_IRQ); \
		tmio_iowrite32(mask, (ctl)->irq_mask); \
	} while (0)

#define ack_mmc_irqs(ctl, i) \
	do { \
		u32 mask;\
		mask  = tmio_ioread32((ctl)->status); \
		mask &= ~((i) & TMIO_MASK_IRQ); \
		tmio_iowrite32(mask, (ctl)->status); \
	} while (0)


struct tmio_mmc_host {
	struct tmio_mmc_cnf __iomem *cnf;
	struct tmio_mmc_ctl __iomem *ctl;
	struct mmc_command      *cmd;
	struct mmc_request      *mrq;
	struct mmc_data         *data;
	struct mmc_host         *mmc;
	int                     irq;

	/* pio related stuff */
	struct scatterlist      *sg_ptr;
	unsigned int            sg_len;
	unsigned int            sg_off;
};

#include <linux/scatterlist.h>
#include <linux/blkdev.h>

static inline void tmio_mmc_init_sg(struct tmio_mmc_host *host, struct mmc_data *data)
{
	host->sg_len = data->sg_len;
	host->sg_ptr = data->sg;
	host->sg_off = 0;
}

static inline int tmio_mmc_next_sg(struct tmio_mmc_host *host)
{
	host->sg_ptr++;
	host->sg_off = 0;
	return --host->sg_len;
}

static inline char *tmio_mmc_kmap_atomic(struct tmio_mmc_host *host, unsigned long *flags)
{
	struct scatterlist *sg = host->sg_ptr;

	local_irq_save(*flags);
	return kmap_atomic(sg_page(sg), KM_BIO_SRC_IRQ) + sg->offset;
}

static inline void tmio_mmc_kunmap_atomic(struct tmio_mmc_host *host, unsigned long *flags)
{
	kunmap_atomic(sg_page(host->sg_ptr), KM_BIO_SRC_IRQ);
	local_irq_restore(*flags);
}

#ifdef CONFIG_MMC_DEBUG
#define DBG(args...)    printk(args)

void debug_status(u32 status){
	printk("status: %08x = ", status);
	if(status & TMIO_STAT_CARD_REMOVE) printk("Card_removed ");
	if(status & TMIO_STAT_CARD_INSERT) printk("Card_insert ");
	if(status & TMIO_STAT_SIGSTATE) printk("Sigstate ");
	if(status & TMIO_STAT_WRPROTECT) printk("Write_protect ");
	if(status & TMIO_STAT_CARD_REMOVE_A) printk("Card_remove_A ");
	if(status & TMIO_STAT_CARD_INSERT_A) printk("Card_insert_A ");
	if(status & TMIO_STAT_SIGSTATE_A) printk("Sigstate_A ");
	if(status & TMIO_STAT_CMD_IDX_ERR) printk("Cmd_IDX_Err ");
	if(status & TMIO_STAT_STOPBIT_ERR) printk("Stopbit_ERR ");
	if(status & TMIO_STAT_ILL_FUNC) printk("ILLEGAL_FUNC ");
	if(status & TMIO_STAT_CMD_BUSY) printk("CMD_BUSY ");
	if(status & TMIO_STAT_CMDRESPEND)  printk("Response_end ");
	if(status & TMIO_STAT_DATAEND)     printk("Data_end ");
	if(status & TMIO_STAT_CRCFAIL)     printk("CRC_failure ");
	if(status & TMIO_STAT_DATATIMEOUT) printk("Data_timeout ");
	if(status & TMIO_STAT_CMDTIMEOUT)  printk("Command_timeout ");
	if(status & TMIO_STAT_RXOVERFLOW)  printk("RX_OVF ");
	if(status & TMIO_STAT_TXUNDERRUN)  printk("TX_UND ");
	if(status & TMIO_STAT_RXRDY)       printk("RX_rdy ");
	if(status & TMIO_STAT_TXRQ)        printk("TX_req ");
	if(status & TMIO_STAT_ILL_ACCESS)  printk("ILLEGAL_ACCESS ");
	printk("\n");
}
#else
#define DBG(fmt,args...)        do { } while (0)
#endif
