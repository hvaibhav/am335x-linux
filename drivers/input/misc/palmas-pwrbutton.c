/**
 * Palmas Power Button Input Driver
 *
 * Copyright (C) 2012 Texas Instrument Inc
 *
 * Written by Girish S Ghongdemath <girishsg@ti.com>
 * Based on twl4030-pwrbutton.c driver.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/palmas.h>
#include <linux/of_platform.h>

struct palmas_pwron {
	struct palmas *palmas;
	struct input_dev *input_dev;
};

static irqreturn_t pwron_irq(int irq, void *palmas_pwron)
{
	struct palmas_pwron *pwron = palmas_pwron;
	struct input_dev *input_dev = pwron->input_dev;

	input_report_key(input_dev, KEY_POWER, 1);
	input_report_key(input_dev, KEY_POWER, 0);
	input_sync(input_dev);

	return IRQ_HANDLED;
}

static int palmas_pwron_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct input_dev *input_dev;
	struct palmas_pwron *pwron;
	int irq;
	unsigned int addr;
	int err, slave;


	pwron = kzalloc(sizeof(*pwron), GFP_KERNEL);
	if (!pwron) {
		dev_err(&pdev->dev, "PWRON memory allocation failed\n");
		return -ENOMEM;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
			dev_err(&pdev->dev,
				"input_dev memory allocation failed\n");
			err = -ENOMEM;
			goto free_pwron;
	}

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	input_dev->keybit[BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER);
	input_dev->name = "palmas_pwron";
	input_dev->phys = "palmas_pwron/input0";
	input_dev->dev.parent = &pdev->dev;

	pwron->palmas = palmas;
	pwron->input_dev = input_dev;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_PMU_CONTROL_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_PMU_CONTROL_BASE,
			PALMAS_PMU_CONTROL_BASE);

	/* 6 Seconds as the LPK_TIME Long Press Key Time */
	regmap_update_bits(palmas->regmap[slave], addr,
			PALMAS_LONG_PRESS_KEY_LPK_TIME_MASK, 0);

	irq = regmap_irq_get_virq(palmas->irq_data, PALMAS_PWRON_IRQ);
	err = request_threaded_irq(irq, NULL, pwron_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"palmas_pwron", pwron);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't get IRQ for pwron: %d\n", err);
		goto free_input_dev;
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_dbg(&pdev->dev, "Can't register power button: %d\n", err);
		goto free_irq;
	}

	platform_set_drvdata(pdev, input_dev);

	return 0;

free_irq:
	free_irq(irq, input_dev);
free_input_dev:
	input_free_device(input_dev);
free_pwron:
	kfree(pwron);
	return err;
}

static int palmas_pwron_remove(struct platform_device *pdev)
{
	struct input_dev *input_dev = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	free_irq(irq, input_dev);
	input_unregister_device(input_dev);

	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-pwrbutton", },
	{ /* end */ }
};

static struct platform_driver palmas_pwron_driver = {
	.remove = palmas_pwron_remove,
	.driver = {
		.name = "palmas-pwrbutton",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
};

static int __init palmas_pwron_init(void)
{
	return platform_driver_probe(&palmas_pwron_driver,
			palmas_pwron_probe);
}
module_init(palmas_pwron_init);

static void __exit palmas_pwron_exit(void)
{
	platform_driver_unregister(&palmas_pwron_driver);
}
module_exit(palmas_pwron_exit);

MODULE_ALIAS("platform:palmas-pwrbutton");
MODULE_DESCRIPTION("Palmas Power Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Girish S Ghongdemath <girishsg@ti.com>");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
