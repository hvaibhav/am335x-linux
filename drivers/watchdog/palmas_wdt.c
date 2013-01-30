/*
 * Driver for Watchdog part of Palmas PMIC Chips
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 *
 * Based on twl4030_wdt.c
 *
 * Author: Timo Kokkonen <timo.t.kokkonen at nokia.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mfd/palmas.h>

static struct platform_device *palmas_wdt_dev;

struct palmas_wdt {
	struct palmas *palmas;

	struct miscdevice miscdev;
	int timer_margin;
	unsigned long state;
};

#define PALMAS_WDT_ENABLED	(1<<1)

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int palmas_wdt_write(struct palmas *palmas, unsigned int data)
{
	int slave;
	unsigned int addr;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_PMU_CONTROL_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_PMU_CONTROL_BASE, PALMAS_WATCHDOG);

	return regmap_write(palmas->regmap[slave], addr, data);
}

static int palmas_wdt_enable(struct palmas_wdt *wdt)
{
	return palmas_wdt_write(wdt->palmas,
			wdt->timer_margin | PALMAS_WATCHDOG_ENABLE);
}

static int palmas_wdt_disable(struct palmas_wdt *wdt)
{
	return palmas_wdt_write(wdt->palmas, wdt->timer_margin);
}

static int palmas_wdt_set_timeout(struct palmas_wdt *wdt, int timeout)
{
	if (timeout < 1 || timeout > 128) {
		dev_warn(wdt->miscdev.parent,
			"Timeout can only be in the range [1-128] seconds");
		return -EINVAL;
	}
	wdt->timer_margin = fls(timeout) - 1;
	return palmas_wdt_enable(wdt);
}

static ssize_t palmas_wdt_write_fop(struct file *file,
		const char __user *data, size_t len, loff_t *ppos)
{
	struct palmas_wdt *wdt = file->private_data;

	if (len)
		palmas_wdt_enable(wdt);

	return len;
}

static long palmas_wdt_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_margin;
	struct palmas_wdt *wdt = file->private_data;
	int time = 0;

	static const struct watchdog_info palmas_wd_ident = {
		.identity = "Palmas Watchdog",
		.options = WDIOF_SETTIMEOUT,
		.firmware_version = 0,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &palmas_wd_ident,
				sizeof(palmas_wd_ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_KEEPALIVE:
		palmas_wdt_enable(wdt);
		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, p))
			return -EFAULT;
		if (palmas_wdt_set_timeout(wdt, new_margin))
			return -EINVAL;

		time = 1 << wdt->timer_margin;

		return put_user(time, p);

	case WDIOC_GETTIMEOUT:
		time = 1 << wdt->timer_margin;

		return put_user(time, p);

	default:
		return -ENOTTY;
	}

	return 0;
}

static int palmas_wdt_open(struct inode *inode, struct file *file)
{
	struct palmas_wdt *wdt = platform_get_drvdata(palmas_wdt_dev);

	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &wdt->state))
		return -EBUSY;

	wdt->state |= PALMAS_WDT_ENABLED;
	file->private_data = (void *) wdt;

	palmas_wdt_enable(wdt);
	return nonseekable_open(inode, file);
}

static int palmas_wdt_release(struct inode *inode, struct file *file)
{
	struct palmas_wdt *wdt = file->private_data;
	if (nowayout) {
		dev_alert(wdt->miscdev.parent,
		       "Unexpected close, watchdog still running!\n");
		palmas_wdt_enable(wdt);
	} else {
		if (palmas_wdt_disable(wdt))
			return -EFAULT;
		wdt->state &= ~PALMAS_WDT_ENABLED;
	}

	clear_bit(0, &wdt->state);
	return 0;
}

static const struct file_operations palmas_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= palmas_wdt_open,
	.release	= palmas_wdt_release,
	.unlocked_ioctl	= palmas_wdt_ioctl,
	.write		= palmas_wdt_write_fop,
};

static int palmas_wdt_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_wdt *wdt;
	int ret = 0;

	wdt = kzalloc(sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->palmas		= palmas;
	wdt->state		= 0;
	wdt->timer_margin	= 7;
	wdt->miscdev.parent	= &pdev->dev;
	wdt->miscdev.fops	= &palmas_wdt_fops;
	wdt->miscdev.minor	= WATCHDOG_MINOR;
	wdt->miscdev.name	= "watchdog";

	platform_set_drvdata(pdev, wdt);

	palmas_wdt_dev = pdev;

	palmas_wdt_disable(wdt);

	ret = misc_register(&wdt->miscdev);
	if (ret) {
		dev_err(wdt->miscdev.parent,
			"Failed to register misc device\n");
		platform_set_drvdata(pdev, NULL);
		kfree(wdt);
		palmas_wdt_dev = NULL;
		return ret;
	}
	return 0;
}

static int palmas_wdt_remove(struct platform_device *pdev)
{
	struct palmas_wdt *wdt = platform_get_drvdata(pdev);

	if (wdt->state & PALMAS_WDT_ENABLED)
		if (palmas_wdt_disable(wdt))
			return -EFAULT;

	wdt->state &= ~PALMAS_WDT_ENABLED;
	misc_deregister(&wdt->miscdev);

	platform_set_drvdata(pdev, NULL);
	kfree(wdt);
	palmas_wdt_dev = NULL;

	return 0;
}

#ifdef CONFIG_PM
static int palmas_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct palmas_wdt *wdt = platform_get_drvdata(pdev);
	if (wdt->state & PALMAS_WDT_ENABLED)
		return palmas_wdt_disable(wdt);

	return 0;
}

static int palmas_wdt_resume(struct platform_device *pdev)
{
	struct palmas_wdt *wdt = platform_get_drvdata(pdev);
	if (wdt->state & PALMAS_WDT_ENABLED)
		return palmas_wdt_enable(wdt);

	return 0;
}
#else
#define palmas_wdt_suspend        NULL
#define palmas_wdt_resume         NULL
#endif

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-wdt", },
	{ /* end */ }
};

static struct platform_driver palmas_wdt_driver = {
	.probe = palmas_wdt_probe,
	.remove = palmas_wdt_remove,
	.suspend = palmas_wdt_suspend,
	.resume = palmas_wdt_resume,
	.driver = {
		.owner = THIS_MODULE,
		.of_match_table = of_palmas_match_tbl,
		.name = "palmas-wdt",
	},
};

static int __init palmas_wdt_init(void)
{
	return platform_driver_register(&palmas_wdt_driver);
}
module_init(palmas_wdt_init);

static void __exit palmas_wdt_exit(void)
{
	platform_driver_unregister(&palmas_wdt_driver);
}
module_exit(palmas_wdt_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:palmas-wdt");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
