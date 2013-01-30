/*
 * PALMAS resource clock module driver
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

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/palmas.h>
#include <linux/clk-provider.h>
#include <linux/of_platform.h>

struct palmas_clk {
	struct palmas *palmas;
	struct device *dev;
	struct clk_hw clk32kg;
	struct clk_hw clk32kgaudio;
};

static int palmas_clock_setbits(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RESOURCE_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE, reg);

	return regmap_update_bits(palmas->regmap[slave], addr, data, data);
}

static int palmas_clock_clrbits(struct palmas *palmas, unsigned int reg,
		unsigned int data)
{
	unsigned int addr;
	int slave;

	slave = PALMAS_BASE_TO_SLAVE(PALMAS_RESOURCE_BASE);
	addr = PALMAS_BASE_TO_REG(PALMAS_RESOURCE_BASE, reg);

	return regmap_update_bits(palmas->regmap[slave], addr, data, 0);
}

static int palmas_prepare_clk32kg(struct clk_hw *hw)
{
	struct palmas_clk *palmas_clk = container_of(hw, struct palmas_clk,
			clk32kg);
	int ret;

	ret = palmas_clock_setbits(palmas_clk->palmas,
			PALMAS_CLK32KG_CTRL, PALMAS_CLK32KG_CTRL_MODE_ACTIVE);
	if (ret)
		dev_err(palmas_clk->dev, "Failed to enable clk32kg: %d\n", ret);

	return ret;
}

static void palmas_unprepare_clk32kg(struct clk_hw *hw)
{
	struct palmas_clk *palmas_clk = container_of(hw, struct palmas_clk,
			clk32kg);
	int ret;

	ret = palmas_clock_clrbits(palmas_clk->palmas,
			PALMAS_CLK32KG_CTRL, PALMAS_CLK32KG_CTRL_MODE_ACTIVE);
	if (ret)
		dev_err(palmas_clk->dev, "Failed to enable clk32kg: %d\n", ret);

	return;
}

static const struct clk_ops palmas_clk32kg_ops = {
	.prepare = palmas_prepare_clk32kg,
	.unprepare = palmas_unprepare_clk32kg,
};

static int palmas_prepare_clk32kgaudio(struct clk_hw *hw)
{
	struct palmas_clk *palmas_clk = container_of(hw, struct palmas_clk,
			clk32kgaudio);
	int ret;

	ret = palmas_clock_setbits(palmas_clk->palmas,
		PALMAS_CLK32KGAUDIO_CTRL, PALMAS_CLK32KGAUDIO_CTRL_MODE_ACTIVE);
	if (ret)
		dev_err(palmas_clk->dev,
				"Failed to enable clk32kgaudio: %d\n", ret);

	return ret;
}

static void palmas_unprepare_clk32kgaudio(struct clk_hw *hw)
{
	struct palmas_clk *palmas_clk = container_of(hw, struct palmas_clk,
			clk32kgaudio);
	int ret;

	ret = palmas_clock_clrbits(palmas_clk->palmas,
		PALMAS_CLK32KGAUDIO_CTRL, PALMAS_CLK32KGAUDIO_CTRL_MODE_ACTIVE);
	if (ret)
		dev_err(palmas_clk->dev,
				"Failed to enable clk32kgaudio: %d\n", ret);

	return;
}

static const struct clk_ops palmas_clk32kgaudio_ops = {
	.prepare = palmas_prepare_clk32kgaudio,
	.unprepare = palmas_unprepare_clk32kgaudio,
};

static int palmas_initialise_clk(struct palmas_clk *palmas_clk,
		struct palmas_clk_platform_data *pdata)
{
	int ret;

	if (pdata->clk32kg_mode_sleep) {
		ret = palmas_clock_setbits(palmas_clk->palmas,
			PALMAS_CLK32KG_CTRL, PALMAS_CLK32KG_CTRL_MODE_SLEEP);
		if (ret)
			return ret;
	}

	if (pdata->clk32kgaudio_mode_sleep) {
		ret = palmas_clock_setbits(palmas_clk->palmas,
			PALMAS_CLK32KGAUDIO_CTRL,
			PALMAS_CLK32KGAUDIO_CTRL_MODE_SLEEP);
		if (ret)
			return ret;
	}

	return 0;
}

static void __devinit palmas_dt_to_pdata(struct device_node *node,
		struct palmas_clk_platform_data *pdata)
{
	int ret;
	u32 prop;

	ret = of_property_read_u32(node, "ti,clk32kg_mode_sleep", &prop);
	if (!ret) {
		pdata->clk32kg_mode_sleep = prop;
	}

	ret = of_property_read_u32(node, "ti,clk32kgaudio_mode_sleep", &prop);
	if (!ret) {
		pdata->clk32kgaudio_mode_sleep = prop;
	}
}

static __devinit int palmas_clk_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_clk_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *node = pdev->dev.of_node;
	struct palmas_clk *palmas_clk;
	int ret;

	if(node && !pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);

		if (!pdata)
			return -ENOMEM;

		palmas_dt_to_pdata(node, pdata);
	}

	palmas_clk = kzalloc(sizeof(*palmas_clk), GFP_KERNEL);
	if (!palmas_clk)
		return -ENOMEM;

	palmas_clk->palmas = palmas;
	palmas_clk->dev = &pdev->dev;

	if (!clk_register(palmas_clk->dev, "clk32kg", &palmas_clk32kg_ops,
			  &palmas_clk->clk32kg, NULL, 0, CLK_IS_ROOT))
	{
		ret = -EINVAL;
		goto err_clk32kg;
	}

	if (!clk_register(palmas_clk->dev, "clk32kgaudio",
			  &palmas_clk32kgaudio_ops, &palmas_clk->clk32kgaudio,
			  NULL, 0, CLK_IS_ROOT))
	{
		ret = -EINVAL;
		goto err_audio;
	}

	ret = palmas_initialise_clk(palmas_clk, pdata);
	if (ret)
		goto err;

	dev_set_drvdata(&pdev->dev, palmas_clk);

	return 0;

err:
	clk_unregister(palmas_clk->clk32kgaudio.clk);
err_audio:
	clk_unregister(palmas_clk->clk32kg.clk);
err_clk32kg:
	kfree(palmas_clk);

	return ret;
}

static __devexit int palmas_clk_remove(struct platform_device *pdev)
{
	struct palmas_clk *palmas_clk = dev_get_drvdata(&pdev->dev);

	clk_unregister(palmas_clk->clk32kgaudio.clk);
	clk_unregister(palmas_clk->clk32kg.clk);

	kfree(palmas_clk);

	return 0;
}

static struct of_device_id __devinitdata of_palmas_match_tbl[] = {
	{ .compatible = "ti,palmas-clk", },
	{ /* end */ }
};

static struct platform_driver palmas_clk_driver = {
	.probe = palmas_clk_probe,
	.remove = __devexit_p(palmas_clk_remove),
	.driver = {
		.name = "palmas-clk",
		.of_match_table = of_palmas_match_tbl,
		.owner = THIS_MODULE,
	},
};

static int __init palmas_clk_init(void)
{
	return platform_driver_register(&palmas_clk_driver);
}
module_init(palmas_clk_init);

static void __exit palmas_clk_exit(void)
{
	platform_driver_unregister(&palmas_clk_driver);
}
module_exit(palmas_clk_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_DESCRIPTION("PALMAS clock driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:palmas-clk");
MODULE_DEVICE_TABLE(of, of_palmas_match_tbl);
