/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "dsi.h"
#include "dsi.xml.h"

#define dsi_phy_read(offset) msm_readl((offset))
#define dsi_phy_write(offset, data) msm_writel((data), (offset))

static int dsi_28hpm_phy_enable(struct dsi_phy *phy,
			struct dsi_phy *master_phy, u32 timing_ctrl[])
{
	int i;
	void __iomem *base = phy->base;
	void __iomem *mbase;

	DBG("");

	dsi_phy_write(base + REG_DSI_28hpm_PHY_STRENGTH_0, 0xff);

	/* Both the DSI controller have one regulator */
	if (master_phy)
		mbase = master_phy->base;
	else
		mbase = base;
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_0, 0x0);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CAL_PWR_CFG, 1);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_5, 0);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_3, 0);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_2, 0x3);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_1, 0x9);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_0, 0x7);
	dsi_phy_write(mbase + REG_DSI_28hpm_PHY_REGULATOR_CTRL_4, 0x20);

	dsi_phy_write(base + REG_DSI_28hpm_PHY_LDO_CNTRL, 0x00);

	for (i = 0; i < 12; i++)
		dsi_phy_write(base + REG_DSI_28hpm_PHY_TIMING_CTRL_0 + i * 4,
			timing_ctrl[i]);

	dsi_phy_write(base + REG_DSI_28hpm_PHY_CTRL_1, 0x00);
	dsi_phy_write(base + REG_DSI_28hpm_PHY_CTRL_0, 0x5f);

	dsi_phy_write(base + REG_DSI_28hpm_PHY_STRENGTH_1, 0x6);

	for (i = 0; i < 4; i++) {
		dsi_phy_write(base + REG_DSI_28hpm_LN_TEST_STR_0(i), 0x1);
		dsi_phy_write(base + REG_DSI_28hpm_LN_TEST_STR_1(i), 0x97);
	}
	dsi_phy_write(base + REG_DSI_28hpm_LN_CFG_4(0), 0);
	dsi_phy_write(base + REG_DSI_28hpm_LN_CFG_4(1), 0x5);
	dsi_phy_write(base + REG_DSI_28hpm_LN_CFG_4(2), 0xa);
	dsi_phy_write(base + REG_DSI_28hpm_LN_CFG_4(3), 0xf);

	dsi_phy_write(base + REG_DSI_28hpm_PHY_LNCK_CFG_1, 0xc0);
	dsi_phy_write(base + REG_DSI_28hpm_PHY_LNCK_TEST_STR0, 0x1);
	dsi_phy_write(base + REG_DSI_28hpm_PHY_LNCK_TEST_STR1, 0xbb);

	dsi_phy_write(base + REG_DSI_28hpm_PHY_CTRL_0, 0x5f);

	if (phy == master_phy)
		dsi_phy_write(base + REG_DSI_28hpm_PHY_GLBL_TEST_CTRL, 0x01);
	else
		dsi_phy_write(base + REG_DSI_28hpm_PHY_GLBL_TEST_CTRL, 0x00);

	return 0;
}

static void dsi_28hpm_phy_disable(struct dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_28hpm_PHY_CTRL_0, 0);
	dsi_phy_write(phy->base + REG_DSI_28hpm_PHY_REGULATOR_CAL_PWR_CFG, 0);

	/*
	 * Wait for the registers writes to complete in order to
	 * ensure that the phy is completely disabled
	 */
	wmb();
}

#define dsi_phy_func_init(name)	\
	do {	\
		dsi_phy->enable = dsi_##name##_phy_enable;	\
		dsi_phy->disable = dsi_##name##_phy_disable;	\
	} while (0)

int msm_dsi_phy_init(struct dsi_phy *dsi_phy, struct platform_device *pdev,
			enum msm_dsi_phy_type type)
{
	dsi_phy->base = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR_OR_NULL(dsi_phy->base)) {
		pr_err("%s: failed to map phy base\n", __func__);
		return -EINVAL;
	}

	switch (type) {
	case MSM_DSI_PHY_28HPM:
		dsi_phy_func_init(28hpm);
		break;
	default:
		pr_err("%s: unsupported type, %d\n", __func__, type);
		return -EINVAL;
	}

	return 0;
}

