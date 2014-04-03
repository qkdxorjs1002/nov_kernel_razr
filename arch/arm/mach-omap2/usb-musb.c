/*
 * linux/arch/arm/mach-omap2/usb-musb.c
 *
 * This file will contain the board specific details for the
 * MENTOR USB OTG controller on OMAP3430
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Vikram Pandita
 *
 * Generalization by:
 * Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <linux/usb/musb.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/am35xx.h>
#include <plat/usb.h>
#include <plat/omap_device.h>
#include "mux.h"

#include <asm/mach-types.h>

#define CONTROL_DEV_CONF                0x300
#define PHY_PD				(1 << 0)

#ifdef CONFIG_ARCH_OMAP4
#define DIE_ID_REG_BASE         (L4_44XX_PHYS + 0x2000)
#define DIE_ID_REG_OFFSET               0x200
#else
#define DIE_ID_REG_BASE         (L4_WK_34XX_PHYS + 0xA000)
#define DIE_ID_REG_OFFSET               0x218
#endif /* CONFIG_ARCH_OMAP4 */

#define CLKSEL_60M       24      /* Bit position */
#define CLKSEL_UTMI      (0 << CLKSEL_60M)
#define CLKSEL_ULPI      (1 << CLKSEL_60M)

#define OPTFCLKEN_XCLK   8	/* Bit Position */
#define OPTFCLK_DISABLE  (0 << OPTFCLKEN_XCLK)
#define OPTFCLK_ENABLE   (1 << OPTFCLKEN_XCLK)
#define L3INIT_HSUSBOTG_CLKCTRL   0x9360
#if defined(CONFIG_USB_MUSB_OMAP2PLUS) || defined (CONFIG_USB_MUSB_AM35X)

static struct musb_hdrc_config musb_config = {
	.multipoint	= 1,
	.dyn_fifo	= 1,
	.num_eps	= 16,
	.ram_bits	= 12,
};

static struct musb_hdrc_platform_data musb_plat = {
#ifdef CONFIG_USB_MUSB_OTG
	.mode		= MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
	.mode		= MUSB_HOST,
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
	.mode		= MUSB_PERIPHERAL,
#endif
	/* .clock is set dynamically */
	.config		= &musb_config,

	/* REVISIT charge pump on TWL4030 can supply up to
	 * 100 mA ... but this value is board-specific, like
	 * "mode", and should be passed to usb_musb_init().
	 */
	.power		= 50,			/* up to 100 mA */
};

static u64 musb_dmamask = DMA_BIT_MASK(32);

static struct omap_device_pm_latency omap_musb_latency[] = {
	{
		.deactivate_func	= omap_device_idle_hwmods,
		.activate_func		= omap_device_enable_hwmods,
		.flags			= OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

static void usb_musb_mux_init(struct omap_musb_board_data *board_data)
{
	switch (board_data->interface_type) {
	case MUSB_INTERFACE_UTMI:
		omap_mux_init_signal("usba0_otg_dp", OMAP_PIN_INPUT);
		omap_mux_init_signal("usba0_otg_dm", OMAP_PIN_INPUT);
		break;
	case MUSB_INTERFACE_ULPI:
		omap_mux_init_signal("usba0_ulpiphy_clk",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_stp",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dir",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_nxt",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat0",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat1",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat2",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat3",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat4",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat5",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat6",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat7",
						OMAP_PIN_INPUT_PULLDOWN);
		break;
	default:
		break;
	}
}

static struct omap_musb_board_data musb_default_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

void __init usb_musb_init(struct omap_musb_board_data *musb_board_data)
{
	struct omap_hwmod		*oh;
	struct omap_device		*od;
	struct platform_device		*pdev;
	struct device			*dev = NULL;
	int				bus_id = -1;
	const char			*oh_name, *name;
	struct omap_musb_board_data	*board_data;
	int				usb_clkctrl = 0;

	if (musb_board_data)
		board_data = musb_board_data;
	else
		board_data = &musb_default_board_data;

	/*
	 * REVISIT: This line can be removed once all the platforms using
	 * musb_core.c have been converted to use use clkdev.
	 */
	musb_plat.clock = "ick";
	musb_plat.board_data = board_data;
	musb_plat.power = board_data->power >> 1;
	musb_plat.mode = board_data->mode;
	musb_plat.extvbus = board_data->extvbus;

	if (cpu_is_omap3517() || cpu_is_omap3505()) {
		oh_name = "am35x_otg_hs";
		name = "musb-am35x";
	} else {
		oh_name = "usb_otg_hs";
		name = "musb-omap2430";
	}

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		return;
	}

	od = omap_device_build(name, bus_id, oh, &musb_plat,
			       sizeof(musb_plat), omap_musb_latency,
			       ARRAY_SIZE(omap_musb_latency), false);
	if (IS_ERR(od)) {
		pr_err("Could not build omap_device for %s %s\n",
						name, oh_name);
		return;
	}

	pdev = &od->pdev;
	dev = &pdev->dev;
	get_device(dev);
	dev->dma_mask = &musb_dmamask;
	dev->coherent_dma_mask = musb_dmamask;
	put_device(dev);

	if (machine_is_mapphone()) {
		void __iomem *l4_base = ioremap(L4_44XX_BASE, SZ_4K);

		if (WARN_ON(!l4_base))
			return;

		usb_clkctrl = __raw_readl(l4_base +
				L3INIT_HSUSBOTG_CLKCTRL);
		printk(KERN_INFO "USB MUSB Init-Initial value "
				"of CLKCTRL is 0x%x \n", usb_clkctrl);
		if (board_data->interface_type == MUSB_INTERFACE_UTMI)
			usb_clkctrl &= ~(CLKSEL_ULPI | OPTFCLK_ENABLE);
		else
			usb_clkctrl |= CLKSEL_ULPI | OPTFCLK_ENABLE;
		__raw_writel(usb_clkctrl,
				l4_base+L3INIT_HSUSBOTG_CLKCTRL);
		usb_clkctrl = __raw_readl(l4_base +
				L3INIT_HSUSBOTG_CLKCTRL);
		printk(KERN_INFO "USB MUSB Post-Initial value "
				"of CLKCTRL is 0x%x \n", usb_clkctrl);
	}
	/*powerdown the phy*/
	omap_writel(PHY_PD, DIE_ID_REG_BASE + CONTROL_DEV_CONF);
}
#else
void __init usb_musb_init(struct omap_musb_board_data *board_data)
{
	if (cpu_is_omap44xx())
		omap4430_phy_init(NULL);
}
#endif /* CONFIG_USB_MUSB_SOC */
