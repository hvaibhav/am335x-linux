/*
 * OMAP2xxx APLL clock control functions
 *
 * Copyright (C) 2005-2008 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 *
 * Based on earlier work by Tuukka Tikkanen, Tony Lindgren,
 * Gordon McNutt and RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <plat/clock.h>
#include <plat/prcm.h>

#include "clock.h"
#include "clock2xxx.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-24xx.h"

/* CM_CLKEN_PLL.EN_{54,96}M_PLL options (24XX) */
#define EN_APLL_STOPPED			0
#define EN_APLL_LOCKED			3

/* CM_CLKSEL1_PLL.APLLS_CLKIN options (24XX) */
#define APLLS_CLKIN_19_2MHZ		0
#define APLLS_CLKIN_13MHZ		2
#define APLLS_CLKIN_12MHZ		3

void __iomem *cm_idlest_pll;

/* Private functions */

/* Enable an APLL if off */
#ifdef CONFIG_COMMON_CLK
static int omap2_clk_apll_enable(struct clk_hw *hw, u32 status_mask)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
#else
static int omap2_clk_apll_enable(struct clk *clk, u32 status_mask)
{
#endif
	u32 cval, apll_mask;

	apll_mask = EN_APLL_LOCKED << clk->enable_bit;

	cval = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKEN);

	if ((cval & apll_mask) == apll_mask)
		return 0;   /* apll already enabled */

	cval &= ~apll_mask;
	cval |= apll_mask;
	omap2_cm_write_mod_reg(cval, PLL_MOD, CM_CLKEN);

	omap2_cm_wait_idlest(cm_idlest_pll, status_mask,
#ifdef CONFIG_COMMON_CLK
			     OMAP24XX_CM_IDLEST_VAL, __clk_get_name(hw->clk));
#else
			     OMAP24XX_CM_IDLEST_VAL, __clk_get_name(clk));
#endif

	/*
	 * REVISIT: Should we return an error code if omap2_wait_clock_ready()
	 * fails?
	 */
	return 0;
}

#ifdef CONFIG_COMMON_CLK
int omap2_clk_apll96_enable(struct clk_hw *clk)
#else
static int omap2_clk_apll96_enable(struct clk *clk)
#endif
{
	return omap2_clk_apll_enable(clk, OMAP24XX_ST_96M_APLL_MASK);
}

#ifdef CONFIG_COMMON_CLK
int omap2_clk_apll54_enable(struct clk_hw *clk)
#else
static int omap2_clk_apll54_enable(struct clk *clk)
#endif
{
	return omap2_clk_apll_enable(clk, OMAP24XX_ST_54M_APLL_MASK);
}

#ifdef CONFIG_COMMON_CLK
void _apll96_allow_idle(struct clk_hw_omap *clk)
#else
static void _apll96_allow_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll96_auto_low_power_stop();
}

#ifdef CONFIG_COMMON_CLK
void _apll96_deny_idle(struct clk_hw_omap *clk)
#else
static void _apll96_deny_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll96_disable_autoidle();
}

#ifdef CONFIG_COMMON_CLK
void _apll54_allow_idle(struct clk_hw_omap *clk)
#else
static void _apll54_allow_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll54_auto_low_power_stop();
}

#ifdef CONFIG_COMMON_CLK
void _apll54_deny_idle(struct clk_hw_omap *clk)
#else
static void _apll54_deny_idle(struct clk *clk)
#endif
{
	omap2xxx_cm_set_apll54_disable_autoidle();
}

/* Stop APLL */
#ifdef CONFIG_COMMON_CLK
void omap2_clk_apll_disable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
#else
static void omap2_clk_apll_disable(struct clk *clk)
{
#endif
	u32 cval;

	cval = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKEN);
	cval &= ~(EN_APLL_LOCKED << clk->enable_bit);
	omap2_cm_write_mod_reg(cval, PLL_MOD, CM_CLKEN);
}

/* Public data */
#ifdef CONFIG_COMMON_CLK
const struct clk_hw_omap_ops clkhwops_apll54 = {
	.allow_idle	= _apll54_allow_idle,
	.deny_idle	= _apll54_deny_idle,
};

const struct clk_hw_omap_ops clkhwops_apll96 = {
	.allow_idle	= _apll96_allow_idle,
	.deny_idle	= _apll96_deny_idle,
};
#else
const struct clkops clkops_apll96 = {
	.enable		= omap2_clk_apll96_enable,
	.disable	= omap2_clk_apll_disable,
	.allow_idle	= _apll96_allow_idle,
	.deny_idle	= _apll96_deny_idle,
};

const struct clkops clkops_apll54 = {
	.enable		= omap2_clk_apll54_enable,
	.disable	= omap2_clk_apll_disable,
	.allow_idle	= _apll54_allow_idle,
	.deny_idle	= _apll54_deny_idle,
};
#endif

/* Public functions */

u32 omap2xxx_get_apll_clkin(void)
{
	u32 aplls, srate = 0;

	aplls = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL1);
	aplls &= OMAP24XX_APLLS_CLKIN_MASK;
	aplls >>= OMAP24XX_APLLS_CLKIN_SHIFT;

	if (aplls == APLLS_CLKIN_19_2MHZ)
		srate = 19200000;
	else if (aplls == APLLS_CLKIN_13MHZ)
		srate = 13000000;
	else if (aplls == APLLS_CLKIN_12MHZ)
		srate = 12000000;

	return srate;
}

