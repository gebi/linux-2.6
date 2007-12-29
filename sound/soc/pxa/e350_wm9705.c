/*
 * Based on tosa.c
 *
 * Copyright 2007 (c) Ian Molton <spyro@f2s.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation; version 2 ONLY.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/hardware.h>
#include <asm/arch/audio.h>

#include "../codecs/wm9705.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"

static struct snd_soc_machine e350;

static struct snd_soc_dai_link e350_dai[] = {
{
        .name = "AC97 Aux",
        .stream_name = "AC97 Aux",
        .cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
        .codec_dai = &wm9705_dai[WM9705_DAI_AC97_AUX],
},
};

static struct snd_soc_machine e350 = {
	.name = "Toshiba e350",
	.dai_link = e350_dai,
	.num_links = ARRAY_SIZE(e350_dai),
};

static struct snd_soc_device e350_snd_devdata = {
	.machine = &e350,
	.platform = &pxa2xx_soc_platform,
	.codec_dev = &soc_codec_dev_wm9705,
};

static struct platform_device *e350_snd_device;

static int __init e350_init(void)
{
	int ret;

	if (!machine_is_e350())
		return -ENODEV;

	e350_snd_device = platform_device_alloc("soc-audio", -1);
	if (!e350_snd_device)
		return -ENOMEM;

	platform_set_drvdata(e350_snd_device, &e350_snd_devdata);
	e350_snd_devdata.dev = &e350_snd_device->dev;
	ret = platform_device_add(e350_snd_device);

	if (ret)
		platform_device_put(e350_snd_device);

	return ret;
}

static void __exit e350_exit(void)
{
	platform_device_unregister(e350_snd_device);
}

module_init(e350_init);
module_exit(e350_exit);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e350");
MODULE_LICENSE("GPL");
