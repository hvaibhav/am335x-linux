/*
 * CBUS I2C driver for Nokia Internet Tablets.
 *
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Based on code written by Juha Yrjölä, David Weinehall, Mikko Ylinen and
 * Felipe Balbi. Converted to I2C driver by Aaro Koskinen.
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

#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/i2c-cbus.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

struct cbus_host {
	/* host lock */
	spinlock_t	lock;

	struct device	*dev;

	int		clk_gpio;
	int		dat_gpio;
	int		sel_gpio;
};

/**
 * cbus_send_bit - sends one bit over the bus
 * @host: the host we're using
 * @bit: one bit of information to send
 * @input: whether to set data pin as input after sending
 */
static int cbus_send_bit(struct cbus_host *host, unsigned bit,
		unsigned input)
{
	int ret = 0;

	gpio_set_value(host->dat_gpio, bit ? 1 : 0);
	gpio_set_value(host->clk_gpio, 1);

	/* The data bit is read on the rising edge of CLK */
	if (input)
		ret = gpio_direction_input(host->dat_gpio);

	gpio_set_value(host->clk_gpio, 0);

	return ret;
}

/**
 * cbus_send_data - sends @len amount of data over the bus
 * @host: the host we're using
 * @data: the data to send
 * @len: size of the transfer
 * @input: whether to set data pin as input after sending
 */
static int cbus_send_data(struct cbus_host *host, unsigned data, unsigned len,
		unsigned input)
{
	int ret = 0;
	int i;

	for (i = len; i > 0; i--) {
		ret = cbus_send_bit(host, data & (1 << (i - 1)),
				input && (i == 1));
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

/**
 * cbus_receive_bit - receives one bit from the bus
 * @host: the host we're using
 */
static int cbus_receive_bit(struct cbus_host *host)
{
	int ret;

	gpio_set_value(host->clk_gpio, 1);
	ret = gpio_get_value(host->dat_gpio);
	if (ret < 0)
		goto out;
	gpio_set_value(host->clk_gpio, 0);

out:
	return ret;
}

/**
 * cbus_receive_word - receives 16-bit word from the bus
 * @host: the host we're using
 */
static int cbus_receive_word(struct cbus_host *host)
{
	int ret = 0;
	int i;

	for (i = 16; i > 0; i--) {
		int bit = cbus_receive_bit(host);

		if (bit < 0)
			goto out;

		if (bit)
			ret |= 1 << (i - 1);
	}

out:
	return ret;
}

/**
 * cbus_transfer - transfers data over the bus
 * @host: the host we're using
 * @rw: read/write flag
 * @dev: device address
 * @reg: register address
 * @data: if @rw == I2C_SBUS_WRITE data to send otherwise 0
 */
static int cbus_transfer(struct cbus_host *host, char rw, unsigned dev,
			 unsigned reg, unsigned data)
{
	unsigned long flags;
	int ret;

	/* We don't want interrupts disturbing our transfer */
	spin_lock_irqsave(&host->lock, flags);

	/* Reset state and start of transfer, SEL stays down during transfer */
	gpio_set_value(host->sel_gpio, 0);

	/* Set the DAT pin to output */
	gpio_direction_output(host->dat_gpio, 1);

	/* Send the device address */
	ret = cbus_send_data(host, dev, 3, 0);
	if (ret < 0) {
		dev_dbg(host->dev, "failed sending device addr\n");
		goto out;
	}

	/* Send the rw flag */
	ret = cbus_send_bit(host, rw == I2C_SMBUS_READ, 0);
	if (ret < 0) {
		dev_dbg(host->dev, "failed sending read/write flag\n");
		goto out;
	}

	/* Send the device address */
	ret = cbus_send_data(host, reg, 5, rw == I2C_SMBUS_READ);
	if (ret < 0) {
		dev_dbg(host->dev, "failed sending register addr\n");
		goto out;
	}

	if (rw == I2C_SMBUS_WRITE) {
		ret = cbus_send_data(host, data, 16, 0);
		if (ret < 0) {
			dev_dbg(host->dev, "failed sending data\n");
			goto out;
		}
	} else {
		gpio_set_value(host->clk_gpio, 1);

		ret = cbus_receive_word(host);
		if (ret < 0) {
			dev_dbg(host->dev, "failed receiving data\n");
			goto out;
		}
	}

	/* Indicate end of transfer, SEL goes up until next transfer */
	gpio_set_value(host->sel_gpio, 1);
	gpio_set_value(host->clk_gpio, 1);
	gpio_set_value(host->clk_gpio, 0);

out:
	spin_unlock_irqrestore(&host->lock, flags);

	return ret;
}

static int cbus_i2c_smbus_xfer(struct i2c_adapter	*adapter,
			       u16			addr,
			       unsigned short		flags,
			       char			read_write,
			       u8			command,
			       int			size,
			       union i2c_smbus_data	*data)
{
	struct cbus_host *chost = i2c_get_adapdata(adapter);
	int ret;

	if (size != I2C_SMBUS_WORD_DATA)
		return -EINVAL;

	ret = cbus_transfer(chost, read_write == I2C_SMBUS_READ, addr,
			    command, data->word);
	if (ret < 0)
		return ret;

	if (read_write == I2C_SMBUS_READ)
		data->word = ret;

	return 0;
}

static u32 cbus_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_WRITE_WORD_DATA;
}

static const struct i2c_algorithm cbus_i2c_algo = {
	.smbus_xfer	= cbus_i2c_smbus_xfer,
	.functionality	= cbus_i2c_func,
};

static int cbus_i2c_remove(struct platform_device *pdev)
{
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);
	struct cbus_host *chost = i2c_get_adapdata(adapter);
	int ret;

	ret = i2c_del_adapter(adapter);
	if (ret)
		return ret;
	gpio_free(chost->clk_gpio);
	gpio_free(chost->dat_gpio);
	gpio_free(chost->sel_gpio);
	kfree(chost);
	kfree(adapter);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int cbus_i2c_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adapter;
	struct cbus_host *chost;
	struct gpio gpios[3];
	int ret;

	adapter = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	if (!adapter) {
		ret = -ENOMEM;
		goto error;
	}

	chost = kzalloc(sizeof(*chost), GFP_KERNEL);
	if (!chost) {
		ret = -ENOMEM;
		goto error_mem;
	}

	if (pdev->dev.of_node) {
		struct device_node *dnode = pdev->dev.of_node;
		if (of_gpio_count(dnode) != 3) {
			ret = -ENODEV;
			goto error_mem;
		}
		chost->clk_gpio = of_get_gpio(dnode, 0);
		chost->dat_gpio = of_get_gpio(dnode, 1);
		chost->sel_gpio = of_get_gpio(dnode, 2);
	} else if (pdev->dev.platform_data) {
		struct i2c_cbus_platform_data *pdata = pdev->dev.platform_data;
		chost->clk_gpio = pdata->clk_gpio;
		chost->dat_gpio = pdata->dat_gpio;
		chost->sel_gpio = pdata->sel_gpio;
	} else {
		ret = -ENODEV;
		goto error_mem;
	}

	adapter->owner		= THIS_MODULE;
	adapter->class		= I2C_CLASS_HWMON;
	adapter->dev.parent	= &pdev->dev;
	adapter->nr		= pdev->id;
	adapter->timeout	= HZ;
	adapter->algo		= &cbus_i2c_algo;
	strlcpy(adapter->name, "CBUS I2C adapter", sizeof(adapter->name));

	spin_lock_init(&chost->lock);
	chost->dev = &pdev->dev;

	gpios[0].gpio  = chost->clk_gpio;
	gpios[0].flags = GPIOF_OUT_INIT_LOW;
	gpios[0].label = "CBUS clk";

	gpios[1].gpio  = chost->dat_gpio;
	gpios[1].flags = GPIOF_IN;
	gpios[1].label = "CBUS data";

	gpios[2].gpio  = chost->sel_gpio;
	gpios[2].flags = GPIOF_OUT_INIT_HIGH;
	gpios[2].label = "CBUS sel";

	ret = gpio_request_array(gpios, ARRAY_SIZE(gpios));
	if (ret)
		goto error_gpio;

	gpio_set_value(chost->clk_gpio, 1);
	gpio_set_value(chost->clk_gpio, 0);

	i2c_set_adapdata(adapter, chost);
	platform_set_drvdata(pdev, adapter);

	ret = i2c_add_numbered_adapter(adapter);
	if (ret)
		goto error_i2c;

	return 0;

error_i2c:
	gpio_free_array(gpios, ARRAY_SIZE(gpios));
error_gpio:
	kfree(chost);
error_mem:
	kfree(adapter);
error:
	return ret;
}

#if defined(CONFIG_OF)
static const struct of_device_id i2c_cbus_dt_ids[] = {
	{ .compatible = "i2c-cbus", },
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_cbus_dt_ids);
#endif

static struct platform_driver cbus_i2c_driver = {
	.probe	= cbus_i2c_probe,
	.remove	= cbus_i2c_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "i2c-cbus",
	},
};
module_platform_driver(cbus_i2c_driver);

MODULE_ALIAS("platform:i2c-cbus");
MODULE_DESCRIPTION("CBUS I2C driver");
MODULE_AUTHOR("Juha Yrjölä");
MODULE_AUTHOR("David Weinehall");
MODULE_AUTHOR("Mikko Ylinen");
MODULE_AUTHOR("Felipe Balbi");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
MODULE_LICENSE("GPL");
