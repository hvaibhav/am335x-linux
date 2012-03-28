/*
 * Palmas Real Time Clock interface
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 *
 * Based on rtc-twl.c
 * Copyright (C) 2007 MontaVista Software, Inc
 * Author: Alexandre Rusev <source@mvista.com>
 *
 * Based on original TI driver twl4030-rtc.c
 *   Copyright (C) 2006 Texas Instruments, Inc.
 *
 * Based on rtc-omap.c
 *   Copyright (C) 2003 MontaVista Software, Inc.
 *   Author: George G. Davis <gdavis@mvista.com> or <source@mvista.com>
 *   Copyright (C) 2006 David Brownell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mfd/palmas.h>

#define ALL_TIME_REGS	6

struct palmas_rtc {
	struct palmas *palmas;
	struct device *dev;
	struct rtc_device *rtc;
	unsigned int irq_bits;
	int irq;
};

static int palmas_rtc_read(struct palmas *palmas, unsigned int reg,
		unsigned int *dest)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RTC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RTC_BASE, reg);

	return regmap_read(palmas->regmap[slave], addr, dest);
}

static int palmas_rtc_write(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RTC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RTC_BASE, reg);

	return regmap_write(palmas->regmap[slave], addr, data);
}

static int palmas_rtc_read_block(struct palmas *palmas, unsigned int reg,
		u8 *dest, size_t count)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RTC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RTC_BASE, reg);

	return regmap_bulk_read(palmas->regmap[slave], addr, dest, count);
}

static int palmas_rtc_write_block(struct palmas *palmas, unsigned int reg,
		u8 *src, size_t count)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RTC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RTC_BASE, reg);

	return regmap_raw_write(palmas->regmap[slave], addr, src, count);
}

static int palmas_rtc_setbits(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RESOURCE_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE, reg);

	return regmap_update_bits(palmas->regmap[slave], addr, data, data);
}

static int palmas_rtc_clrbits(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RESOURCE_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE, reg);

	return regmap_update_bits(palmas->regmap[slave], addr, data, 0);
}

/*
 * Gets current TWL RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int palmas_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);
	struct palmas *palmas = palmas_rtc->palmas;
	unsigned char rtc_data[ALL_TIME_REGS];
	int ret;

	ret = palmas_rtc_setbits(palmas, PALMAS_RTC_CTRL_REG,
				PALMAS_RTC_CTRL_REG_GET_TIME);
	if (ret < 0) {
		dev_err(dev, "Failed to update RTC_CTRL %d\n", ret);
		goto out;
	}

	ret = palmas_rtc_read_block(palmas,PALMAS_SECONDS_REG, rtc_data,
				   ALL_TIME_REGS);

	if (ret < 0) {
		dev_err(dev, "Failed to read time block %d\n", ret);
		goto out;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;

	ret = palmas_rtc_clrbits(palmas, PALMAS_RTC_CTRL_REG,
			PALMAS_RTC_CTRL_REG_GET_TIME);
	if (ret < 0)
		dev_err(dev, "Failed to update RTC_CTRL %d\n", ret);

out:
	return ret;
}

static int palmas_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);
	struct palmas *palmas = palmas_rtc->palmas;
	unsigned char rtc_data[ALL_TIME_REGS];
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);

	/* Stop RTC while updating the TC registers */
	ret = palmas_rtc_clrbits(palmas, PALMAS_RTC_CTRL_REG,
			PALMAS_RTC_CTRL_REG_STOP_RTC);
	if (ret < 0) {
		dev_err(dev, "Failed to stop RTC %d\n", ret);
		goto out;
	}

	/* update all the time registers in one shot */
	ret = palmas_rtc_write_block(palmas, PALMAS_SECONDS_REG, rtc_data,
				     ALL_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "Failed to write time block %d\n", ret);
		goto out;
	}

	/* Start back RTC */
	ret = palmas_rtc_setbits(palmas, PALMAS_RTC_CTRL_REG,
			PALMAS_RTC_CTRL_REG_STOP_RTC);
	if (ret < 0) {
		dev_err(dev, "Failed to start RTC %d\n", ret);
		goto out;
	}

out:
	return ret;
}

static int palmas_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);
	struct palmas *palmas = palmas_rtc->palmas;
	int ret;

	if (enabled) {
		ret = palmas_rtc_setbits(palmas, PALMAS_RTC_INTERRUPTS_REG,
				PALMAS_RTC_INTERRUPTS_REG_IT_ALARM);
		if (ret)
			dev_err(palmas_rtc->dev,
				"failed to set RTC alarm IRQ %d\n", ret);

		palmas_rtc->irq_bits |= PALMAS_RTC_INTERRUPTS_REG_IT_ALARM;
	} else {
		ret = palmas_rtc_clrbits(palmas, PALMAS_RTC_INTERRUPTS_REG,
				PALMAS_RTC_INTERRUPTS_REG_IT_ALARM);
		if (ret)
			dev_err(palmas_rtc->dev,
				"failed to clear RTC alarm IRQ %d\n", ret);

		palmas_rtc->irq_bits &= ~PALMAS_RTC_INTERRUPTS_REG_IT_ALARM;
	}

	return ret;
}

/*
 * Gets current TWL RTC alarm time.
 */
static int palmas_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);
	struct palmas *palmas = palmas_rtc->palmas;
	unsigned char rtc_data[ALL_TIME_REGS];
	int ret;

	ret = palmas_rtc_read_block(palmas, PALMAS_ALARM_SECONDS_REG, rtc_data,
				    ALL_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "Failed to read alarm block %d\n", ret);
		goto out;
	}

	/* some of these fields may be wildcard/"match all" */
	alm->time.tm_sec = bcd2bin(rtc_data[0]);
	alm->time.tm_min = bcd2bin(rtc_data[1]);
	alm->time.tm_hour = bcd2bin(rtc_data[2]);
	alm->time.tm_mday = bcd2bin(rtc_data[3]);
	alm->time.tm_mon = bcd2bin(rtc_data[4]) - 1;
	alm->time.tm_year = bcd2bin(rtc_data[5]) + 100;

	/* report cached alarm enable state */
	if (palmas_rtc->irq_bits & PALMAS_RTC_INTERRUPTS_REG_IT_ALARM)
		alm->enabled = 1;

out:
	return ret;
}

static int palmas_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);
	struct palmas *palmas = palmas_rtc->palmas;
	unsigned char alarm_data[ALL_TIME_REGS];
	int ret;

	ret = palmas_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		goto out;

	alarm_data[0] = bin2bcd(alm->time.tm_sec);
	alarm_data[1] = bin2bcd(alm->time.tm_min);
	alarm_data[2] = bin2bcd(alm->time.tm_hour);
	alarm_data[3] = bin2bcd(alm->time.tm_mday);
	alarm_data[4] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[5] = bin2bcd(alm->time.tm_year - 100);

	/* update all the alarm registers in one shot */
	ret = palmas_rtc_write_block(palmas, PALMAS_ALARM_SECONDS_REG,
				     alarm_data, ALL_TIME_REGS);
	if (ret) {
		dev_err(dev, "Failed to write alarm block %d\n", ret);
		goto out;
	}

	if (alm->enabled) {
		ret = palmas_rtc_alarm_irq_enable(dev, 1);
	}

out:
	return ret;
}

static irqreturn_t palmas_rtc_interrupt(int irq, void *rtc)
{
	struct palmas_rtc *palmas_rtc = rtc;
	struct palmas *palmas = palmas_rtc->palmas;
	unsigned long events = 0;
	int ret = IRQ_NONE;
	int res;
	unsigned int rd_reg;

	res = palmas_rtc_read(palmas, PALMAS_RTC_STATUS_REG, &rd_reg);
	if (res) {
		dev_err(palmas_rtc->dev, "Failed to read IRQ sts %d\n, res",
				res);
		goto out;
	}

	/*
	 * Figure out source of interrupt: ALARM or TIMER in RTC_STATUS_REG.
	 * only one (ALARM or RTC) interrupt source may be enabled
	 * at time, we also could check our results
	 * by reading RTS_INTERRUPTS_REGISTER[IT_TIMER,IT_ALARM]
	 */
	if (rd_reg & PALMAS_RTC_STATUS_REG_ALARM)
		events |= RTC_IRQF | RTC_AF;
	else
		events |= RTC_IRQF | RTC_UF;

	res = palmas_rtc_write(palmas, PALMAS_RTC_STATUS_REG,
			rd_reg | PALMAS_RTC_STATUS_REG_ALARM);
	if (res) {
		dev_err(palmas_rtc->dev, "Failed to clear IRQ sts %d\n, res",
				res);
		goto out;
	}

	/* Notify RTC core on event */
	rtc_update_irq(rtc, 1, events);

	ret = IRQ_HANDLED;
out:
	return ret;
}

static struct rtc_class_ops twl_rtc_ops = {
	.read_time	= palmas_rtc_read_time,
	.set_time	= palmas_rtc_set_time,
	.read_alarm	= palmas_rtc_read_alarm,
	.set_alarm	= palmas_rtc_set_alarm,
	.alarm_irq_enable = palmas_rtc_alarm_irq_enable,
};

/*----------------------------------------------------------------------*/

static int palmas_rtc_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_rtc *palmas_rtc;
	struct rtc_device *rtc;
	int ret = -EINVAL;
	int irq;
	unsigned int rd_reg;

	ret = palmas_rtc_read(palmas, PALMAS_RTC_STATUS_REG, &rd_reg);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC status %d\n", rd_reg);
		goto out1;
	}

	palmas_rtc = kzalloc(sizeof(*palmas_rtc), GFP_KERNEL);
	if (!palmas_rtc) {
		ret = -ENOMEM;
		goto out1;
	}

	if (rd_reg & PALMAS_RTC_STATUS_REG_POWER_UP)
		dev_warn(&pdev->dev, "Power up reset detected\n");

	if (rd_reg & PALMAS_RTC_STATUS_REG_ALARM)
		dev_warn(&pdev->dev, "Pending Alarm interrupt detected\n");

	/* Clear RTC Power up reset and pending alarm interrupts */
	ret = palmas_rtc_write(palmas, PALMAS_RTC_STATUS_REG, rd_reg);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC status %d\n", ret);
		goto out2;
	}

	/* Check RTC module status, Enable if it is off */
	ret = palmas_rtc_read(palmas, PALMAS_RTC_CTRL_REG, &rd_reg);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC ctrl %d\n", ret);
		goto out2;
	}

	if (!(rd_reg & PALMAS_RTC_CTRL_REG_STOP_RTC)) {
		dev_info(&pdev->dev, "Enabling Palmas RTC\n");
		rd_reg = PALMAS_RTC_CTRL_REG_STOP_RTC;
		ret = palmas_rtc_write(palmas, PALMAS_RTC_CTRL_REG, rd_reg);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to write RTC ctrl %d\n",
					ret);
			goto out2;
		}
	}

	/* init cached IRQ enable bits */
	ret = palmas_rtc_read(palmas, PALMAS_RTC_INTERRUPTS_REG,
			&palmas_rtc->irq_bits);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to cache IRQ bits %d\n", ret);
		goto out2;
	}

	palmas_rtc->palmas = palmas;
	palmas_rtc->dev = &pdev->dev;
	platform_set_drvdata(pdev, palmas_rtc);

	device_init_wakeup(&pdev->dev, 1);

	rtc = rtc_device_register(pdev->name,
				  &pdev->dev, &twl_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		dev_err(&pdev->dev, "can't register RTC device, err %d\n",
			ret);
		goto out2;
	}

	irq = regmap_irq_get_virq(palmas->irq_data, PALMAS_RTC_ALARM_IRQ);
	ret = request_threaded_irq(irq, NULL, palmas_rtc_interrupt,
				   IRQF_TRIGGER_RISING,
				   dev_name(&pdev->dev), palmas_rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ is not free\n");
		goto out3;
	}

	palmas_rtc->irq = irq;

	return 0;

out3:
	rtc_device_unregister(rtc);
out2:
	kfree(palmas_rtc);
out1:
	return ret;
}

/*
 * Disable all TWL RTC module interrupts.
 * Sets status flag to free.
 */
static int palmas_rtc_remove(struct platform_device *pdev)
{
	struct palmas_rtc *palmas_rtc = platform_get_drvdata(pdev);
	struct rtc_device *rtc = palmas_rtc->rtc;

	palmas_rtc_alarm_irq_enable(&pdev->dev, 0);

	free_irq(palmas_rtc->irq, palmas_rtc);
	rtc_device_unregister(rtc);
	kfree(palmas_rtc);

	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-rtc", },
	{ /* end */ },
};

static struct platform_driver palmas_rtc_driver = {
	.probe = palmas_rtc_probe,
	.remove = palmas_rtc_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "palmas-rtc",
		.of_match_table = of_palmas_match_tbl,
	},
};

static int __init palmas_rtc_init(void)
{
	return platform_driver_register(&palmas_rtc_driver);
}
module_init(palmas_rtc_init);

static void __exit palmas_rtc_exit(void)
{
	platform_driver_unregister(&palmas_rtc_driver);
}
module_exit(palmas_rtc_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas RTC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palmas-rtc");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
