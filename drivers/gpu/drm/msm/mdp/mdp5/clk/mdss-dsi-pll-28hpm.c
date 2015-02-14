/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>

#include "mdss-pll.h"
#include "mdss-dsi-pll.h"

#define DSI_PHY_PLL_UNIPHY_PLL_REFCLK_CFG	(0x0)
#define DSI_PHY_PLL_UNIPHY_PLL_POSTDIV1_CFG	(0x0004)
#define DSI_PHY_PLL_UNIPHY_PLL_CHGPUMP_CFG	(0x0008)
#define DSI_PHY_PLL_UNIPHY_PLL_VCOLPF_CFG	(0x000C)
#define DSI_PHY_PLL_UNIPHY_PLL_VREG_CFG		(0x0010)
#define DSI_PHY_PLL_UNIPHY_PLL_PWRGEN_CFG	(0x0014)
#define DSI_PHY_PLL_UNIPHY_PLL_DMUX_CFG		(0x0018)
#define DSI_PHY_PLL_UNIPHY_PLL_AMUX_CFG		(0x001C)
#define DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG		(0x0020)
#define DSI_PHY_PLL_UNIPHY_PLL_POSTDIV2_CFG	(0x0024)
#define DSI_PHY_PLL_UNIPHY_PLL_POSTDIV3_CFG	(0x0028)
#define DSI_PHY_PLL_UNIPHY_PLL_LPFR_CFG		(0x002C)
#define DSI_PHY_PLL_UNIPHY_PLL_LPFC1_CFG	(0x0030)
#define DSI_PHY_PLL_UNIPHY_PLL_LPFC2_CFG	(0x0034)
#define DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG0		(0x0038)
#define DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG1		(0x003C)
#define DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG2		(0x0040)
#define DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG3		(0x0044)
#define DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG4		(0x0048)
#define DSI_PHY_PLL_UNIPHY_PLL_SSC_CFG0		(0x004C)
#define DSI_PHY_PLL_UNIPHY_PLL_SSC_CFG1		(0x0050)
#define DSI_PHY_PLL_UNIPHY_PLL_SSC_CFG2		(0x0054)
#define DSI_PHY_PLL_UNIPHY_PLL_SSC_CFG3		(0x0058)
#define DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG0	(0x005C)
#define DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG1	(0x0060)
#define DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2	(0x0064)
#define DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG		(0x0068)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG0		(0x006C)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG1		(0x0070)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG2		(0x0074)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG3		(0x0078)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG4		(0x007C)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG5		(0x0080)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG6		(0x0084)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG7		(0x0088)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG8		(0x008C)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG9		(0x0090)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG10	(0x0094)
#define DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG11	(0x0098)
#define DSI_PHY_PLL_UNIPHY_PLL_EFUSE_CFG	(0x009C)
#define DSI_PHY_PLL_UNIPHY_PLL_STATUS		(0x00C0)


#define DSI_PLL_POLL_MAX_READS			10
#define DSI_PLL_POLL_TIMEOUT_US			50

static inline struct dsi_pll_vco_clk *to_vco_clk(struct clk_hw *hw)
{
	return container_of(hw, struct dsi_pll_vco_clk, hw);
}

static int dsi_pll_lock_status(struct mdss_pll_resources *dsi_pll_res)
{
	u32 status;
	int count;

	/* poll for PLL ready status */
	for (count = DSI_PLL_POLL_MAX_READS; count > 0; count--) {
		status = readl(dsi_pll_res->pll_base +
				DSI_PHY_PLL_UNIPHY_PLL_STATUS);
		if (status & BIT(0))
			break;
		udelay(DSI_PLL_POLL_TIMEOUT_US);
	}
	if (!(status & BIT(0))) {
		pr_debug("DSI PLL status=%x failed to Lock\n", status);
		return 0;
	}
	return 1;
}

static void dsi_pll_software_reset(struct mdss_pll_resources *dsi_pll_res)
{
	/*
	 * Add HW recommended delays after toggling the software
	 * reset bit off and back on.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x01);
	udelay(1);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_TEST_CFG, 0x00);
	udelay(1);
}

static int dsi_pll_enable_seq_8974(struct mdss_pll_resources *dsi_pll_res)
{
	int i, rc = 0;
	u32 max_reads, timeout_us;
	int pll_locked;

	dsi_pll_software_reset(dsi_pll_res);

	/*
	 * PLL power up sequence.
	 * Add necessary delays recommeded by hardware.
	 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x01);
	udelay(1);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x05);
	udelay(200);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x07);
	udelay(500);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x0f);
	udelay(500);

	for (i = 0; i < 2; i++) {
		udelay(100);
		/* DSI Uniphy lock detect setting */
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x0c);
		udelay(100);
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x0d);
		/* poll for PLL ready status */
		max_reads = 5;
		timeout_us = 100;

		pll_locked = dsi_pll_lock_status(dsi_pll_res);
		if (pll_locked)
			break;

		dsi_pll_software_reset(dsi_pll_res);
		/*
		 * PLL power up sequence.
		 * Add necessary delays recommeded by hardware.
		 */
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x1);
		udelay(1);
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x5);
		udelay(200);
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x7);
		udelay(250);
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x5);
		udelay(200);
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x7);
		udelay(500);
		MDSS_PLL_REG_W(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0xf);
		udelay(500);

	}

	if (!pll_locked) {
		pr_err("DSI PLL lock failed\n");
		rc = -EINVAL;
	} else {
		pr_debug("DSI PLL Lock success\n");
	}

	return rc;
}

static int dsi_pll_enable(struct clk_hw *hw)
{
	int i, rc;
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	rc = mdss_pll_resource_enable(dsi_pll_res, true);
	if (rc) {
		pr_err("Failed to enable mdss dsi pll resources\n");
		return rc;
	}

	/* Try all enable sequences until one succeeds */
	for (i = 0; i < vco->pll_en_seq_cnt; i++) {
		rc = vco->pll_enable_seqs[i](dsi_pll_res);
		pr_debug("DSI PLL %s after sequence #%d\n",
			rc ? "unlocked" : "locked", i + 1);
		if (!rc)
			break;
	}

	if (rc) {
		mdss_pll_resource_enable(dsi_pll_res, false);
		pr_err("DSI PLL failed to lock\n");
	}
	dsi_pll_res->pll_on = true;

	return rc;
}

static void dsi_pll_disable(struct clk_hw *hw)
{
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	if (!dsi_pll_res->pll_on &&
		mdss_pll_resource_enable(dsi_pll_res, true)) {
		pr_err("Failed to enable mdss dsi pll resources\n");
		return;
	}

	dsi_pll_res->handoff_resources = false;

	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_GLB_CFG, 0x00);

	mdss_pll_resource_enable(dsi_pll_res, false);
	dsi_pll_res->pll_on = false;

	pr_debug("DSI PLL Disabled\n");
	return;
}

static int vco_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	s64 vco_clk_rate = rate;
	s32 rem;
	s64 refclk_cfg, frac_n_mode, ref_doubler_en_b;
	s64 ref_clk_to_pll, div_fbx1000, frac_n_value;
	s64 sdm_cfg0, sdm_cfg1, sdm_cfg2, sdm_cfg3;
	s64 gen_vco_clk, cal_cfg10, cal_cfg11;
	u32 res;
	int i, rc;
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	rc = mdss_pll_resource_enable(dsi_pll_res, true);
	if (rc) {
		pr_err("Failed to enable mdss dsi pll resources\n");
		return rc;
	}

	/* Configure the Loop filter resistance */
	for (i = 0; i < vco->lpfr_lut_size; i++)
		if (vco_clk_rate <= vco->lpfr_lut[i].vco_rate)
			break;
	if (i == vco->lpfr_lut_size) {
		pr_err("unable to get loop filter resistance. vco=%ld\n", rate);
		rc = -EINVAL;
		goto error;
	}
	res = vco->lpfr_lut[i].r;
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_LPFR_CFG, res);

	/* Loop filter capacitance values : c1 and c2 */
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_LPFC1_CFG, 0x70);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_LPFC2_CFG, 0x15);

	div_s64_rem(vco_clk_rate, vco->ref_clk_rate, &rem);
	if (rem) {
		refclk_cfg = 0x1;
		frac_n_mode = 1;
		ref_doubler_en_b = 0;
	} else {
		refclk_cfg = 0x0;
		frac_n_mode = 0;
		ref_doubler_en_b = 1;
	}

	pr_debug("refclk_cfg = %lld\n", refclk_cfg);

	ref_clk_to_pll = ((vco->ref_clk_rate * 2 * (refclk_cfg))
			  + (ref_doubler_en_b * vco->ref_clk_rate));
	div_fbx1000 = div_s64((vco_clk_rate * 1000), ref_clk_to_pll);

	div_s64_rem(div_fbx1000, 1000, &rem);
	frac_n_value = div_s64((rem * (1 << 16)), 1000);
	gen_vco_clk = div_s64(div_fbx1000 * ref_clk_to_pll, 1000);

	pr_debug("ref_clk_to_pll = %lld\n", ref_clk_to_pll);
	pr_debug("div_fb = %lld\n", div_fbx1000);
	pr_debug("frac_n_value = %lld\n", frac_n_value);

	pr_debug("Generated VCO Clock: %lld\n", gen_vco_clk);
	rem = 0;
	if (frac_n_mode) {
		sdm_cfg0 = (0x0 << 5);
		sdm_cfg0 |= (0x0 & 0x3f);
		sdm_cfg1 = (div_s64(div_fbx1000, 1000) & 0x3f) - 1;
		sdm_cfg3 = div_s64_rem(frac_n_value, 256, &rem);
		sdm_cfg2 = rem;
	} else {
		sdm_cfg0 = (0x1 << 5);
		sdm_cfg0 |= (div_s64(div_fbx1000, 1000) & 0x3f) - 1;
		sdm_cfg1 = (0x0 & 0x3f);
		sdm_cfg2 = 0;
		sdm_cfg3 = 0;
	}

	pr_debug("sdm_cfg0=%lld\n", sdm_cfg0);
	pr_debug("sdm_cfg1=%lld\n", sdm_cfg1);
	pr_debug("sdm_cfg2=%lld\n", sdm_cfg2);
	pr_debug("sdm_cfg3=%lld\n", sdm_cfg3);

	cal_cfg11 = div_s64_rem(gen_vco_clk, 256 * 1000000, &rem);
	cal_cfg10 = rem / 1000000;
	pr_debug("cal_cfg10=%lld, cal_cfg11=%lld\n", cal_cfg10, cal_cfg11);

	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CHGPUMP_CFG, 0x02);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG3, 0x2b);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG4, 0x66);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_LKDET_CFG2, 0x0d);

	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
		DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG1, (u32)(sdm_cfg1 & 0xff));
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
		DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG2, (u32)(sdm_cfg2 & 0xff));
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
		DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG3, (u32)(sdm_cfg3 & 0xff));
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
				DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG4, 0x00);

	/* Add hardware recommended delay for correct PLL configuration */
	udelay(1);

	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_REFCLK_CFG, (u32)refclk_cfg);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_PWRGEN_CFG, 0x00);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_VCOLPF_CFG, 0x71);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG0, (u32)sdm_cfg0);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG0, 0x12);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG6, 0x30);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG7, 0x00);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG8, 0x60);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG9, 0x00);
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
		DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG10, (u32)(cal_cfg10 & 0xff));
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
		DSI_PHY_PLL_UNIPHY_PLL_CAL_CFG11, (u32)(cal_cfg11 & 0xff));
	MDSS_PLL_REG_W(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_EFUSE_CFG, 0x20);

error:
	mdss_pll_resource_enable(dsi_pll_res, false);
	return rc;
}

static long vco_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	unsigned long rrate = rate;
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);

	if (rate < vco->min_rate)
		rrate = vco->min_rate;
	if (rate > vco->max_rate)
		rrate = vco->max_rate;

	return rrate;
}

static int dsi_pll_is_enabled(struct clk_hw *hw)
{
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;
	int rc;

	rc = mdss_pll_resource_enable(dsi_pll_res, true);
	if (rc) {
		pr_err("%s: failed to enable mdss ahb clock. rc=%d\n",
			__func__, rc);
		return 0;
	}

	rc = dsi_pll_lock_status(dsi_pll_res);

	mdss_pll_resource_enable(dsi_pll_res, false);

	return rc;
}

static unsigned long vco_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	u32 sdm0, doubler, sdm_byp_div;
	u64 vco_rate;
	u32 sdm_dc_off, sdm_freq_seed, sdm2, sdm3;
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	u64 ref_clk = vco->ref_clk_rate;
	int rc;
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	rc = mdss_pll_resource_enable(dsi_pll_res, true);
	if (rc) {
		pr_err("Failed to enable mdss dsi pll resources\n");
		return 0;
	}

	/* Check to see if the ref clk doubler is enabled */
	doubler = MDSS_PLL_REG_R(dsi_pll_res->pll_base,
				 DSI_PHY_PLL_UNIPHY_PLL_REFCLK_CFG) & BIT(0);
	ref_clk += (doubler * vco->ref_clk_rate);

	/* see if it is integer mode or sdm mode */
	sdm0 = MDSS_PLL_REG_R(dsi_pll_res->pll_base,
					DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG0);
	if (sdm0 & BIT(6)) {
		/* integer mode */
		sdm_byp_div = (MDSS_PLL_REG_R(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG0) & 0x3f) + 1;
		vco_rate = ref_clk * sdm_byp_div;
	} else {
		/* sdm mode */
		sdm_dc_off = MDSS_PLL_REG_R(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG1) & 0xFF;
		pr_debug("sdm_dc_off = %d\n", sdm_dc_off);
		sdm2 = MDSS_PLL_REG_R(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG2) & 0xFF;
		sdm3 = MDSS_PLL_REG_R(dsi_pll_res->pll_base,
			DSI_PHY_PLL_UNIPHY_PLL_SDM_CFG3) & 0xFF;
		sdm_freq_seed = (sdm3 << 8) | sdm2;
		pr_debug("sdm_freq_seed = %d\n", sdm_freq_seed);

		vco_rate = (ref_clk * (sdm_dc_off + 1)) +
			mult_frac(ref_clk, sdm_freq_seed, BIT(16));
		pr_debug("vco rate = %lld", vco_rate);
	}

	pr_debug("returning vco rate = %lu\n", (unsigned long)vco_rate);

	mdss_pll_resource_enable(dsi_pll_res, false);

	return (unsigned long)vco_rate;
}

static int vco_prepare(struct clk_hw *hw)
{
	int rc = 0;
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	if (!dsi_pll_res) {
		pr_err("Dsi pll resources are not available\n");
		return -EINVAL;
	}

	if ((dsi_pll_res->vco_cached_rate != 0)
	    && (dsi_pll_res->vco_cached_rate == __clk_get_rate(hw->clk))) {
		rc = vco_set_rate(hw, dsi_pll_res->vco_cached_rate, 0);
		if (rc) {
			pr_err("vco_set_rate failed. rc=%d\n", rc);
			goto error;
		}
	}

	rc = dsi_pll_enable(hw);

error:
	return rc;
}

static void vco_unprepare(struct clk_hw *hw)
{
	struct dsi_pll_vco_clk *vco = to_vco_clk(hw);
	struct mdss_pll_resources *dsi_pll_res = vco->priv;

	if (!dsi_pll_res) {
		pr_err("Dsi pll resources are not available\n");
		return;
	}

	dsi_pll_res->vco_cached_rate = __clk_get_rate(hw->clk);
	dsi_pll_disable(hw);
}

/* Op structures */

static const struct clk_ops clk_ops_dsi_vco = {
	.set_rate = vco_set_rate,
	.round_rate = vco_round_rate,
	.recalc_rate = vco_recalc_rate,
	.prepare = vco_prepare,
	.unprepare = vco_unprepare,
	.is_enabled = dsi_pll_is_enabled,
};


static struct dsi_pll_vco_clk dsi_vco_clk_8974 = {
	.ref_clk_rate = 19200000,
	.min_rate = 350000000,
	.max_rate = 750000000,
	.pll_en_seq_cnt = 3,
	.pll_enable_seqs[0] = dsi_pll_enable_seq_8974,
	.pll_enable_seqs[1] = dsi_pll_enable_seq_8974,
	.pll_enable_seqs[2] = dsi_pll_enable_seq_8974,
	.lpfr_lut_size = 10,
	.lpfr_lut = (struct lpfr_cfg[]){
		{479500000, 8},
		{480000000, 11},
		{575500000, 8},
		{576000000, 12},
		{610500000, 8},
		{659500000, 9},
		{671500000, 10},
		{672000000, 14},
		{708500000, 10},
		{750000000, 11},
	},
	.hw.init = &(struct clk_init_data){
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.name = "dsi_vco_clk",
		.ops = &clk_ops_dsi_vco,
	},
};

int dsi_pll_clock_register(struct platform_device *pdev,
				struct mdss_pll_resources *pll_res)
{
	if (!pll_res || !pll_res->pll_base) {
		pr_err("Invalid input parameters\n");
		return -EPROBE_DEFER;
	}

	dsi_vco_clk_8974.priv = pll_res;

	clk_register(&pdev->dev, &dsi_vco_clk_8974.hw);
	clk_register_divider(&pdev->dev, "dsi_analog_postdiv_clk",
			     "dsi_vco_clk", CLK_SET_RATE_PARENT,
			     pll_res->pll_base +
			     DSI_PHY_PLL_UNIPHY_PLL_POSTDIV1_CFG,
			     0, 8, 0, NULL);
	clk_register_fixed_factor(&pdev->dev, "dsi_indirect_path_div2_clk",
				  "dsi_analog_postdiv_clk", CLK_SET_RATE_PARENT,
				  1, 2);
	clk_register_divider(&pdev->dev, "dsi0pll", "dsi_vco_clk",
			     0, pll_res->pll_base +
			     DSI_PHY_PLL_UNIPHY_PLL_POSTDIV3_CFG,
			     0, 8, 0, NULL);
	clk_register_mux(&pdev->dev, "dsi_byte_mux",
			(const char *[]){
				"dsi_vco_clk",
				"dsi_indirect_path_div2_clk"
			}, 2, CLK_SET_RATE_PARENT, pll_res->pll_base +
			DSI_PHY_PLL_UNIPHY_PLL_VREG_CFG, 1, 2, 0, NULL);

	/* Force postdiv2 to be div-4 */
	writel_relaxed(3, pll_res->pll_base +
				DSI_PHY_PLL_UNIPHY_PLL_POSTDIV2_CFG);
	clk_register_fixed_factor(&pdev->dev, "dsi0pllbyte",
				  "dsi_byte_mux", CLK_SET_RATE_PARENT,
				  1, 4);

	return 0;
}
