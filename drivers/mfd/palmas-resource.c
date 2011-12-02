/*
 * PALMAS resource module driver
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
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/mfd/palmas.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static int palmas_resource_read(struct palmas *palmas, unsigned int reg,
		unsigned int *dest)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RESOURCE_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE, reg);

	return regmap_read(palmas->regmap[slave], addr, dest);
}

static int palmas_resource_write(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RESOURCE_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE, reg);

	return regmap_write(palmas->regmap[slave], addr, data);
}

int palmas_enable_regen1(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_REGEN1_CTRL, &reg);

	if (ret)
		return ret;

	reg |= PALMAS_REGEN1_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_REGEN1_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_enable_regen1);

int palmas_disable_regen1(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_REGEN1_CTRL, &reg);

	if (ret)
		return ret;

	reg &= ~PALMAS_REGEN1_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_REGEN1_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_disable_regen1);

int palmas_is_enabled_regen1(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_REGEN1_CTRL, &reg);


	return !!(reg & PALMAS_REGEN1_CTRL_STATUS);
}
EXPORT_SYMBOL(palmas_is_enabled_regen1);

int palmas_enable_regen2(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_REGEN2_CTRL, &reg);

	if (ret)
		return ret;

	reg |= PALMAS_REGEN2_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_REGEN2_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_enable_regen2);

int palmas_disable_regen2(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_REGEN2_CTRL, &reg);

	if (ret)
		return ret;

	reg &= ~PALMAS_REGEN2_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_REGEN2_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_disable_regen2);

int palmas_is_enabled_regen2(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_REGEN2_CTRL, &reg);


	return !!(reg & PALMAS_REGEN2_CTRL_STATUS);
}
EXPORT_SYMBOL(palmas_is_enabled_regen2);

int palmas_enable_sysen1(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_SYSEN1_CTRL, &reg);

	if (ret)
		return ret;

	reg |= PALMAS_SYSEN1_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_SYSEN1_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_enable_sysen1);

int palmas_disable_sysen1(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_SYSEN1_CTRL, &reg);

	if (ret)
		return ret;

	reg &= ~PALMAS_SYSEN1_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_SYSEN1_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_disable_sysen1);

int palmas_is_enabled_sysen1(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_SYSEN1_CTRL, &reg);


	return !!(reg & PALMAS_SYSEN1_CTRL_STATUS);
}
EXPORT_SYMBOL(palmas_is_enabled_sysen1);

int palmas_enable_sysen2(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_SYSEN2_CTRL, &reg);

	if (ret)
		return ret;

	reg |= PALMAS_SYSEN2_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_SYSEN2_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_enable_sysen2);

int palmas_disable_sysen2(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_SYSEN2_CTRL, &reg);

	if (ret)
		return ret;

	reg &= ~PALMAS_SYSEN2_CTRL_MODE_ACTIVE;

	ret = palmas_resource_write(resource->palmas,
			PALMAS_SYSEN2_CTRL, reg);

	return ret;
}
EXPORT_SYMBOL(palmas_disable_sysen2);

int palmas_is_enabled_sysen2(struct palmas_resource *resource)
{
	int ret;
	unsigned int reg;

	ret = palmas_resource_read(resource->palmas,
			PALMAS_SYSEN2_CTRL, &reg);


	return !!(reg & PALMAS_SYSEN2_CTRL_STATUS);
}
EXPORT_SYMBOL(palmas_is_enabled_sysen2);

static int palmas_initialise_resource(struct palmas_resource *resource,
		struct palmas_resource_platform_data *pdata)
{
	int ret;
	unsigned int reg;

	if (pdata->regen1_mode_sleep) {
		ret = palmas_resource_read(resource->palmas,
				PALMAS_REGEN1_CTRL, &reg);
		if (ret)
			return ret;

		reg |= pdata->regen1_mode_sleep <<
				PALMAS_REGEN1_CTRL_MODE_SLEEP_SHIFT;

		ret = palmas_resource_write(resource->palmas,
				PALMAS_REGEN1_CTRL, reg);
		if (ret)
			return ret;
	}

	if (pdata->regen2_mode_sleep) {
		ret = palmas_resource_read(resource->palmas,
				PALMAS_REGEN2_CTRL, &reg);
		if (ret)
			return ret;

		reg |= pdata->regen2_mode_sleep <<
				PALMAS_REGEN2_CTRL_MODE_SLEEP_SHIFT;

		ret = palmas_resource_write(resource->palmas,
				PALMAS_REGEN2_CTRL, reg);
		if (ret)
			return ret;
	}

	if (pdata->sysen1_mode_sleep) {
		ret = palmas_resource_read(resource->palmas,
				PALMAS_SYSEN1_CTRL, &reg);
		if (ret)
			return ret;

		reg |= pdata->sysen1_mode_sleep <<
				PALMAS_SYSEN1_CTRL_MODE_SLEEP_SHIFT;

		ret = palmas_resource_write(resource->palmas,
				PALMAS_SYSEN1_CTRL, reg);
		if (ret)
			return ret;
	}

	if (pdata->sysen2_mode_sleep) {
		ret = palmas_resource_read(resource->palmas,
				PALMAS_SYSEN2_CTRL, &reg);
		if (ret)
			return ret;

		reg |= pdata->sysen2_mode_sleep <<
				PALMAS_SYSEN2_CTRL_MODE_SLEEP_SHIFT;

		ret = palmas_resource_write(resource->palmas,
				PALMAS_SYSEN2_CTRL, reg);
		if (ret)
			return ret;
	}

	if (pdata->nsleep_res) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_NSLEEP_RES_ASSIGN, pdata->nsleep_res);
		if (ret)
			return ret;
	}

	if (pdata->nsleep_smps) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_NSLEEP_SMPS_ASSIGN, pdata->nsleep_smps);
		if (ret)
			return ret;
	}

	if (pdata->nsleep_ldo1) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_NSLEEP_LDO_ASSIGN1, pdata->nsleep_ldo1);
		if (ret)
			return ret;
	}

	if (pdata->nsleep_ldo2) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_NSLEEP_LDO_ASSIGN2, pdata->nsleep_ldo2);
		if (ret)
			return ret;
	}

	if (pdata->enable1_res) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE1_RES_ASSIGN, pdata->enable1_res);
		if (ret)
			return ret;
	}

	if (pdata->enable1_smps) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE1_SMPS_ASSIGN, pdata->enable1_smps);
		if (ret)
			return ret;
	}

	if (pdata->enable1_ldo1) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE1_LDO_ASSIGN1, pdata->enable1_ldo1);
		if (ret)
			return ret;
	}

	if (pdata->enable1_ldo2) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE1_LDO_ASSIGN2, pdata->enable1_ldo2);
		if (ret)
			return ret;
	}

	if (pdata->enable2_res) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE2_RES_ASSIGN, pdata->enable2_res);
		if (ret)
			return ret;
	}

	if (pdata->enable2_smps) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE2_SMPS_ASSIGN, pdata->enable2_smps);
		if (ret)
			return ret;
	}

	if (pdata->enable2_ldo1) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE2_LDO_ASSIGN1, pdata->enable2_ldo1);
		if (ret)
			return ret;
	}

	if (pdata->enable2_ldo2) {
		ret = palmas_resource_write(resource->palmas,
			PALMAS_ENABLE2_LDO_ASSIGN2, pdata->enable2_ldo2);
		if (ret)
			return ret;
	}

	return ret;
}

static struct miscdevice palmas_resource_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "palmas-resource",
};

static void palmas_dt_to_pdata(struct device_node *node,
		struct palmas_resource_platform_data *pdata)
{
	int ret;
	u32 prop;

	ret = of_property_read_u32(node, "ti,regen1_mode_sleep", &prop);
	if (!ret) {
		pdata->regen1_mode_sleep = prop;
	}

	ret = of_property_read_u32(node, "ti,regen2_mode_sleep", &prop);
	if (!ret) {
		pdata->regen2_mode_sleep = prop;
	}

	ret = of_property_read_u32(node, "ti,sysen1_mode_sleep", &prop);
	if (!ret) {
		pdata->regen1_mode_sleep = prop;
	}

	ret = of_property_read_u32(node, "ti,sysen2_mode_sleep", &prop);
	if (!ret) {
		pdata->regen2_mode_sleep = prop;
	}

	ret = of_property_read_u32(node, "ti,nsleep_res", &prop);
	if (!ret) {
		pdata->nsleep_res = prop;
	}

	ret = of_property_read_u32(node, "ti,nsleep_smps", &prop);
	if (!ret) {
		pdata->nsleep_smps = prop;
	}

	ret = of_property_read_u32(node, "ti,nsleep_ldo1", &prop);
	if (!ret) {
		pdata->nsleep_ldo1 = prop;
	}

	ret = of_property_read_u32(node, "ti,nsleep_ldo2", &prop);
	if (!ret) {
		pdata->nsleep_ldo2 = prop;
	}

	ret = of_property_read_u32(node, "ti,enable1_res", &prop);
	if (!ret) {
		pdata->enable1_res = prop;
	}

	ret = of_property_read_u32(node, "ti,enable1_smps", &prop);
	if (!ret) {
		pdata->enable1_smps = prop;
	}

	ret = of_property_read_u32(node, "ti,enable1_ldo1", &prop);
	if (!ret) {
		pdata->enable1_ldo1 = prop;
	}

	ret = of_property_read_u32(node, "ti,enable1_ldo2", &prop);
	if (!ret) {
		pdata->enable1_ldo2 = prop;
	}

	ret = of_property_read_u32(node, "ti,enable2_res", &prop);
	if (!ret) {
		pdata->enable2_res = prop;
	}

	ret = of_property_read_u32(node, "ti,enable2_smps", &prop);
	if (!ret) {
		pdata->enable2_smps = prop;
	}

	ret = of_property_read_u32(node, "ti,enable2_ldo1", &prop);
	if (!ret) {
		pdata->enable2_ldo1 = prop;
	}

	ret = of_property_read_u32(node, "ti,enable2_ldo2", &prop);
	if (!ret) {
		pdata->enable2_ldo2 = prop;
	}
}

static int palmas_resource_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_resource_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *node = pdev->dev.of_node;
	struct palmas_resource *resource;
	int ret;

	if(node && !pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);

		if (!pdata)
			return -ENOMEM;

		palmas_dt_to_pdata(node, pdata);
	}

	if (!pdata)
		return -EINVAL;

	resource = kzalloc(sizeof(struct palmas_resource), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	resource->dev = &pdev->dev;
	resource->palmas = palmas;
	palmas->resource = resource;

	ret = misc_register(&palmas_resource_device);
	if (ret) {
		dev_dbg(&pdev->dev, "could not register misc_device\n");
		goto err_misc;
	}

	platform_set_drvdata(pdev, resource);

	ret = palmas_initialise_resource(resource, pdata);

	if (ret)
		goto err_int;

	return 0;

err_int:
	misc_deregister(&palmas_resource_device);
err_misc:
	kfree(resource);

	return ret;
}

static int palmas_resource_remove(struct platform_device *pdev)
{
	struct palmas_resource *resource = platform_get_drvdata(pdev);

	kfree(resource);
	misc_deregister(&palmas_resource_device);

	return 0;
}

static struct of_device_id of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-resource", },
	{ /* end */ }
};

static struct platform_driver palmas_resource_driver = {
	.probe = palmas_resource_probe,
	.remove = palmas_resource_remove,
	.driver = {
		.name = "palmas-resource",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
};

static int __init palmas_resource_init(void)
{
	return platform_driver_register(&palmas_resource_driver);
}
module_init(palmas_resource_init);

static void __exit palmas_resource_exit(void)
{
	platform_driver_unregister(&palmas_resource_driver);
}
module_exit(palmas_resource_exit);

MODULE_ALIAS("platform:palmas-resource");
MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("Palmas General Resource driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
