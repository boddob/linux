/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <dt-bindings/clock/qcom,dsiphy-msm8960.h>

#include "dsi_pll.h"
#include "dsi.xml.h"

/*
 * DSI PLL 28nm (8960/A family)- clock diagram (eg: DSI0):
 *
 *
 *
 *   F * bitclk      +------+
 *  dsi0vco_clk --o--| DIV1 |---dsi0pllbit
 *                |  +------+
 *                |  bit_div (F)
 *                |
 *		  |  +------+
 *                o--| DIV2 |---dsi0pllbyte--> To byte RCG
 *		  |  +------+
 *                |  byte_div (F * 8)
 *                | 
 *                |  +------+
 *                o--| DIV3 |---dsi0pll--o--->pixel rcg
 *                   +------+		 |
 *					 |
 *					 o--->dsi rcg
 *                  dsi_clk_div (F * magic)
 */

#define POLL_MAX_READS		8000
#define POLL_TIMEOUT_US		1

#define NUM_PROVIDED_CLKS	2

#define VCO_REF_CLK_RATE	19200000
#define VCO_MIN_RATE		600000000
#define VCO_MAX_RATE		1200000000

#define VCO_PREF_DIV_RATIO	27

struct pll_28nm_cached_state {
	unsigned long vco_rate;
	u8 postdiv3;
	u8 postdiv2;
	u8 postdiv1;
};

struct dsi_pll_28nm {
	struct msm_dsi_pll base;

	int id;
	struct platform_device *pdev;
	void __iomem *mmio;

	/* private clocks: */
	struct clk *clks[NUM_DSI_CLOCKS_MAX];
	u32 num_clks;

	/* clock-provider: */
	struct clk *provided_clks[NUM_PROVIDED_CLKS];
	struct clk_onecell_data clk_data;

	struct pll_28nm_cached_state cached_state;
};

#define to_pll_28nm(x)	container_of(x, struct dsi_pll_28nm, base)

static bool pll_28nm_poll_for_ready(struct dsi_pll_28nm *pll_28nm,
				u32 nb_tries, u32 timeout_us)
{
	bool pll_locked = false;
	u32 val;

	while (nb_tries--) {
		val = pll_read(pll_28nm->mmio + REG_DSI_28nm_8960_PHY_PLL_RDY);
		pll_locked = !!(val & DSI_28nm_8960_PHY_PLL_RDY_PLL_RDY);

		if (pll_locked)
			break;

		udelay(timeout_us);
	}
	DBG("DSI PLL is %slocked", pll_locked ? "" : "*not* ");

	return pll_locked;
}

/* do we have a way to reset A family PLL? */
static void pll_28nm_software_reset(struct dsi_pll_28nm *pll_28nm)
{
}

/*
 * Clock Callbacks
 */
static int dsi_pll_28nm_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);
	struct device *dev = &pll_28nm->pdev->dev;
	void __iomem *base = pll_28nm->mmio;
	unsigned long div_fbx1000, gen_vco_clk;
	u32 val;
	u32 fb_divider;
	u32 refclk_cfg, frac_n_mode, frac_n_value;
	u32 sdm_cfg0, sdm_cfg1, sdm_cfg2, sdm_cfg3;
	u32 cal_cfg10, cal_cfg11;
	u32 rem;
	int i;
	
	VERB("rate=%lu, parent's=%lu", rate, parent_rate);

	fb_divider = ((rate * VCO_PREF_DIV_RATIO) / 27);
	fb_divider =  fb_divider / 2 - 1;

	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_1,
			fb_divider & 0xff);

	/* maybe we need to read copy write here */
	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_2,
			(fb_divider >> 8) & 0x07);

	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_3,
			(VCO_PREF_DIV_RATIO - 1) & 0x3f);

	/* ? */
	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_5, 0x050);

	return 0;
}

static int dsi_pll_28nm_clk_is_enabled(struct clk_hw *hw)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);

	return pll_28nm_poll_for_ready(pll_28nm, POLL_MAX_READS,
					POLL_TIMEOUT_US);
}

static unsigned long dsi_pll_28nm_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);
	void __iomem *base = pll_28nm->mmio;
	unsigned long vco_rate;
	u32 status, fb_divider, temp, ref_divider;

	VERB("parent_rate=%lu", parent_rate);

	status = pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_0);

	if (status & DSI_28nm_8960_PHY_PLL_CTRL_0_ENABLE) {
		fb_divider = pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_1);
		fb_divider &= 0xff;
		temp = pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_2) & 0x07;
		fb_divider = (temp << 8) | fb_divider;
		fb_divider += 1;

		ref_divider = pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_3);
		ref_divider &= 0x3f;
		ref_divider += 1;

		 /* do we need to multiply fb_divider by 2 here? */
		vco_rate = (parent_rate / ref_divider) * fb_divider;
	} else {
		vco_rate = 0;
	}

	DBG("returning vco rate = %lu", vco_rate);

	return vco_rate;
}

static const struct clk_ops clk_ops_dsi_pll_28nm_vco = {
	.round_rate = msm_dsi_pll_helper_clk_round_rate,
	.set_rate = dsi_pll_28nm_clk_set_rate,
	.recalc_rate = dsi_pll_28nm_clk_recalc_rate,
	.prepare = msm_dsi_pll_helper_clk_prepare,
	.unprepare = msm_dsi_pll_helper_clk_unprepare,
	.is_enabled = dsi_pll_28nm_clk_is_enabled,
};

/*
 * PLL Callbacks
 */
static int dsi_pll_28nm_enable_seq(struct msm_dsi_pll *pll)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);
	struct device *dev = &pll_28nm->pdev->dev;
	void __iomem *base = pll_28nm->mmio;
	bool locked;
	u32 max_reads = 10, timeout_us = 50;
	u32 val;

	DBG("id=%d", pll_28nm->id);

	pll_28nm_software_reset(pll_28nm);

	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_0,
			DSI_28nm_8960_PHY_PLL_CTRL_0_ENABLE);

	locked = pll_28nm_poll_for_ready(pll_28nm, max_reads, timeout_us);

	if (unlikely(!locked))
		dev_err(dev, "DSI PLL lock failed\n");
	else
		DBG("DSI PLL lock success");

	return locked ? 0 : -EINVAL;
}

static void dsi_pll_28nm_disable_seq(struct msm_dsi_pll *pll)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);

	DBG("id=%d", pll_28nm->id);
	pll_write(pll_28nm->mmio + REG_DSI_28nm_8960_PHY_PLL_CTRL_0, 0x00);
}

static void dsi_pll_28nm_save_state(struct msm_dsi_pll *pll)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);
	struct pll_28nm_cached_state *cached_state = &pll_28nm->cached_state;
	void __iomem *base = pll_28nm->mmio;

	cached_state->postdiv3 =
			pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_10);
	cached_state->postdiv2 =
			pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_9);
	cached_state->postdiv1 =
			pll_read(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8);

	cached_state->vco_rate = __clk_get_rate(pll->clk_hw.clk);
}

static int dsi_pll_28nm_restore_state(struct msm_dsi_pll *pll)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);
	struct pll_28nm_cached_state *cached_state = &pll_28nm->cached_state;
	void __iomem *base = pll_28nm->mmio;
	int ret;

	ret = dsi_pll_28nm_clk_set_rate(&pll->clk_hw,
					cached_state->vco_rate, 0);
	if (ret) {
		dev_err(&pll_28nm->pdev->dev,
			"restore vco rate failed. ret=%d\n", ret);
		return ret;
	}

	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_10,
			cached_state->postdiv3);
	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_9,
			cached_state->postdiv2);
	pll_write(base + REG_DSI_28nm_8960_PHY_PLL_CTRL_8,
			cached_state->postdiv1);

	return 0;
}

static int dsi_pll_28nm_get_provider(struct msm_dsi_pll *pll,
				struct clk **byte_clk_provider,
				struct clk **pixel_clk_provider)
{
	*byte_clk_provider = *pixel_clk_provider = NULL;

	return 0;
}

static void dsi_pll_28nm_destroy(struct msm_dsi_pll *pll)
{
	struct dsi_pll_28nm *pll_28nm = to_pll_28nm(pll);
	int i;

	msm_dsi_pll_helper_unregister_clks(pll_28nm->pdev,
					pll_28nm->clks, pll_28nm->num_clks);

	pll_28nm->num_clks = 0;
}

static int pll_28nm_register(struct dsi_pll_28nm *pll_28nm)
{
	char clk_name[32], parent[32], vco_name[32];
	struct clk_init_data vco_init = {
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.name = vco_name,
		.ops = &clk_ops_dsi_pll_28nm_vco,
	};
	struct device *dev = &pll_28nm->pdev->dev;
	struct clk **clks = pll_28nm->clks;
	struct clk **provided_clks = pll_28nm->provided_clks;
	int ret, num = 0;

	DBG("%d", pll_28nm->id);

	snprintf(vco_name, 32, "dsi%dvco_clk", pll_28nm->id);
	pll_28nm->base.clk_hw.init = &vco_init;

	clks[num++] = provided_clks[DSI_VCO_CLK] =
			clk_register(dev, &pll_28nm->base.clk_hw);

	snprintf(parent, 32, "dsi%dvco_clk", pll_28nm->id);

	snprintf(clk_name, 32, "dsi%dpllbit", pll_28nm->id);
	/* DIV1 */
	clks[num++] = provided_clks[DSI_BIT_CLK] =
			clk_register_divider(dev, clk_name,
			parent, 0,
			pll_28nm->mmio +
			REG_DSI_28nm_8960_PHY_PLL_CTRL_8,
			0, 4, 0, NULL);

	snprintf(clk_name, 32, "dsi%dpllbyte", pll_28nm->id);
	/* DIV2 */
	clks[num++] = clk_register_divider(dev, clk_name,
			parent, 0,
			pll_28nm->mmio +
			REG_DSI_28nm_8960_PHY_PLL_CTRL_9,
			0, 7, 0, NULL);

	snprintf(clk_name, 32, "dsi%dpll", pll_28nm->id);
	/* DIV3 */
	clks[num++] = clk_register_divider(dev, clk_name,
				parent, 0, pll_28nm->mmio +
				REG_DSI_28nm_8960_PHY_PLL_CTRL_10,
				0, 8, 0, NULL);

	pll_28nm->num_clks = num;

	pll_28nm->clk_data.clk_num = NUM_PROVIDED_CLKS;
	pll_28nm->clk_data.clks = provided_clks;

	ret = of_clk_add_provider(dev->of_node,
			of_clk_src_onecell_get, &pll_28nm->clk_data);
	if (ret) {
		dev_err(dev, "failed to register clk provider: %d\n", ret);
		return ret;
	}

	return 0;
}

struct msm_dsi_pll *msm_dsi_pll_28nm_8960_init(struct platform_device *pdev,
					       int id)
{
	struct dsi_pll_28nm *pll_28nm;
	struct msm_dsi_pll *pll;
	int ret;

	if (!pdev)
		return ERR_PTR(-ENODEV);

	pll_28nm = devm_kzalloc(&pdev->dev, sizeof(*pll_28nm), GFP_KERNEL);
	if (!pll_28nm)
		return ERR_PTR(-ENOMEM);

	pll_28nm->pdev = pdev;
	pll_28nm->id = id;

	pll_28nm->mmio = msm_ioremap(pdev, "dsi_pll", "DSI_PLL");
	if (IS_ERR_OR_NULL(pll_28nm->mmio)) {
		dev_err(&pdev->dev, "%s: failed to map pll base\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	pll = &pll_28nm->base;
	pll->min_rate = VCO_MIN_RATE;
	pll->max_rate = VCO_MAX_RATE;
	pll->get_provider = dsi_pll_28nm_get_provider;
	pll->destroy = dsi_pll_28nm_destroy;
	pll->disable_seq = dsi_pll_28nm_disable_seq;
	pll->save_state = dsi_pll_28nm_save_state;
	pll->restore_state = dsi_pll_28nm_restore_state;

	pll->en_seq_cnt = 1;
	pll->enable_seqs[0] = dsi_pll_28nm_enable_seq;

	ret = pll_28nm_register(pll_28nm);
	if (ret) {
		dev_err(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ERR_PTR(ret);
	}

	return pll;
}
