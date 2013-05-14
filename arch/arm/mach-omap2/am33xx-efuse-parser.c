/*
 * TI AM33xx CPUFreq/OPP eFuse data Parser
 *
 * Copyright (C) {2013} Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/memory.h>
#include <linux/opp.h>

#include "common.h"
#include "iomap.h"
#include "control.h"

/*
 * Below enum is arranged as per bit allocation
 * for respective OPP, they are 1-2-1 mapped.
 */
enum {
	EFUSE_OPP_100_275 = 0,
	EFUSE_OPP_100_500,
	EFUSE_OPP_120_600,
	EFUSE_OPP_TB_720,
	EFUSE_OPP_50_300,
	EFUSE_OPP_100_300,
	EFUSE_OPP_100_600,
	EFUSE_OPP_120_720,
	EFUSE_OPP_TB_800,
	EFUSE_OPP_NT_1000,
	EFUSE_OPP_RSVD1,
	EFUSE_OPP_RSVD2,
	EFUSE_OPP_RSVD3,
};

/* Total available OPPs across devices and Si versions */
#define MAX_AVAIL_OPPS EFUSE_OPP_RSVD3

struct opp_def {
	unsigned long freq;
	unsigned long u_volt;
};

/*
 * Each OPP is a set of tuples consisting of frequency and
 * voltage like <freq-kHz vol-uV>.
 */
static struct opp_def am335x_avail_opps[] = {
	[EFUSE_OPP_100_275] = {
		.freq	= 275000,
		.u_volt	= 1100000,
	},
	[EFUSE_OPP_100_500] = {
		.freq	= 500000,
		.u_volt	= 1100000,
	},
	[EFUSE_OPP_120_600] = {
		.freq	= 600000,
		.u_volt	= 1200000,
	},
	[EFUSE_OPP_TB_720] = {
		.freq	= 720000,
		.u_volt	= 1260000,
	},
	[EFUSE_OPP_50_300] = {
		.freq	= 300000,
		.u_volt	= 950000,
	},
	[EFUSE_OPP_100_300] = {
		.freq	= 300000,
		.u_volt	= 1100000,
	},
	[EFUSE_OPP_100_600] = {
		.freq	= 600000,
		.u_volt	= 1100000,
	},
	[EFUSE_OPP_120_720] = {
		.freq	= 720000,
		.u_volt	= 1200000,
	},
	[EFUSE_OPP_TB_800] = {
		.freq	= 800000,
		.u_volt	= 1260000,
	},
	[EFUSE_OPP_NT_1000] = {
		.freq	= 1000000,
		.u_volt	= 1325000,
	},
	/* Bits reserved for future new OPP defination */
	[EFUSE_OPP_RSVD1] = {
		.freq	= 0,
		.u_volt	= 0,
	},
	[EFUSE_OPP_RSVD2] = {
		.freq	= 0,
		.u_volt	= 0,
	},
	[EFUSE_OPP_RSVD3] = {
		.freq	= 0,
		.u_volt	= 0,
	},
};

static inline int of_add_opp(struct property *prop, u32 opp_bit)
{
	__be32 *new_value;

	/*
	 * Each OPP is a set of tuples consisting of frequency and
	 * voltage like <freq-kHz vol-uV>.
	 */
	prop->value = krealloc(prop->value,
			prop->length + sizeof(struct opp_def), GFP_KERNEL);
	if (!prop->value)
		return -ENOMEM;

	new_value = prop->value + prop->length;
	prop->length += sizeof(struct opp_def);

	new_value[0] = cpu_to_be32(am335x_avail_opps[opp_bit].freq);
	new_value[1] = cpu_to_be32(am335x_avail_opps[opp_bit].u_volt);

	return 0;
}

int am33xx_init_opp_from_efuse(void)
{
	int val, ret = 0;
	unsigned long *addr;
	struct device_node *np;
	struct property *new_prop;

	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np) {
		pr_err("failed to find cpu0 node\n");
		return -ENOENT;
	}

	new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
	if (new_prop == NULL)
		return -ENOMEM;

	new_prop->name = kstrdup("operating-points", GFP_KERNEL);
	if (new_prop->name == NULL) {
		ret = -ENOMEM;
		goto err1;
	}

	/* Check availability of eFuse info */
	val = omap_ctrl_readl(AM33XX_CONTROL_EFUSE_SMA);
	if (!val) {
		/*
		 * If eFuse's are not blown for OPPs, then
		 * add static OPP's as per PG1.0
		 */
		ret |= of_add_opp(new_prop, 0);	/* 275MHz@1.1 */
		ret |= of_add_opp(new_prop, 1);	/* 500MHz@1.1 */
		ret |= of_add_opp(new_prop, 2);	/* 600MHz@1.2 */
		ret |= of_add_opp(new_prop, 3);	/* 720MHz@1.26 */
	} else {
		addr = omap_ctrl_base_get() + AM33XX_CONTROL_EFUSE_SMA;
		for_each_clear_bit(val, addr, MAX_AVAIL_OPPS)
			ret |= of_add_opp(new_prop, val);
	}

	if (ret) {
		pr_err("failed to update opp table\n");
		goto err2;
	}

	return of_update_property(np, new_prop);

err2:
	kfree(new_prop->name);
	kfree(new_prop->value);
err1:
	kfree(new_prop);
	return ret;
}
