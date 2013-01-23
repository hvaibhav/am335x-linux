/*
 * PALMAS GPADC module driver
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 * Graeme Gregory <gg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/mfd/palmas.h>
#include <linux/hwmon-sysfs.h>
#include <linux/of.h>
#include <linux/of_platform.h>

struct palmas_ideal_code {
	s16 code1;
	s16 code2;
	s16 v1;
	s16 v2;
};

static struct palmas_ideal_code palmas_ideal[PALMAS_MAX_CHANNELS] = {
	{ /* Channel 0 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 630,
		.v2 = 950,
	},
	{ /* Channel 1 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 630,
		.v2 = 950,
	},
	{ /* Channel 2 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 1260,
		.v2 = 1900,
	},
	{ /* Channel 3 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 630,
		.v2 = 950,
	},
	{ /* Channel 4 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 630,
		.v2 = 950,
	},
	{ /* Channel 5 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 630,
		.v2 = 950,
	},
	{ /* Channel 6 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 2520,
		.v2 = 3800,
	},
	{ /* Channel 7 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 2520,
		.v2 = 3800,
	},
	{ /* Channel 8 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 3150,
		.v2 = 4750,
	},
	{ /* Channel 9 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 5670,
		.v2 = 8550,
	},
	{ /* Channel 10 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 3465,
		.v2 = 5225,
	},
	{ /* Channel 11 */
	},
	{ /* Channel 12 */
	},
	{ /* Channel 13 */
	},
	{ /* Channel 14 */
		.code1 = 2064,
		.code2 = 3112,
		.v1 = 3645,
		.v2 = 5225,
	},
	{ /* Channel 15 */
	},
};

static int palmas_gpadc_read(struct palmas *palmas, unsigned int reg,
		unsigned int *dest)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_GPADC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_GPADC_BASE, reg);

	return regmap_read(palmas->regmap[slave], addr, dest);
}

static int palmas_gpadc_write(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_GPADC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_GPADC_BASE, reg);

	return regmap_write(palmas->regmap[slave], addr, data);
}

static int palmas_gpadc_trim_read_block(struct palmas *palmas, unsigned int reg,
		u8 *dest, size_t count)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_TRIM_GPADC_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_TRIM_GPADC_BASE, reg);

	return regmap_bulk_read(palmas->regmap[slave], addr, dest, count);
}

static irqreturn_t palmas_gpadc_irq_handler(int irq, void *_gpadc)
{
	struct palmas_gpadc *gpadc = _gpadc;

	complete(&gpadc->irq_complete);

	return IRQ_HANDLED;
}

static void palmas_gpadc_calculate_values(struct palmas_gpadc *gpadc, int channel,
				s32 raw_code, struct palmas_gpadc_result *res)
{
	int channel_value = 0;
	s32 corrected_code;

	res->raw_code = raw_code;

	/* Do TRIM corrections */
	/* No correction for channels 11-13,15 */
	if (((channel >= 11) && (channel <= 13)) || (channel == 15)) {
		corrected_code = raw_code;
		channel_value = raw_code;
	} else {
		corrected_code = ((raw_code * 1000) -
		gpadc->palmas_cal_tbl[channel].offset_error) /
			gpadc->palmas_cal_tbl[channel].gain_error;

		channel_value = corrected_code *
				gpadc->palmas_cal_tbl[channel].gain;

		/* Shift back into mV range */
		channel_value /= 1000;
	}

	dev_dbg(gpadc->dev, "GPADC raw: %d", raw_code);
	dev_dbg(gpadc->dev, "GPADC cor: %d", corrected_code);
	dev_dbg(gpadc->dev, "GPADC val: %d", channel_value);

	res->corrected_code = corrected_code;
	res->result = channel_value;
}

static s32 palmas_gpadc_calculate_reverse(struct palmas_gpadc *gpadc,
		int channel, int value)
{
	s32 corrected_code, raw_code;

	/* Do reverse TRIM corrections */
	/* No correction for channels 11-13 */
	if ((channel >= 11) && (channel <= 13)) {
		raw_code =  value;
		corrected_code = value;
	} else {
		value *= 1000;
		corrected_code = value / gpadc->palmas_cal_tbl[channel].gain;
		raw_code = ((corrected_code *
				gpadc->palmas_cal_tbl[channel].gain_error) +
				gpadc->palmas_cal_tbl[channel].offset_error) /
				1000;
	}

	dev_dbg(gpadc->dev, "GPADC raw: %d", raw_code);
	dev_dbg(gpadc->dev, "GPADC cor: %d", corrected_code);
	dev_dbg(gpadc->dev, "GPADC val: %d", value);

	return raw_code;
}

int palmas_gpadc_conversion(struct palmas_gpadc *gpadc, int channel,
				struct palmas_gpadc_result *res)
{
	unsigned int reg;
	int ret = 0;
	s32 raw_code;

	if (unlikely(!gpadc)) {
		printk(KERN_ERR "palmas-gpadc: no GPADC exists yet!\n");
		return -EINVAL;
	}

	if (unlikely(!res))
		return -EINVAL;

	mutex_lock(&gpadc->reading_lock);

	/* Setup Conversion */
	reg = PALMAS_GPADC_SW_SELECT_SW_CONV_EN |
			PALMAS_GPADC_SW_SELECT_SW_START_CONV0 |
			(channel & PALMAS_GPADC_SW_SELECT_SW_CONV0_SEL_MASK);
	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_SW_SELECT, reg);
	if (ret)
		return ret;

	/* Wait for IRQ to finish*/
	wait_for_completion_interruptible_timeout(&gpadc->irq_complete,
						msecs_to_jiffies(5000));

	/* Read the results */
	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_SW_CONV0_LSB,
				&reg);
	if (ret)
		return ret;

	raw_code = reg;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_SW_CONV0_MSB,
				&reg);
	if (ret)
		return ret;

	raw_code += reg << 8;

	palmas_gpadc_calculate_values(gpadc, channel, raw_code, res);

	mutex_unlock(&gpadc->reading_lock);

	return 0;
}
EXPORT_SYMBOL(palmas_gpadc_conversion);

static ssize_t show_channel(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct palmas_gpadc *gpadc = dev_get_drvdata(dev);
	int ret;
	struct palmas_gpadc_result result;

	ret = palmas_gpadc_conversion(gpadc, attr->index, &result);

	return sprintf(buf, "%d\n", result.result);
}

#define in_channel(index) \
static SENSOR_DEVICE_ATTR(in##index##_channel, S_IRUGO, show_channel, \
	NULL, index)

in_channel(0);
in_channel(1);
in_channel(2);
in_channel(3);
in_channel(4);
in_channel(5);
in_channel(6);
in_channel(7);
in_channel(8);
in_channel(9);
in_channel(10);
in_channel(11);
in_channel(12);
in_channel(13);
in_channel(14);

#define IN_ATTRS_CHANNEL(X)\
	(&sensor_dev_attr_in##X##_channel.dev_attr.attr)

static struct attribute *palmas_gpadc_attributes[] = {
	IN_ATTRS_CHANNEL(0),
	IN_ATTRS_CHANNEL(1),
	IN_ATTRS_CHANNEL(2),
	IN_ATTRS_CHANNEL(3),
	IN_ATTRS_CHANNEL(4),
	IN_ATTRS_CHANNEL(5),
	IN_ATTRS_CHANNEL(6),
	IN_ATTRS_CHANNEL(7),
	IN_ATTRS_CHANNEL(8),
	IN_ATTRS_CHANNEL(9),
	IN_ATTRS_CHANNEL(10),
	IN_ATTRS_CHANNEL(11),
	IN_ATTRS_CHANNEL(12),
	IN_ATTRS_CHANNEL(13),
	IN_ATTRS_CHANNEL(14),
	NULL
};

static const struct attribute_group palmas_gpadc_group = {
	.attrs = palmas_gpadc_attributes,
};

/* @brief Current Source selection for GPADC Channel 0 input
 *
 * The current is selected from the following values.
 * 0: 0uA
 * 1: 5uA
 * 2: 15uA
 * 3: 20uA
 *
 * @param gpadc pointer to the gpadc device data
 * @param cursel value from table to select current
 * @return error
 */
int palmas_gpadc_current_src_ch0(struct palmas_gpadc *gpadc, int cursel)
{
	unsigned int reg;
	int ret = 0;

	palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_CTRL1, &reg);
	if (ret)
		return ret;

	reg &= ~PALMAS_GPADC_CTRL1_CURRENT_SRC_CH0_MASK;
	cursel <<= PALMAS_GPADC_CTRL1_CURRENT_SRC_CH0_SHIFT;
	cursel &= PALMAS_GPADC_CTRL1_CURRENT_SRC_CH0_MASK;
	reg |= cursel;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_CTRL1, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_current_src_ch0);

/* @brief Current Source selection for GPADC Channel 3 input
 *
 * The current is selected from the following values.
 * 0: 0uA
 * 1: 10uA
 * 2: 400uA
 * 3: 800uA
 *
 * @param gpadc pointer to the gpadc device data
 * @param cursel value from table to select current
 * @return error
 */
int palmas_gpadc_current_src_ch3(struct palmas_gpadc *gpadc, int cursel)
{
	unsigned int reg;
	int ret = 0;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_CTRL1, &reg);
	if (ret)
		return ret;

	reg &= ~PALMAS_GPADC_CTRL1_CURRENT_SRC_CH3_MASK;
	cursel <<= PALMAS_GPADC_CTRL1_CURRENT_SRC_CH3_SHIFT;
	cursel &= PALMAS_GPADC_CTRL1_CURRENT_SRC_CH3_MASK;
	reg |= cursel;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_CTRL1, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_current_src_ch3);

/* @brief GPADC Force control
 *
 * Enabling FORCE will lower the latency for GPADC conversions but will draw
 * more power as GPADC unit is always on.
 *
 * 0 : GPADC power is controlled by conversion
 * 1 : GPADC is always on.
 *
 * @param gpadc pointer to the gpadc device data
 * @param force boolean for enable/disable
 * @return error
 */
int palmas_gpadc_force(struct palmas_gpadc *gpadc, int force)
{
	unsigned int reg;
	int ret = 0;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_CTRL1, &reg);
	if (ret)
		return ret;

	if (force)
		reg |= PALMAS_GPADC_CTRL1_GPADC_FORCE;
	else
		reg &= ~PALMAS_GPADC_CTRL1_GPADC_FORCE;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_CTRL1, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_force);

/* @brief conversion timeslot selection
 *
 * This selects the time between auto conversions in GPADC in binary
 * the value are given as follows
 *
 * 0000: 1/32s
 * 0001: 1/16s
 * 0010: 1/8s
 * 1110: 512s
 * 1111: 1024s
 *
 * @param gpadc pointer to the gpadc device data
 * @param timeslot value from the table
 * @return error
 */
int palmas_gpadc_counter_conv(struct palmas_gpadc *gpadc, int timeslot)
{
	unsigned int reg;
	int ret = 0;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CTRL, &reg);
	if (ret)
		return ret;

	reg &= ~PALMAS_GPADC_AUTO_CTRL_COUNTER_CONV_MASK;
	timeslot <<= PALMAS_GPADC_AUTO_CTRL_COUNTER_CONV_SHIFT;
	timeslot &= PALMAS_GPADC_AUTO_CTRL_COUNTER_CONV_MASK;
	reg |= timeslot;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_AUTO_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_counter_conv);

/* @brief set the channels for auto conversion
 *
 * This sets the two channels for the auto conversion in the GPADC
 *
 * @param gpadc pointer to the gpadc device data
 * @param ch0 the channel for AUTO_CONV0
 * @return error
 */
int palmas_gpadc_auto_conv0_channel(struct palmas_gpadc *gpadc, int ch0)
{
	unsigned int reg;
	int ret;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_SELECT, &reg);
	if (ret)
		return ret;

	reg |= ch0 & PALMAS_GPADC_AUTO_SELECT_AUTO_CONV0_SEL_MASK;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_AUTO_SELECT, reg);

	gpadc->conv0_channel = ch0;

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv0_channel);

/* @brief set the channels for auto conversion
 *
 * This sets the two channels for the auto conversion in the GPADC
 *
 * @param gpadc pointer to the gpadc device data
 * @param ch1 the channel for AUTO_CONV1
 * @return error
 */
int palmas_gpadc_auto_conv1_channel(struct palmas_gpadc *gpadc, int ch1)
{
	unsigned int reg;
	int ret;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_SELECT, &reg);
	if (ret)
		return ret;

	reg &= PALMAS_GPADC_AUTO_SELECT_AUTO_CONV1_SEL_MASK;
	reg |= ch1 << PALMAS_GPADC_AUTO_SELECT_AUTO_CONV1_SEL_SHIFT;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_AUTO_SELECT, reg);

	gpadc->conv1_channel = ch1;

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv1_channel);

/* @brief set the threshold for auto conversion
 *
 * This sets the two channels for the auto conversion in the GPADC
 *
 * @param gpadc pointer to the gpadc device data
 * @param th0 the channel for AUTO_CONV0
 * @param lt set if IRQ is generated for less than, otherwise greater than
 * @return error
 */
int palmas_gpadc_auto_conv0_threshold(struct palmas_gpadc *gpadc, int th0,
		int lt)
{
	unsigned int reg;
	int ret;
	s32 raw_code;

	raw_code = palmas_gpadc_calculate_reverse(gpadc, gpadc->conv0_channel,
			th0);

	reg = raw_code & PALMAS_GPADC_THRES_CONV0_LSB_THRES_CONV0_LSB_MASK;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_THRES_CONV0_LSB,
			reg);
	if (ret)
		return ret;

	reg = (raw_code >> 8) &
			PALMAS_GPADC_THRES_CONV0_MSB_THRES_CONV0_MSB_MASK;
	reg |= lt << PALMAS_GPADC_THRES_CONV0_MSB_THRES_CONV0_POL_SHIFT;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_THRES_CONV0_LSB,
			reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv0_threshold);

/* @brief set the threshold for auto conversion
 *
 * This sets the two channels for the auto conversion in the GPADC
 *
 * @param gpadc pointer to the gpadc device data
 * @param th1 the channel for AUTO_CONV0
 * @param lt set if IRQ is generated for less than, otherwise greater than
 * @return error
 */
int palmas_gpadc_auto_conv1_threshold(struct palmas_gpadc *gpadc, int th1,
		int lt)
{
	unsigned int reg;
	int ret;
	s32 raw_code;

	raw_code = palmas_gpadc_calculate_reverse(gpadc, gpadc->conv1_channel,
			th1);

	reg = raw_code & PALMAS_GPADC_THRES_CONV1_LSB_THRES_CONV1_LSB_MASK;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_THRES_CONV1_LSB,
			reg);
	if (ret)
		return ret;

	reg = (raw_code >> 8) &
			PALMAS_GPADC_THRES_CONV1_MSB_THRES_CONV1_MSB_MASK;
	reg |= lt << PALMAS_GPADC_THRES_CONV1_MSB_THRES_CONV1_POL_SHIFT;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_THRES_CONV0_LSB,
			reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv1_threshold);

/* @brief enable the auto conv0
 *
 * This function starts or stops the conv0 auto conversion.
 *
 * @param gpadc pointer to the gpadc device data
 * @param enable boolean to set enable state
 * @return error
 */
int palmas_gpadc_auto_conv0_enable(struct palmas_gpadc *gpadc, int enable)
{
	unsigned int reg;
	int ret;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CTRL, &reg);
	if (ret)
		return ret;

	if (enable)
		reg |= PALMAS_GPADC_AUTO_CTRL_AUTO_CONV0_EN;
	else
		reg &= ~PALMAS_GPADC_AUTO_CTRL_AUTO_CONV0_EN;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_AUTO_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv0_enable);

/* @brief enable the auto conv1
 *
 * This function starts or stops the conv1 auto conversion.
 *
 * @param gpadc pointer to the gpadc device data
 * @param enable boolean to set enable state
 * @return error
 */
int palmas_gpadc_auto_conv1_enable(struct palmas_gpadc *gpadc, int enable)
{
	unsigned int reg;
	int ret;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CTRL, &reg);
	if (ret)
		return ret;

	if (enable)
		reg |= PALMAS_GPADC_AUTO_CTRL_AUTO_CONV1_EN;
	else
		reg &= ~PALMAS_GPADC_AUTO_CTRL_AUTO_CONV1_EN;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_AUTO_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv1_enable);

/* @brief return the result of auto conv0 conversion
 *
 * @param gpadc pointer to the gpadc device data
 * @param res pointer to the result structure
 * @return error or result
 */
int palmas_gpadc_auto_conv0_result(struct palmas_gpadc *gpadc,
		struct palmas_gpadc_result *res)
{
	unsigned int reg;
	int ret;
	s32 raw_code;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CONV0_LSB,
			&reg);
	if (ret)
		return ret;

	raw_code = reg;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CONV0_LSB,
			&reg);
	if (ret)
		return ret;

	raw_code += (reg &
			PALMAS_GPADC_AUTO_CONV0_MSB_AUTO_CONV0_MSB_MASK) << 8;

	palmas_gpadc_calculate_values(gpadc, gpadc->conv0_channel,
			raw_code, res);

	return 0;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv0_result);

/* @brief return the result of auto conv0 conversion
 *
 * @param gpadc pointer to the gpadc device data
 * @param res pointer to the result structure
 * @return error or result
 */
int palmas_gpadc_auto_conv1_result(struct palmas_gpadc *gpadc,
		struct palmas_gpadc_result *res)
{
	unsigned int reg;
	int ret;
	s32 raw_code;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CONV1_LSB,
			&reg);
	if (ret)
		return ret;

	raw_code = reg;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_AUTO_CONV1_LSB,
			&reg);
	if (ret)
		return ret;

	raw_code += (reg &
			PALMAS_GPADC_AUTO_CONV1_MSB_AUTO_CONV1_MSB_MASK) << 8;

	palmas_gpadc_calculate_values(gpadc, gpadc->conv1_channel,
			raw_code, res);

	return 0;
}
EXPORT_SYMBOL(palmas_gpadc_auto_conv1_result);

/* @brief select channel for realtime conversion
 *
 * This function selects the channel for the realtime conversion.
 *
 * @param gpadc pointer to the gpadc device data
 * @param ch select channel for realtime conversion
 * @return error
 */
int palmas_gpadc_rt_channel(struct palmas_gpadc *gpadc, int ch)
{
	unsigned int reg;
	int ret;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_RT_SELECT, &reg);
	if (ret)
		return ret;

	reg &= ~PALMAS_GPADC_RT_SELECT_RT_CONV0_SEL_MASK;
	reg |= (ch & PALMAS_GPADC_RT_SELECT_RT_CONV0_SEL_MASK);

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_RT_SELECT, reg);

	gpadc->rt_channel = ch;

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_rt_channel);

/* @brief enable the realtime conversion
 *
 * This function starts or stops the realtime conversion.
 *
 * @param gpadc pointer to the gpadc device data
 * @param enable boolean to set enable state
 * @return error
 */
int palmas_gpadc_rt_enable(struct palmas_gpadc *gpadc, int enable)
{
	unsigned int reg;
	int ret;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_RT_SELECT, &reg);
	if (ret)
		return ret;

	if (enable)
		reg |= PALMAS_GPADC_RT_SELECT_RT_CONV_EN;
	else
		reg &= ~PALMAS_GPADC_RT_SELECT_RT_CONV_EN;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_RT_SELECT, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_gpadc_rt_enable);

/* @brief return the result of realtime conversion
 *
 * @param gpadc pointer to the gpadc device data
 * @param res pointer to the result structure
 * @return error or result
 */
int palmas_gpadc_rt_result(struct palmas_gpadc *gpadc,
		struct palmas_gpadc_result *res)
{
	unsigned int reg;
	int ret;
	s32 raw_code;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_RT_CONV0_LSB, &reg);
	if (ret)
		return ret;

	raw_code = reg;

	ret = palmas_gpadc_read(gpadc->palmas, PALMAS_GPADC_RT_CONV0_MSB, &reg);
	if (ret)
		return ret;

	raw_code += (reg & PALMAS_GPADC_RT_CONV0_MSB_RT_CONV0_MSB_MASK) << 8;

	palmas_gpadc_calculate_values(gpadc, gpadc->conv1_channel,
			raw_code, NULL);

	return 0;
}
EXPORT_SYMBOL(palmas_gpadc_rt_result);


/* @brief configure the current monitoring for GPADC channel 11
 *
 * @param gpadc pointer to the gpadc device data.
 * @param enable enable/desable the feature.
 * @param rext enable/disable the external resistor feature.
 * @param channel select the SMPS to be monitored.
 * @return error or resul
 */
int palmas_gpadc_configure_ilmonitor(struct palmas_gpadc *gpadc,
		int enable, int rext, int channel)
{
	unsigned int reg = 0;
	int ret;

	if (channel > 6 || channel < 0)
		return -EINVAL;

	if (enable)
		reg |= PALMAS_GPADC_SMPS_ILMONITOR_EN_SMPS_ILMON_EN;

	if (rext)
		reg |= PALMAS_GPADC_SMPS_ILMONITOR_EN_SMPS_ILMON_REXT;

	reg |= channel;

	ret = palmas_gpadc_write(gpadc->palmas, PALMAS_GPADC_SMPS_ILMONITOR_EN,
			reg);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(palmas_gpadc_configure_ilmonitor);

static struct miscdevice palmas_gpadc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "palmas-gpadc",
};

static int palmas_gpadc_calibrate(struct palmas_gpadc *gpadc)
{
	int chn, d1 = 0, d2 = 0, b, k, gain, x1, x2, v1, v2;
	u8 trim_regs[17];
	int ret;

	ret = palmas_gpadc_trim_read_block(gpadc->palmas,
			PALMAS_GPADC_TRIM1, trim_regs + 1,
			PALMAS_MAX_CHANNELS);
	if (ret < 0) {
		dev_err(gpadc->dev, "Error reading trim registers\n");
		return ret;
	}

	/*
	 * Loop to calculate the value needed for returning voltages from
	 * GPADC not values.
	 *
	 * gain is calculated to 3 decimal places fixed point.
	 */
	for (chn = 0; chn < PALMAS_MAX_CHANNELS; chn++) {
		switch (chn) {
		case 0:
		case 1:
		case 3:
		case 4:
		case 5:
			/* D1 */
			d1 = trim_regs[1];

			/* D2 */
			d2 = trim_regs[2];
			break;
		case 2:
			/* D1 */
			d1 = trim_regs[3];

			/* D2 */
			d2 = trim_regs[4];
			break;
		case 6:
			/* D1 */
			d1 = trim_regs[5];

			/* D2 */
			d2 = trim_regs[6];
			break;
		case 7:
			/* D1 */
			d1 = trim_regs[7];

			/* D2 */
			d2 = trim_regs[8];
			break;
		case 8:
			/* D1 */
			d1 = trim_regs[9];

			/* D2 */
			d2 = trim_regs[10];
			break;
		case 9:
			/* D1 */
			d1 = trim_regs[11];

			/* D2 */
			d2 = trim_regs[12];
			break;
		case 10:
			/* D1 */
			d1 = trim_regs[13];

			/* D2 */
			d2 = trim_regs[14];
			break;
		case 14:
			/* D1 */
			d1 = trim_regs[15];

			/* D2 */
			d2 = trim_regs[16];
			break;
		default:
			/* there is no calibration for other channels */
			continue;
		}

		/* if bit 8 is set then number is negative */
		if (d1 & 0x80)
			d1 = 0 - (d1 & 0x7F);
		if (d2 & 0x80)
			d2 = 0 - (d2 & 0x7F);

		dev_dbg(gpadc->dev, "GPADC d1   for Chn: %d = %d", chn, d1);
		dev_dbg(gpadc->dev, "GPADC d2   for Chn: %d = %d", chn, d2);

		x1 = palmas_ideal[chn].code1;
		x2 = palmas_ideal[chn].code2;

		dev_dbg(gpadc->dev, "GPADC x1   for Chn: %d = %d", chn, x1);
		dev_dbg(gpadc->dev, "GPADC x2   for Chn: %d = %d", chn, x2);

		v1 = palmas_ideal[chn].v1;
		v2 = palmas_ideal[chn].v2;

		dev_dbg(gpadc->dev, "GPADC v1   for Chn: %d = %d", chn, v1);
		dev_dbg(gpadc->dev, "GPADC v2   for Chn: %d = %d", chn, v2);

		/* Gain */
		gain = ((v2 - v1) * 1000) / (x2 - x1);

		/* k */
		k = 1000 + (((d2 - d1) * 1000) / (x2 - x1));

		/* b */
		b = (d1 * 1000) - (k - 1000) * x1;

		gpadc->palmas_cal_tbl[chn].gain = gain;
		gpadc->palmas_cal_tbl[chn].gain_error = k;
		gpadc->palmas_cal_tbl[chn].offset_error = b;

		dev_dbg(gpadc->dev, "GPADC Gain for Chn: %d = %d", chn, gain);
		dev_dbg(gpadc->dev, "GPADC k    for Chn: %d = %d", chn, k);
		dev_dbg(gpadc->dev, "GPADC b    for Chn: %d = %d", chn, b);

	}
	return 0;
}

static void palmas_dt_to_pdata(struct device_node *node,
		struct palmas_gpadc_platform_data *pdata)
{
	u32 prop;
	int ret;

	ret = of_property_read_u32(node, "ti,ch3_current", &prop);
	if (!ret) {
		pdata->ch3_current = prop;
	}

	ret = of_property_read_u32(node, "ti,ch0_current", &prop);
	if (!ret) {
		pdata->ch0_current = prop;
	}

	ret = of_property_read_u32(node, "ti,bat_removal", &prop);
	if (!ret) {
		pdata->bat_removal = prop;
	}

	ret = of_property_read_u32(node, "ti,start_polarity", &prop);
	if (!ret) {
		pdata->start_polarity = prop;
	}
}

static int palmas_gpadc_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_gpadc_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *node = pdev->dev.of_node;
	struct palmas_gpadc *gpadc;
	int ret, irq;

	if(node && !pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);

		if (!pdata)
			return -ENOMEM;

		palmas_dt_to_pdata(node, pdata);
	}

	if (!pdata)
		return -EINVAL;

	gpadc = kzalloc(sizeof(struct palmas_gpadc), GFP_KERNEL);
	if (!gpadc)
		return -ENOMEM;

	gpadc->palmas_cal_tbl = kzalloc(
				sizeof(struct palmas_gpadc_calibration) *
				PALMAS_MAX_CHANNELS,
				GFP_KERNEL);
	if (!gpadc->palmas_cal_tbl) {
		kfree(gpadc);
		return -ENOMEM;
	}

	gpadc->dev = &pdev->dev;
	gpadc->palmas = palmas;
	palmas->gpadc = gpadc;

	ret = misc_register(&palmas_gpadc_device);
	if (ret) {
		dev_dbg(&pdev->dev, "could not register misc_device\n");
		goto err_misc;
	}

	irq = regmap_irq_get_virq(palmas->irq_data, PALMAS_GPADC_EOC_SW_IRQ);
	ret = request_threaded_irq(irq, NULL, palmas_gpadc_irq_handler,
		0, "palmas_gpadc", gpadc);
	if (ret) {
		dev_dbg(&pdev->dev, "could not request irq\n");
		goto err_irq;
	}

	gpadc->eoc_sw_irq = irq;

	platform_set_drvdata(pdev, gpadc);

	mutex_init(&gpadc->reading_lock);
	init_completion(&gpadc->irq_complete);

	/* Initialise CTRL1 from pdata */
	if (pdata->ch3_current) {
		ret = palmas_gpadc_current_src_ch3(gpadc, pdata->ch3_current);
		if (ret) {
			dev_err(gpadc->dev, "CH3 Current Setting Error\n");
			goto err_ctrl;
		}
	}
	if (pdata->ch0_current) {
		ret = palmas_gpadc_current_src_ch0(gpadc, pdata->ch0_current);
		if (ret) {
			dev_err(gpadc->dev, "CH0 Current Setting Error\n");
			goto err_ctrl;
		}
	}

	palmas_gpadc_calibrate(gpadc);

	ret = sysfs_create_group(&pdev->dev.kobj, &palmas_gpadc_group);
	if (ret)
		dev_err(&pdev->dev, "could not create sysfs files\n");

	return 0;

err_ctrl:
	free_irq(irq, gpadc);
err_irq:
	misc_deregister(&palmas_gpadc_device);
err_misc:
	kfree(gpadc);

	return ret;
}

static int palmas_gpadc_remove(struct platform_device *pdev)
{
	struct palmas_gpadc *gpadc = platform_get_drvdata(pdev);

	disable_irq(gpadc->eoc_sw_irq);
	free_irq(gpadc->eoc_sw_irq, gpadc);
	sysfs_remove_group(&pdev->dev.kobj, &palmas_gpadc_group);
	kfree(gpadc->palmas_cal_tbl);
	kfree(gpadc);
	misc_deregister(&palmas_gpadc_device);

	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-gpadc", },
	{ /* end */ }
};

static struct platform_driver palmas_gpadc_driver = {
	.driver = {
		.name = "palmas-gpadc",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
	.probe = palmas_gpadc_probe,
	.remove = palmas_gpadc_remove,
};

static int __init palmas_gpadc_init(void)
{
	return platform_driver_register(&palmas_gpadc_driver);
}
module_init(palmas_gpadc_init);

static void __exit palmas_gpadc_exit(void)
{
	platform_driver_unregister(&palmas_gpadc_driver);
}
module_exit(palmas_gpadc_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas General Purpose ADC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palmas-gpadc");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
