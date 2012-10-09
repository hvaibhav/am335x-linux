/*
 * Retu power button driver.
 *
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Original code written by Ari Saastamoinen, Juha Yrjölä and Felipe Balbi.
 * Converted to use to use Retu MFD driver by Aaro Koskinen.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/irq.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/retu.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#define RETU_STATUS_PWRONX (1 << 5)

struct retu_pwrbutton {
	struct input_dev	*idev;
	struct retu_dev		*rdev;
	struct device		*dev;
	bool			pressed;
	int			irq;
};

static irqreturn_t retu_pwrbutton_irq(int irq, void *_pwr)
{
	struct retu_pwrbutton *pwr = _pwr;
	bool state;

	state = !(retu_read(pwr->rdev, RETU_REG_STATUS) & RETU_STATUS_PWRONX);

	if (pwr->pressed != state) {
		input_report_key(pwr->idev, KEY_POWER, state);
		input_sync(pwr->idev);
		pwr->pressed = state;
	}

	return IRQ_HANDLED;
}

static int __devinit retu_pwrbutton_probe(struct platform_device *pdev)
{
	struct retu_dev *rdev = dev_get_drvdata(pdev->dev.parent);
	struct retu_pwrbutton *pwr;
	int ret;

	pwr = kzalloc(sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	pwr->rdev = rdev;
	pwr->dev  = &pdev->dev;
	pwr->irq  = platform_get_irq(pdev, 0);
	platform_set_drvdata(pdev, pwr);

	ret = request_threaded_irq(pwr->irq, NULL, retu_pwrbutton_irq, 0,
				   "retu-pwrbutton", pwr);
	if (ret < 0)
		goto error_irq;

	pwr->idev = input_allocate_device();
	if (!pwr->idev) {
		ret = -ENOMEM;
		goto error_input;
	}

	pwr->idev->evbit[0]			= BIT_MASK(EV_KEY);
	pwr->idev->keybit[BIT_WORD(KEY_POWER)]	= BIT_MASK(KEY_POWER);
	pwr->idev->name				= "retu-pwrbutton";

	ret = input_register_device(pwr->idev);
	if (ret < 0)
		goto error_reg;

	return 0;

error_reg:
	input_free_device(pwr->idev);
error_input:
	free_irq(pwr->irq, pwr);
error_irq:
	kfree(pwr);
	return ret;
}

static int __devexit retu_pwrbutton_remove(struct platform_device *pdev)
{
	struct retu_pwrbutton *pwr = platform_get_drvdata(pdev);

	free_irq(pwr->irq, pwr);
	input_unregister_device(pwr->idev);
	input_free_device(pwr->idev);
	kfree(pwr);

	return 0;
}

static struct platform_driver retu_pwrbutton_driver = {
	.probe		= retu_pwrbutton_probe,
	.remove		= __devexit_p(retu_pwrbutton_remove),
	.driver		= {
		.name	= "retu-pwrbutton",
	},
};
module_platform_driver(retu_pwrbutton_driver);

MODULE_ALIAS("platform:retu-pwrbutton");
MODULE_DESCRIPTION("Retu Power Button");
MODULE_AUTHOR("Ari Saastamoinen");
MODULE_AUTHOR("Felipe Balbi");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
MODULE_LICENSE("GPL");
