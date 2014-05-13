/*
* Copyright (C) 2014 Texas Instruments Ltd
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License version 2 as published by
* the Free Software Foundation.
*
* You should have received a copy of the GNU General Public License along with
* this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/regmap.h>
#include <linux/of.h>

#include <video/omapdss.h>

#include "dss.h"

struct {
	struct regmap *syscon;
	struct regulator *pll_reg;

	struct pll_data *pll[2];
	unsigned scp_refcount[2];
	void __iomem *clk_ctrl[2];
} dpll;

#define DSS_PLL_CONTROL_OFF	0x538

#define CLK_CTRL_GET(id, start, end) \
	FLD_GET(clk_ctrl_read(id), start, end)

#define CLK_CTRL_FLD_MOD(id, val, start, end) \
	clk_ctrl_write(id, FLD_MOD(clk_ctrl_read(id), val, start, end))

static inline u32 clk_ctrl_read(int id)
{
	return __raw_readl(dpll.clk_ctrl[id]);
}

static inline void clk_ctrl_write(int id, u32 val)
{
	__raw_writel(val, dpll.clk_ctrl[id]);
}

struct pll_data *dss_dpll_get_pll_data(int id)
{
	return dpll.pll[id];
}

static void dss_dpll_disable_scp_clk(int id)
{
	unsigned *refcount;

	refcount = &dpll.scp_refcount[id];

	WARN_ON(*refcount == 0);
	if (--(*refcount) == 0)
		CLK_CTRL_FLD_MOD(id, 0, 14, 14); /* CIO_CLK_ICG */
}

static void dss_dpll_enable_scp_clk(int id)
{
	unsigned *refcount;

	refcount = &dpll.scp_refcount[id];

	if ((*refcount)++ == 0)
		CLK_CTRL_FLD_MOD(id, 1, 14, 14); /* CIO_CLK_ICG */
}

static int dss_dpll_power(int id, int state)
{
	/* PLL_PWR_CMD = enable both hsdiv and clkout*/
	CLK_CTRL_FLD_MOD(id, state, 31, 30);

	/*
	 * DRA7x has a bug when it comes to reading PLL_PWR_STATUS, we wait for
	 * 100 us here
	 */

	udelay(100);

	return 0;
}

static void ctrl_pll_enable(int id, bool enable)
{
	regmap_update_bits(dpll.syscon, DSS_PLL_CONTROL_OFF,
		1 << id, !enable);
}

static int dss_dpll_enable(struct pll_data *pll)
{
	int r;
	int id = pll == dpll.pll[0] ? 0 : 1;

	if (!dpll.pll_reg) {
		struct regulator *reg;

		reg = devm_regulator_get(&pll->pdev->dev, "vdda_video");

		if (IS_ERR(reg)) {
			if (PTR_ERR(reg) != -EPROBE_DEFER)
				DSSERR("can't get DPLL VDDA regulator\n");
			return PTR_ERR(reg);
		}

		dpll.pll_reg = reg;
	}

	r = dss_runtime_get();
	if (r)
		return r;

	ctrl_pll_enable(id, true);

	pll_enable_clock(pll, true);

	dss_dpll_enable_scp_clk(id);

	r = regulator_enable(dpll.pll_reg);
	if (r)
		goto err_reg;

	r = pll_wait_reset(pll);
	if (r)
		goto err_reset;

	r = dss_dpll_power(id, 0x2);
	if (r)
		goto err_power;

	return 0;

err_power:
err_reset:
	regulator_disable(dpll.pll_reg);
err_reg:
	pll_enable_clock(pll, false);
	ctrl_pll_enable(id, false);
	dss_runtime_put();

	return r;
}

static void dss_dpll_disable(struct pll_data *pll)
{
	int id = pll == dpll.pll[0] ? 0 : 1;

	dss_dpll_power(id, 0x0);
	regulator_disable(dpll.pll_reg);

	dss_dpll_disable_scp_clk(id);

	ctrl_pll_enable(id, false);
	pll_enable_clock(pll, false);
	dss_runtime_put();
}

static struct pll_ops dss_dpll_ops = {
	.enable = dss_dpll_enable,
	.disable = dss_dpll_disable,
};


void dss_dpll_set_control_mux(enum omap_channel channel, int id)
{
	u8 shift, val;

	if (channel == OMAP_DSS_CHANNEL_LCD) {
		shift = 3;

		switch (id) {
		case 0:
			val = 0; break;
		default:
			DSSERR("error in mux config for LCD\n");
			return;
		}
	} else if (channel == OMAP_DSS_CHANNEL_LCD2) {
		shift = 5;

                switch (id) {
		case 0:
			val = 0; break;
		case 1:
			val = 1; break;
		default:
			DSSERR("error in mux config for LCD2\n");
			return;
		}
	} else {
		shift = 7;

		switch (id) {
		case 0:
			val = 1; break;
		case 1:
			val = 0; break;
		default:
			DSSERR("error in mux config for LCD3\n");
			return;
		}
	}

	regmap_update_bits(dpll.syscon, DSS_PLL_CONTROL_OFF, 0x3 << shift, val);
}

int dss_dpll_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;

	dpll.syscon = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (!dpll.syscon) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return -ENODEV;
	}

	dpll.pll[0] = pll_create(pdev, "pll1", "video1_clk", 0, &dss_dpll_ops);
	if (!dpll.pll[0]) {
		dev_err(&pdev->dev, "failed to create PLL1 instance\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pll1_clkctrl");
	if (!res) {
		dev_err(&pdev->dev, "missing platform resource data\n");
		return -ENODEV;
	}

	dpll.clk_ctrl[0] = devm_ioremap_resource(&pdev->dev, res);
	if (!dpll.clk_ctrl[0]) {
		dev_err(&pdev->dev, "failed to ioremap pll1 clkctrl\n");
		return -ENOMEM;
	}

	dpll.pll[1] = pll_create(pdev, "pll2", "video2_clk", 0, &dss_dpll_ops);
	if (!dpll.pll[1]) {
		dev_err(&pdev->dev, "failed to create PLL2 instance\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pll2_clkctrl");
	if (!res) {
		dev_err(&pdev->dev, "missing platform resource data\n");
		return -ENODEV;
	}

	dpll.clk_ctrl[1] = devm_ioremap_resource(&pdev->dev, res);
	if (!dpll.clk_ctrl[1]) {
		dev_err(&pdev->dev, "failed to ioremap pll2 clkctrl\n");
		return -ENOMEM;
	}

	return 0;
}
