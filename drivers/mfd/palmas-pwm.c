/*
 * Driver for Watchdog part of Palmas PMIC Chips
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 *
 * Based on twl6030_pwm.c
 *
 * Author: Hemanth V <hemanthv@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/palmas.h>
#include <linux/slab.h>

struct pwm_device {
	const char *label;
	unsigned int pwm_id;
};

struct palmas *the_palmas;

#define PWM_DUTY_MAX	255

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct palmas *palmas = the_palmas;
	unsigned int addr, reg;
	int ret, slave;

	if (pwm == NULL || period_ns == 0 || duty_ns > period_ns)
		return -EINVAL;

	switch (period_ns) {
	case 8000000:
		reg = 0;
		break;
	case 16000000:
		reg = PALMAS_PWM_CTRL1_PWM_FREQ_SEL;
		break;
	default:
		return -EINVAL;
	}

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_LED_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_LED_BASE, PALMAS_PWM_CTRL1);

	ret = regmap_update_bits(palmas->regmap[slave], addr,
			PALMAS_PWM_CTRL1_PWM_FREQ_SEL, reg);
	if (ret)
		goto out;

	/* Calculate the duty cycle value */
	reg = (duty_ns * PWM_DUTY_MAX) / period_ns;

	addr = PALMAS_BASE_TO_REG(PALMAS_LED_BASE, PALMAS_PWM_CTRL2);

	ret = regmap_write(palmas->regmap[slave], addr, reg);
	if (ret)
		goto out;

	return 0;
out:
	pr_err("%s: Failed to configure PWM, Error %d\n",
			pwm->label, ret);
	return ret;

}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	struct palmas *palmas = the_palmas;
	unsigned int addr;
	int ret, slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_LED_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_LED_BASE, PALMAS_PWM_CTRL1);

	ret = regmap_update_bits(palmas->regmap[slave], addr,
			PALMAS_PWM_CTRL1_PWM_FREQ_EN,
			PALMAS_PWM_CTRL1_PWM_FREQ_EN);
	if (ret < 0) {
		pr_err("%s: Failed to enable PWM, Error %d\n", pwm->label, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	struct palmas *palmas = the_palmas;
	unsigned int addr;
	int ret, slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_LED_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_LED_BASE, PALMAS_PWM_CTRL1);

	ret = regmap_update_bits(palmas->regmap[slave], addr,
			PALMAS_PWM_CTRL1_PWM_FREQ_EN, 0);
	if (ret < 0)
		pr_err("%s: Failed to enable PWM, Error %d\n", pwm->label, ret);

	return;
}
EXPORT_SYMBOL(pwm_disable);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;

	/* We are not ready to write to Palmas device yet */
	if (!the_palmas)
		return NULL;

	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		pr_err("%s: failed to allocate memory\n", label);
		return NULL;
	}

	pwm->label = label;
	pwm->pwm_id = pwm_id;

	return pwm;
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	pwm_disable(pwm);
	kfree(pwm);
}
EXPORT_SYMBOL(pwm_free);

static int palmas_pwm_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);

	/* Only one PWM access can be allowed by PWM subsystem */
	if (the_palmas)
		return -EINVAL;

	/* no need to allow pwm_request if PWM is not muxed */
	if (palmas->led_muxed) {
		the_palmas = palmas;
	} else {
		dev_err(&pdev->dev, "there are no PWMs muxed\n");
		return -EINVAL;
	}

	return 0;
}

static int palmas_pwm_remove(struct platform_device *pdev)
{
	the_palmas = NULL;

	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-pwm", },
	{ /* end */ }
};

static struct platform_driver palmas_pwm_driver = {
	.probe = palmas_pwm_probe,
	.remove = palmas_pwm_remove,
	.driver = {
		.name = "palmas-pwm",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
};

static int __init palmas_pwm_init(void)
{
	return platform_driver_register(&palmas_pwm_driver);
}
module_init(palmas_pwm_init);

static void __exit palmas_pwm_exit(void)
{
	platform_driver_unregister(&palmas_pwm_driver);
}
module_exit(palmas_pwm_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas PWM driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palmas-pwm");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
