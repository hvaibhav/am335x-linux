/*
 * Driver for LED part of Palmas PMIC Chips
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/mfd/palmas.h>
#include <linux/module.h>

struct palmas_leds_data;

struct palmas_led {
	struct led_classdev cdev;
	struct palmas_leds_data *leds_data;
	int led_no;
	struct work_struct work;
	struct mutex mutex;

	spinlock_t value_lock;

	int blink;
	int on_time;
	int period;
	enum led_brightness brightness;
};

struct palmas_leds_data {
	struct device *dev;
	struct led_classdev cdev;
	struct palmas *palmas;

	struct palmas_led *palmas_led;
	int no_leds;
};

#define to_palmas_led(led_cdev) \
	container_of(led_cdev, struct palmas_led, cdev)

#define LED_ON_62_5MS		0x00
#define LED_ON_125MS		0x01
#define LED_ON_250MS		0x02
#define LED_ON_500MS		0x03

#define LED_PERIOD_OFF		0x00
#define LED_PERIOD_125MS	0x01
#define LED_PERIOD_250MS	0x02
#define LED_PERIOD_500MS	0x03
#define LED_PERIOD_1000MS	0x04
#define LED_PERIOD_2000MS	0x05
#define LED_PERIOD_4000MS	0x06
#define LED_PERIOD_8000MS	0x07


static char *palmas_led_names[] = {
	"palmas:led1",
	"palmas:led2",
};

int palmas_led_read(struct palmas_leds_data *leds, unsigned int reg,
		unsigned int *dest)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_LED_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_LED_BASE, reg);

	return regmap_read(leds->palmas->regmap[slave], addr, dest);
}

int palmas_led_write(struct palmas_leds_data *leds, unsigned int reg,
		unsigned int value)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_LED_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_LED_BASE, reg);

	return regmap_write(leds->palmas->regmap[slave], addr, value);
}

static void palmas_leds_work(struct work_struct *work)
{
	struct palmas_led *led = container_of(work, struct palmas_led, work);
	unsigned int period, ctrl;
	unsigned long flags;

	mutex_lock(&led->mutex);

	palmas_led_read(led->leds_data, PALMAS_LED_CTRL, &ctrl);
	palmas_led_read(led->leds_data, PALMAS_LED_PERIOD_CTRL, &period);

	spin_lock_irqsave(&led->value_lock, flags);

	switch (led->led_no) {
	case 1:
		ctrl &= ~PALMAS_LED_CTRL_LED_1_ON_TIME_MASK;
		period &= ~PALMAS_LED_PERIOD_CTRL_LED_1_PERIOD_MASK;

		if (led->blink) {
			ctrl |= led->on_time <<
				PALMAS_LED_CTRL_LED_1_ON_TIME_SHIFT;
			period |= led->period <<
				PALMAS_LED_PERIOD_CTRL_LED_1_PERIOD_SHIFT;
		} else {
			/*
			 * for off value is zero which we already set by
			 * masking earlier.
			 */
			if (led->brightness) {
				ctrl |= LED_ON_500MS <<
					PALMAS_LED_CTRL_LED_1_ON_TIME_SHIFT;
				period |= LED_PERIOD_500MS <<
					PALMAS_LED_PERIOD_CTRL_LED_1_PERIOD_SHIFT;
			}
		}
	case 2:
		ctrl &= ~PALMAS_LED_CTRL_LED_2_ON_TIME_MASK;
		period &= ~PALMAS_LED_PERIOD_CTRL_LED_2_PERIOD_MASK;

		if (led->blink) {
			ctrl |= led->on_time <<
				PALMAS_LED_CTRL_LED_2_ON_TIME_SHIFT;
			period |= led->period <<
				PALMAS_LED_PERIOD_CTRL_LED_2_PERIOD_SHIFT;
		} else {
			/*
			 * for off value is zero which we already set by
			 * masking earlier.
			 */
			if (led->brightness) {
				ctrl |= LED_ON_500MS <<
					PALMAS_LED_CTRL_LED_2_ON_TIME_SHIFT;
				period |= LED_PERIOD_500MS <<
					PALMAS_LED_PERIOD_CTRL_LED_2_PERIOD_SHIFT;
			}
		}
	}
	spin_unlock_irqrestore(&led->value_lock, flags);

	palmas_led_write(led->leds_data, PALMAS_LED_CTRL, ctrl);
	palmas_led_write(led->leds_data, PALMAS_LED_PERIOD_CTRL, period);

	mutex_unlock(&led->mutex);
}

static void palmas_leds_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct palmas_led *led = to_palmas_led(led_cdev);
	unsigned long flags;

	spin_lock_irqsave(&led->value_lock, flags);
	led->brightness = value;
	led->blink = 0;
	schedule_work(&led->work);
	spin_unlock_irqrestore(&led->value_lock, flags);
}

static int palmas_leds_blink_set(struct led_classdev *led_cdev,
				   unsigned long *delay_on,
				   unsigned long *delay_off)
{
	struct palmas_led *led = to_palmas_led(led_cdev);
	unsigned long flags;
	int ret = 0;
	int period;

	/* Pick some defaults if we've not been given times */
	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 250;
		*delay_off = 250;
	}

	spin_lock_irqsave(&led->value_lock, flags);

	/* We only have a limited selection of settings, see if we can
	 * support the configuration we're being given */
	switch (*delay_on) {
	case 500:
		led->on_time = LED_ON_500MS;
		break;
	case 250:
		led->on_time = LED_ON_250MS;
		break;
	case 125:
		led->on_time = LED_ON_125MS;
		break;
	case 62:
	case 63:
		/* Actually 62.5ms */
		led->on_time = LED_ON_62_5MS;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	period = *delay_on + *delay_off;

	if (ret == 0) {
		switch (period) {
		case 124:
		case 125:
		case 126:
			/* All possible variations of 62.5 + 62.5 */
			led->period = LED_PERIOD_125MS;
			break;
		case 250:
			led->period = LED_PERIOD_250MS;
			break;
		case 500:
			led->period = LED_PERIOD_500MS;
			break;
		case 1000:
			led->period = LED_PERIOD_1000MS;
			break;
		case 2000:
			led->period = LED_PERIOD_2000MS;
			break;
		case 4000:
			led->period = LED_PERIOD_4000MS;
			break;
		case 8000:
			led->period = LED_PERIOD_8000MS;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	if (ret == 0)
		led->blink = 1;
	else
		led->blink = 0;

	/* Always update; if we fail turn off blinking since we expect
	 * a software fallback. */
	schedule_work(&led->work);

	spin_unlock_irqrestore(&led->value_lock, flags);

	return ret;
}

static void palmas_init_led(struct palmas_leds_data *leds_data, int offset,
		int led_no)
{
	mutex_init(&leds_data->palmas_led[offset].mutex);
	INIT_WORK(&leds_data->palmas_led[offset].work, palmas_leds_work);
	spin_lock_init(&leds_data->palmas_led[offset].value_lock);

	leds_data->palmas_led[offset].leds_data = leds_data;
	leds_data->palmas_led[offset].led_no = led_no;
	leds_data->palmas_led[offset].cdev.name = palmas_led_names[led_no - 1];
	leds_data->palmas_led[offset].cdev.default_trigger = NULL;
	leds_data->palmas_led[offset].cdev.brightness_set = palmas_leds_set;
	leds_data->palmas_led[offset].cdev.blink_set = palmas_leds_blink_set;
}

static int palmas_leds_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_leds_data *leds_data;
	int ret, i;
	int offset = 0;

	if (!palmas->led_muxed) {
		dev_err(&pdev->dev, "there are no LEDs muxed\n");
		return -EINVAL;
	}

	leds_data = kzalloc(sizeof(*leds_data), GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;
	platform_set_drvdata(pdev, leds_data);

	leds_data->palmas = palmas;

	if (palmas->led_muxed == (PALMAS_LED1_MUXED | PALMAS_LED2_MUXED))
		leds_data->no_leds = 2;
	else
		leds_data->no_leds = 1;

	leds_data->palmas_led = kcalloc(leds_data->no_leds,
			sizeof(struct palmas_led), GFP_KERNEL);

	/* Initialise LED1 */
	if (palmas->led_muxed & PALMAS_LED1_MUXED) {
		palmas_init_led(leds_data, offset, 1);
		offset = 1;
	}

	/* Initialise LED2 */
	if (palmas->led_muxed & PALMAS_LED2_MUXED)
		palmas_init_led(leds_data, offset, 2);

	for (i = 0; i < leds_data->no_leds; i++) {
		ret = led_classdev_register(leds_data->dev,
				&leds_data->palmas_led[i].cdev);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"Failed to register LED no: %d err: %d\n",
				i, ret);
			goto err_led;
		}
	}

	return 0;

err_led:
	for (i = 0; i < leds_data->no_leds; i++)
		led_classdev_unregister(&leds_data->palmas_led[i].cdev);
	kfree(leds_data->palmas_led);
	kfree(leds_data);
	return ret;
}

static int palmas_leds_remove(struct platform_device *pdev)
{
	struct palmas_leds_data *leds_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < leds_data->no_leds; i++)
		led_classdev_unregister(&leds_data->palmas_led[i].cdev);
	if (i)
		kfree(leds_data->palmas_led);
	kfree(leds_data);

	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-leds", },
	{ /* end */ }
};

static struct platform_driver palmas_leds_driver = {
	.driver = {
		.name = "palmas-leds",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
	.probe = palmas_leds_probe,
	.remove = palmas_leds_remove,
};

static int __init palmas_leds_init(void)
{
	return platform_driver_register(&palmas_leds_driver);
}
module_init(palmas_leds_init);

static void __exit palmas_leds_exit(void)
{
	platform_driver_unregister(&palmas_leds_driver);
}
module_exit(palmas_leds_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palmas-leds");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
