/*
 * Retu MFD driver
 *
 * Copyright (C) 2004, 2005 Nokia Corporation
 *
 * Based on code written by Juha Yrjölä, David Weinehall and Mikko Ylinen.
 * Rewritten to MFD/I2C driver by Aaro Koskinen.
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

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/retu.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>

/* Registers */
#define RETU_REG_ASICR		0x00		/* ASIC ID and revision */
#define RETU_REG_ASICR_VILMA	(1 << 7)	/* Bit indicating Vilma */
#define RETU_REG_IDR		0x01		/* Interrupt ID */
#define RETU_REG_IMR		0x02		/* Interrupt mask */

/* Interrupt sources */
#define RETU_INT_PWR		0		/* Power button */

#define RETU_MAX_IRQ_HANDLERS	16

struct retu_dev {
	struct device		*dev;
	struct i2c_client	*i2c;
	struct mutex		mutex;

	struct irq_chip		irq_chip;
	int			irq_base;
	int			irq_end;
	int			irq_mask;
	bool			irq_mask_pending;
};

static struct resource retu_pwrbutton_res[] = {
	{
		.name	= "retu-pwrbutton",
		.start	= RETU_INT_PWR,
		.end	= RETU_INT_PWR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell retu_devs[] = {
	{
		.name		= "retu-wdt"
	},
	{
		.name		= "retu-pwrbutton",
		.resources	= &retu_pwrbutton_res[0],
		.num_resources	= ARRAY_SIZE(retu_pwrbutton_res),
	}
};

/* Retu device registered for the power off. */
static struct retu_dev *retu_pm_power_off;

int retu_read(struct retu_dev *rdev, u8 reg)
{
	int ret;

	mutex_lock(&rdev->mutex);
	ret = i2c_smbus_read_word_data(rdev->i2c, reg);
	mutex_unlock(&rdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(retu_read);

int retu_write(struct retu_dev *rdev, u8 reg, u16 data)
{
	int ret;

	mutex_lock(&rdev->mutex);
	ret = i2c_smbus_write_word_data(rdev->i2c, reg, data);
	mutex_unlock(&rdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(retu_write);

static void retu_power_off(void)
{
	struct retu_dev *rdev = retu_pm_power_off;
	int reg;

	mutex_lock(&retu_pm_power_off->mutex);

	/* Ignore power button state */
	reg = i2c_smbus_read_word_data(rdev->i2c, RETU_REG_CC1);
	i2c_smbus_write_word_data(rdev->i2c, RETU_REG_CC1, reg | 2);

	/* Expire watchdog immediately */
	i2c_smbus_write_word_data(rdev->i2c, RETU_REG_WATCHDOG, 0);

	/* Wait for poweroff */
	for (;;)
		cpu_relax();

	mutex_unlock(&retu_pm_power_off->mutex);
}

static irqreturn_t retu_irq_handler(int irq, void *retu)
{
	struct retu_dev *rdev = retu;
	int idr;
	int imr;

	mutex_lock(&rdev->mutex);

	idr = i2c_smbus_read_word_data(rdev->i2c, RETU_REG_IDR);
	if (idr < 0)
		goto i2c_error;

	imr = i2c_smbus_read_word_data(rdev->i2c, RETU_REG_IMR);
	if (imr < 0)
		goto i2c_error;

	idr &= ~imr;
	i2c_smbus_write_word_data(rdev->i2c, RETU_REG_IDR, idr);

	mutex_unlock(&rdev->mutex);

	if (!idr) {
		dev_vdbg(rdev->dev, "No IRQ, spurious?\n");
		return IRQ_NONE;
	}

	while (idr) {
		unsigned long pending = __ffs(idr);
		unsigned int irq;

		idr &= ~BIT(pending);
		irq = pending + rdev->irq_base;
		handle_nested_irq(irq);
	}

	return IRQ_HANDLED;

i2c_error:
	mutex_unlock(&rdev->mutex);
	return IRQ_NONE;
}

static void retu_irq_mask(struct irq_data *data)
{
	struct retu_dev *rdev = irq_data_get_irq_chip_data(data);
	int irq = data->irq;

	rdev->irq_mask |= (1 << (irq - rdev->irq_base));
	rdev->irq_mask_pending = true;
}

static void retu_irq_unmask(struct irq_data *data)
{
	struct retu_dev *rdev = irq_data_get_irq_chip_data(data);
	int irq = data->irq;

	rdev->irq_mask &= ~(1 << (irq - rdev->irq_base));
	rdev->irq_mask_pending = true;
}

static void retu_bus_lock(struct irq_data *data)
{
	struct retu_dev *rdev = irq_data_get_irq_chip_data(data);

	mutex_lock(&rdev->mutex);
}

static void retu_bus_sync_unlock(struct irq_data *data)
{
	struct retu_dev *rdev = irq_data_get_irq_chip_data(data);

	if (rdev->irq_mask_pending) {
		i2c_smbus_write_word_data(rdev->i2c, RETU_REG_IMR,
					  rdev->irq_mask);
		rdev->irq_mask_pending = false;
	}
	mutex_unlock(&rdev->mutex);
}

static void retu_irq_init(struct retu_dev *rdev)
{
	int base = rdev->irq_base;
	int end = rdev->irq_end;
	int irq;

	rdev->irq_chip.name		   = "RETU";
	rdev->irq_chip.irq_bus_lock	   = retu_bus_lock;
	rdev->irq_chip.irq_bus_sync_unlock = retu_bus_sync_unlock;
	rdev->irq_chip.irq_mask		   = retu_irq_mask;
	rdev->irq_chip.irq_unmask	   = retu_irq_unmask;

	for (irq = base; irq < end; irq++) {
		irq_set_chip_data(irq, rdev);
		irq_set_chip(irq, &rdev->irq_chip);
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	/* Mask all RETU interrupts */
	rdev->irq_mask = 0xffff;
	i2c_smbus_write_word_data(rdev->i2c, RETU_REG_IMR, rdev->irq_mask);
}

static void retu_irq_exit(struct retu_dev *rdev)
{
	int base = rdev->irq_base;
	int end = rdev->irq_end;
	int irq;

	for (irq = base; irq < end; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip_and_handler(irq, NULL, NULL);
		irq_set_chip_data(irq, NULL);
	}
}

static int __devinit retu_probe(struct i2c_client *i2c,
				const struct i2c_device_id *id)
{
	struct retu_dev *rdev;
	int ret;

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (rdev == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rdev);
	rdev->dev = &i2c->dev;
	rdev->i2c = i2c;
	mutex_init(&rdev->mutex);

	ret = retu_read(rdev, RETU_REG_ASICR);
	if (ret < 0) {
		dev_err(rdev->dev, "could not read Retu revision: %d\n", ret);
		goto error;
	}

	dev_info(rdev->dev, "Retu%s v%d.%d found\n",
		 (ret & RETU_REG_ASICR_VILMA) ? " & Vilma" : "",
		 (ret >> 4) & 0x7, ret & 0xf);

	ret = irq_alloc_descs(-1, 0, RETU_MAX_IRQ_HANDLERS, 0);
	if (ret < 0) {
		dev_err(rdev->dev, "failed to allocate IRQ descs: %d\n", ret);
		goto error;
	}
	rdev->irq_base = ret;
	rdev->irq_end  = ret + RETU_MAX_IRQ_HANDLERS;

	retu_irq_init(rdev);

	ret = request_threaded_irq(rdev->i2c->irq, NULL, retu_irq_handler,
				   IRQF_ONESHOT, "retu-mfd", rdev);
	if (ret < 0) {
		dev_err(rdev->dev, "unable to register IRQ handler: %d\n", ret);
		goto error_irq;
	}
	irq_set_irq_wake(rdev->i2c->irq, 1);

	ret = mfd_add_devices(rdev->dev, -1, retu_devs, ARRAY_SIZE(retu_devs),
			      NULL, rdev->irq_base, NULL);
	if (ret < 0)
		goto error_add;

	if (!pm_power_off) {
		pm_power_off	  = retu_power_off;
		retu_pm_power_off = rdev;
	}

	return 0;

error_add:
	free_irq(rdev->i2c->irq, rdev);
error_irq:
	irq_free_descs(rdev->irq_base, RETU_MAX_IRQ_HANDLERS);
error:
	kfree(rdev);
	return ret;
}

static int __devexit retu_remove(struct i2c_client *i2c)
{
	struct retu_dev *rdev = i2c_get_clientdata(i2c);

	if (retu_pm_power_off == rdev) {
		pm_power_off	  = NULL;
		retu_pm_power_off = NULL;
	}
	free_irq(rdev->i2c->irq, rdev);
	retu_irq_exit(rdev);
	irq_free_descs(rdev->irq_base, RETU_MAX_IRQ_HANDLERS);
	mfd_remove_devices(rdev->dev);
	kfree(rdev);

	return 0;
}

static const struct i2c_device_id retu_id[] = {
	{ "retu-mfd", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, retu_id);

static struct i2c_driver retu_driver = {
	.driver		= {
		.name = "retu-mfd",
		.owner = THIS_MODULE,
	},
	.probe		= retu_probe,
	.remove		= retu_remove,
	.id_table	= retu_id,
};
module_i2c_driver(retu_driver);

MODULE_DESCRIPTION("Retu MFD driver");
MODULE_AUTHOR("Juha Yrjölä");
MODULE_AUTHOR("David Weinehall");
MODULE_AUTHOR("Mikko Ylinen");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
MODULE_LICENSE("GPL");
