#ifndef MFD_TMIO_H
#define MFD_TMIO_H

#include <linux/io.h>
#include <linux/platform_device.h>

struct fb_videomode;

/*
 * data for the NAND controller
 */
struct tmio_nand_data {
	struct nand_bbt_descr	*badblock_pattern;
	struct mtd_partition	*partition;
	unsigned int		num_partitions;
};

struct tmio_fb_data {
	int			(*lcd_set_power)(struct platform_device *fb_dev,
								bool on);
	int			(*lcd_mode)(struct platform_device *fb_dev,
						struct fb_videomode *mode);
	int			num_modes;
	struct fb_videomode	*modes;
};

static u32 __maybe_unused tmio_ioread32(const void __iomem *addr)
{
	return ((u32) ioread16(addr)) | (((u32) ioread16(addr + 2)) << 16);
}

static u32 __maybe_unused tmio_iowrite32(u32 val, const void __iomem *addr)
{
	iowrite16(val,		addr);
	iowrite16(val >> 16,	addr + 2);
	return val;
}

#define FBIO_TMIO_ACC_WRITE	0x7C639300
#define FBIO_TMIO_ACC_SYNC	0x7C639301

#define TMIO_MMC_CONFIG         "tmio-mmc-config"
#define TMIO_MMC_CONTROL        "tmio-mmc-control"
#define TMIO_MMC_IRQ            "tmio-mmc"

#define TMIO_NAND_CONFIG	"tmio-nand-config"
#define TMIO_NAND_CONTROL	"tmio-nand-control"
#define TMIO_NAND_IRQ		"tmio-nand"

#define TMIO_FB_CONFIG		"tmio-fb-config"
#define TMIO_FB_CONTROL		"tmio-fb-control"
#define TMIO_FB_VRAM		"tmio-fb-vram"
#define TMIO_FB_IRQ		"tmio-fb"

#define TMIO_OHCI_CONFIG	"tmio-ohci-config"
#define TMIO_OHCI_CONTROL	"tmio-ohci-control"
#define TMIO_OHCI_SRAM		"tmio-ohci-sram"
#define TMIO_OHCI_SRAM_ALIAS	"tmio-ohci-sram-alias"
#define TMIO_OHCI_IRQ		"tmio-ohci"

#endif

