/*
 * Freescale USB-FS OTG Host Controller Driver (Kirin Host Controller).
 * Implements support for USB Full-Speed controller available in Kinetis
 * controllers (K70,K60,...) and some Coldfire controllers (MCF52259,...).
 *
 * Copyright 2013 EmCraft Systems www.emcraft.com
 * Author: Yuri Tikhonov <yur@emcraft.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*****************************************************************************
 * Include files:
 *****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/dmapool.h>
#include <linux/clk.h>
#include <linux/workqueue.h>

#include "khci.h"

#if defined(CONFIG_ARCH_KINETIS)
#include <mach/kinetis.h>
#endif

/*****************************************************************************
 * Local macros and constants:
 *****************************************************************************/

MODULE_DESCRIPTION("Freescale KHCI (USB-FS) Host Controller Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuri Tikhonov");
MODULE_ALIAS("platform:khci_hcd");

static const char hcd_name[] = "khci-hcd";

/*****************************************************************************
 * Static routines prototypes:
 *****************************************************************************/
/*
 * Standard Linux Module API
 */
static int khci_probe(struct platform_device *);
static int __init_or_module khci_remove(struct platform_device *);
#ifdef CONFIG_PM
static int khci_bus_suspend(struct usb_hcd *hcd);
static int khci_bus_resume(struct usb_hcd *hcd);
static int khci_runtime_suspend(struct device *dev);
static int khci_runtime_resume(struct device *dev);
#endif

/*
 * Standard Linux USB HCD API
 */
static int khci_hc_start(struct usb_hcd *hcd);
static void khci_hc_stop(struct usb_hcd *hcd);
static void khci_hc_endpoint_dis(struct usb_hcd *, struct usb_host_endpoint *);
static int khci_hc_get_frame(struct usb_hcd *);

static const struct of_device_id khci_dt_ids[] = {
	{ .compatible = "fsl,kinetis-khci" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, khci_dt_ids);

/*****************************************************************************
 * Variables local to this module:
 *****************************************************************************/

/*
 * PM ops
 */
#ifdef CONFIG_PM
static const struct dev_pm_ops khci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(khci_runtime_suspend, khci_runtime_resume)
	SET_RUNTIME_PM_OPS(khci_runtime_suspend, khci_runtime_resume, NULL)
};
#endif

/*
 * USB-FS platform driver instance
 */
static struct platform_driver khci_driver = {
	.probe		= khci_probe,
	.remove		= khci_remove,
	.driver		= {
		.name           = (char *)hcd_name,
		.owner          = THIS_MODULE,
		.of_match_table = khci_dt_ids,
#ifdef CONFIG_PM
		.pm             = &khci_pm_ops,
#endif
	},
};

/*
 * USB-FS HCD driver instance
 */
static struct hc_driver khci_hc_drv = {
	.description		= hcd_name,
	.hcd_priv_size		= sizeof(struct khci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= khci_hc_irq,
	.flags			= HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start			= khci_hc_start,
	.stop			= khci_hc_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= khci_hc_urb_enqueue,
	.urb_dequeue		= khci_hc_urb_dequeue,
	.endpoint_disable	= khci_hc_endpoint_dis,

	/*
	 * periodic schedule support
	 */
	.get_frame_number	= khci_hc_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= khci_hc_hub_status_data,
	.hub_control		= khci_hc_hub_control,

#ifdef CONFIG_PM
	.bus_suspend		= khci_bus_suspend,
	.bus_resume		= khci_bus_resume,
#endif
};

static struct dma_pool		*khci_bdt_pool;

/*****************************************************************************
 * Linux module entry points:
 *****************************************************************************/
/*
 * Init module
 */
static int __init khci_init(void)
{
	int	rv;

	if (usb_disabled()) {
		rv = -ENODEV;
		goto out;
	}

	printk(KERN_INFO KBUILD_MODNAME ": driver %s\n", hcd_name);

	rv = platform_driver_register(&khci_driver);
out:
	return rv;
}
module_init(khci_init);

/*
 * Cleanup module
 */
static void __exit khci_cleanup(void)
{
	platform_driver_unregister(&khci_driver);

	if (khci_bdt_pool) {
		dma_pool_destroy(khci_bdt_pool);
		khci_bdt_pool = 0;
	}
}
module_exit(khci_cleanup);

/*****************************************************************************
 * Platform driver API:
 *****************************************************************************/
/*
 * Probe device
 */
static int khci_probe(struct platform_device *pdev)
{
	struct resource	*mres, *ires;
	struct usb_hcd	*hcd = NULL;
	void __iomem	*reg = NULL;
	struct khci_hcd	*dev = NULL;
	unsigned long	msk;
	int		rv, irq;

	dbg(5, "%s\n", __func__);

	/*
	 * Verify and get resources
	 */
	mres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mres) {
		rv = -ENODEV;
		dev_err(&pdev->dev, "platform_get_resource MEM error.\n");
		goto out;
	}

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires) {
		rv = -ENODEV;
		dev_err(&pdev->dev, "platform_get_resource IRQ error.\n");
		goto out;
	}

	irq = ires->start;
	msk = ires->flags & IRQF_TRIGGER_MASK;

	reg = ioremap(mres->start, resource_size(mres));
	if (!reg) {
		rv = -ENOMEM;
		dev_err(&pdev->dev, "ioremap error.\n");
		goto out;
	}

	/*
	 * Create and init HCD
	 */
	hcd = usb_create_hcd(&khci_hc_drv, &pdev->dev, (char *)hcd_name);
	if (!hcd) {
		rv = -ENOMEM;
		dev_err(&pdev->dev, "failed to create hcd\n");
		goto out;
	}

	/*
	 * This is just for nice information to console
	 */
	hcd->rsrc_start = mres->start;

	/*
	 * Driver doesn't support WAKE-UP detection for now
	 */
	hcd->cant_recv_wakeups = 1;

	dev = hcd_to_khci(hcd);
	memset(dev, 0, sizeof(struct khci_hcd));
	dev_set_drvdata(&pdev->dev, dev);
	dev->dev = &pdev->dev;

	/*
	 * Allocate pool for BDT tables (2 BDs per dir (x2) per EP (x16)).
	 * Actually only EP0 BDs are used, but BDT must be aligned on 512B
	 */
	khci_bdt_pool = dma_pool_create("khci_bdt", &pdev->dev,
					2 * 2 * sizeof(struct khci_bd),
					2 * 2 * 16 * sizeof(struct khci_bd),
					0);

	if (!khci_bdt_pool) {
		rv = -ENOMEM;
		dev_err(&pdev->dev, "failed to create dma pool\n");
		goto out;
	}

	dev->wq = create_workqueue(hcd_name);
	if (!dev->wq) {
		rv = -ENOMEM;
		dev_err(&pdev->dev, "failed create wq\n");
		goto out;
	}
	INIT_WORK(&dev->wrk, khci_worker);

#if defined(CONFIG_ARCH_KINETIS)
	/* FIXME: configure khci clocks via the clk framework */
	KINETIS_SIM_RESET(sopt2, KINETIS_SIM_SOPT2_USBFSRC_MSK);
	KINETIS_SIM_SET(sopt2, (1 << KINETIS_SIM_SOPT2_USBFSRC_BITS) |
		KINETIS_SIM_SOPT2_USBF_CLKSEL);
	KINETIS_SIM_RESET(clkdiv2, KINETIS_SIM_CLKDIV2_USBFSFRAC_MSK |
		KINETIS_SIM_CLKDIV2_USBFSDIV_MSK);
	KINETIS_SIM_SET(clkdiv2, (1 << KINETIS_SIM_CLKDIV2_USBFSDIV_BIT));
#endif

	/*
	 * Enable clocks
	 */
	dev->clk = clk_get(&pdev->dev, "khci");
	if (IS_ERR(dev->clk)) {
		rv = -EFAULT;
		dev->clk = 0;
		dev_err(&pdev->dev, "failed get khci clock\n");
		goto out;
	}
	clk_prepare_enable(dev->clk);

	dev->reg = reg;

	INIT_LIST_HEAD(&dev->ctrl_lst);
	INIT_LIST_HEAD(&dev->intr_lst);
	INIT_LIST_HEAD(&dev->bulk_lst);
	INIT_LIST_HEAD(&dev->td_done_lst);

	dev->bd[0] = dev->bd[1] = 0;

	dev->bdt = dma_pool_alloc(khci_bdt_pool, GFP_ATOMIC, &dev->bdt_hdl);
	if (!dev->bdt) {
		rv = -ENOMEM;
		dev_err(&pdev->dev, "failed to alloc bdt\n");
		goto out;
	}

	rv = usb_add_hcd(hcd, irq, /*IRQF_DISABLED |*/ msk);
	if (rv) {
		rv = -ENODEV;
		dev_err(&pdev->dev, "failed to add hcd\n");
		goto out;
	}
out:
	if (rv) {
		if (dev) {
			if (dev->bdt) {
				dma_pool_free(khci_bdt_pool, dev->bdt,
					      dev->bdt_hdl);
			}
			if (dev->wq)
				destroy_workqueue(dev->wq);
			if (dev->clk)
				clk_disable(dev->clk);
		}

		if (hcd)
			usb_put_hcd(hcd);

		if (reg)
			iounmap(reg);
	}

	return rv;
}

/*
 * Remove device
 */
static int __init_or_module khci_remove(struct platform_device *pdev)
{
	struct khci_hcd	*dev = dev_get_drvdata(&pdev->dev);
	struct usb_hcd	*hcd = khci_to_hcd(dev);
	struct khci_ep	*kep, *tmp;

	list_for_each_entry_safe(kep, tmp, &dev->ctrl_lst, node)
		khci_hc_endpoint_dis(hcd, kep->hep);
	list_for_each_entry_safe(kep, tmp, &dev->intr_lst, node)
		khci_hc_endpoint_dis(hcd, kep->hep);
	list_for_each_entry_safe(kep, tmp, &dev->bulk_lst, node)
		khci_hc_endpoint_dis(hcd, kep->hep);

	usb_remove_hcd(hcd);

	dma_pool_free(khci_bdt_pool, dev->bdt, dev->bdt_hdl);
	iounmap(dev->reg);

	usb_put_hcd(hcd);

	return 0;
}

/*****************************************************************************
 * USB framework API:
 *****************************************************************************/
/*
 * Start HCD
 */
static int khci_hc_start(struct usb_hcd *hcd)
{
	struct khci_hcd			*dev = hcd_to_khci(hcd);
	volatile struct khci_reg	*reg = dev->reg;

	dbg(1, "%s\n", __func__);

	dev->vrh.hub.wHubStatus = 0;
	dev->vrh.hub.wHubChange = 0;
	dev->vrh.port.wPortStatus = 0;
	dev->vrh.port.wPortChange = 0;

	/*
	 * Chip has been reset, VBUS power is off
	 */
	hcd->state = HC_STATE_RUNNING;

	memset(&dev->stat, 0, sizeof(dev->stat));

	/*
	 * Clear interrupt statuses
	 */
	reg->istat = 0xFF;
	reg->erren = KHCI_ERR_MSK;

	/*
	 * Enable weak pull-downs, usefull for detecting detach;
	 * remove suspend state
	 */
	reg->usbctrl |= KHCI_USBCTRL_PDE;
	reg->usbctrl &= ~KHCI_USBCTRL_SUSP;

	reg->bdtpage1 = (u32)dev->bdt >> 8;
	reg->bdtpage2 = (u32)dev->bdt >> 16;
	reg->bdtpage3 = (u32)dev->bdt >> 24;

	reg->ctl |= KHCI_CTL_ODDRST;

	memset(dev->bd, 0, sizeof(dev->bd));
	memset(dev->bdt, 0, sizeof(struct khci_bd) * 4);

	/*
	 * Enable host mode, and ATTACH interrupt
	 */
	reg->ctl = KHCI_CTL_HOSTMODEEN;
	reg->inten = KHCI_INT_ATTACH | KHCI_INT_ERROR;

	return 0;
}

/*
 * Stop HCD
 */
static void khci_hc_stop(struct usb_hcd *hcd)
{
	struct khci_hcd			*dev = hcd_to_khci(hcd);
	volatile struct khci_reg	*reg = dev->reg;

	dbg(1, "%s\n", __func__);

	reg->inten = 0;
	reg->ctl = 0;
	reg->istat = 0xFF;
}

/*
 * Disable end-point
 */
static void khci_hc_endpoint_dis(struct usb_hcd *hcd,
				 struct usb_host_endpoint *hep)
{
	struct khci_ep	*kep;
	unsigned long	flags;

	dbg(1, "%s EP:%p.%p\n", __func__, hep, hep->hcpriv);

	local_irq_save(flags);

	kep = hep->hcpriv;
	if (kep)
		khci_ep_free(kep);

	local_irq_restore(flags);
}

/*
 * Get frame number
 */
static int khci_hc_get_frame(struct usb_hcd *hcd)
{
	dbg(1, "%s\n", __func__);

	return 0;
}

#ifdef CONFIG_PM
static int khci_bus_suspend(struct usb_hcd *hcd)
{
	/* TODO */
	return 0;
}

static int khci_bus_resume(struct usb_hcd *hcd)
{
	/* TODO */
	return 0;
}

static int khci_runtime_suspend(struct device *dev)
{
	struct khci_hcd * hcd = dev_get_drvdata(dev);
	clk_disable_unprepare(hcd->clk);
	return 0;
}

static int khci_runtime_resume(struct device *dev)
{
	struct khci_hcd * hcd = dev_get_drvdata(dev);
	clk_prepare_enable(hcd->clk);
	return 0;
}
#endif

