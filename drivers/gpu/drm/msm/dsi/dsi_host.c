/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/iopoll.h>
#include <linux/kthread.h>

#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>

#include "dsi.h"
#include "dsi_panel.h"
#include "dsi.xml.h"

#define VDD_MIN_UV		3000000	/* uV units */
#define VDD_MAX_UV		3000000	/* uV units */
#define VDD_UA_ON_LOAD		150000	/* uA units */
#define VDD_UA_OFF_LOAD		100	/* uA units */

#define VDDA_MIN_UV		1200000	/* uV units */
#define VDDA_MAX_UV		1200000	/* uV units */
#define VDDA_UA_ON_LOAD		100000	/* uA units */
#define VDDA_UA_OFF_LOAD	100	/* uA units */

#define VDDIO_MIN_UV		1800000	/* uV units */
#define VDDIO_MAX_UV		1800000	/* uV units */
#define VDDIO_UA_ON_LOAD	100000	/* uA units */
#define VDDIO_UA_OFF_LOAD	100	/* uA units */

#define DSI_ERR_STATE_PLL_UNLOCKED		0x0001
#define DSI_ERR_STATE_MDP_FIFO_UNDERFLOW	0x0002

#define DSI_HOST_0	0
#define DSI_HOST_1	1
#define DSI_HOST_MAX	2

#define DSI_HOST_CLOCK_MASTER		DSI_HOST_0
#define DSI_HOST_CLOCK_SLAVE		DSI_HOST_1

#define MSM_DSI_VER_MAJOR_V2	0x02
#define MSM_DSI_VER_MAJOR_6G	0x03
#define MSM_DSI_6G_VER_MINOR_V1_0	0x10000000
#define MSM_DSI_6G_VER_MINOR_V1_1	0x10010000
#define MSM_DSI_6G_VER_MINOR_V1_2	0x10020000
#define MSM_DSI_6G_VER_MINOR_V1_3	0x10030000
#define MSM_DSI_6G_VER_MINOR_V1_4	0x10040000

#define DSI_6G_REG_SHIFT	4

struct dsi_config {
	u32 major;
	u32 minor;
	u32 reg_offset;
	enum msm_dsi_phy_type phy_type;
};

struct dsi_config dsi_cfgs[] = {
	{MSM_DSI_VER_MAJOR_V2, 0, 0, MSM_DSI_PHY_UNKNOWN},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_0,
		DSI_6G_REG_SHIFT, MSM_DSI_PHY_28HPM},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_1,
		DSI_6G_REG_SHIFT, MSM_DSI_PHY_28HPM},
	{MSM_DSI_VER_MAJOR_6G, MSM_DSI_6G_VER_MINOR_V1_2,
		DSI_6G_REG_SHIFT, MSM_DSI_PHY_28HPM},
};

static int dsi_get_version(const void __iomem *base, u32 *major, u32 *minor)
{
	u32 ver;
	u32 ver_6g;

	if (!major || !minor)
		return -EINVAL;

	/* From DSI6G(v3), addition of a 6G_HW_VERSION register at offset 0
	 * makes all other registers 4-byte shifted down.
	 */
	ver_6g = msm_readl(base + REG_DSI_6G_HW_VERSION);
	if (ver_6g == 0) {
		ver = msm_readl(base + REG_DSI_VERSION);
		ver = FIELD(ver, DSI_VERSION_MAJOR);
		if (ver <= MSM_DSI_VER_MAJOR_V2) {
			/* old versions */
			*major = ver;
			*minor = 0;
			return 0;
		} else {
			return -EINVAL;
		}
	} else {
		ver = msm_readl(base + DSI_6G_REG_SHIFT + REG_DSI_VERSION);
		ver = FIELD(ver, DSI_VERSION_MAJOR);
		if (ver == MSM_DSI_VER_MAJOR_6G) {
			/* 6G version */
			*major = ver;
			*minor = ver_6g;
			return 0;
		} else {
			return -EINVAL;
		}
	}
}

enum msm_dsi_host_supply {
	MSM_DSI_HOST_SUPPLY_GDSC = 0,
	MSM_DSI_HOST_SUPPLY_VDD,
	MSM_DSI_HOST_SUPPLY_VDDA,
	MSM_DSI_HOST_SUPPLY_VDDIO,
	MSM_DSI_HOST_SUPPLY_NUM
};

struct msm_dsi_host {
	struct mipi_dsi_host base;

	struct platform_device *pdev;
	struct drm_device *dev;

	int id;		/* host id */
	struct msm_dsi_panel *msm_panel;

	void __iomem *ctrl_base;
	struct regulator_bulk_data supplies[MSM_DSI_HOST_SUPPLY_NUM];
	struct clk *mdp_core_clk;
	struct clk *ahb_clk;
	struct clk *axi_clk;
	struct clk *mmss_misc_ahb_clk;
	struct clk *byte_clk;
	struct clk *esc_clk;
	struct clk *pixel_clk;
	u32 pclk_rate;
	u32 byte_clk_rate;
	u32 clk_cnt;

	struct dsi_config *cfg;
	struct dsi_phy phy;

	struct completion dma_comp;
	struct completion mdp_comp;
	struct completion video_comp;
	struct completion bta_comp;
	struct mutex dev_mutex;
	struct mutex cmd_mutex;
	struct mutex clk_mutex;
	spinlock_t intr_lock; /* Protect interrupt ctrl register */

	u32 err_work_state;
	struct work_struct err_work;
	struct workqueue_struct *workqueue;

	struct drm_gem_object *tx_gem_obj;
	u8 *rx_buf;

	bool power_on;
};

static inline u32 dsi_read(struct msm_dsi_host *msm_host, u32 reg)
{
	return msm_readl(msm_host->ctrl_base + msm_host->cfg->reg_offset + reg);
}
static inline void dsi_write(struct msm_dsi_host *msm_host, u32 reg, u32 data)
{
	msm_writel(data, msm_host->ctrl_base + msm_host->cfg->reg_offset + reg);
}

static struct dsi_config *dsi_get_config(struct msm_dsi_host *msm_host)
{
	struct dsi_config *cfg;
	int i;
	u32 major, minor;

	if (dsi_get_version(msm_host->ctrl_base, &major, &minor)) {
		pr_err("%s: Invalid version\n", __func__);
		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(dsi_cfgs); i++) {
		cfg = dsi_cfgs + i;
		if ((cfg->major == major) && (cfg->minor == minor))
			return cfg;
	}

	pr_err("%s: Version %x:%x not support\n", __func__, major, minor);
	return NULL;
}

static struct msm_dsi_host *msm_host_list[DSI_HOST_MAX];
static inline struct msm_dsi_host *get_other_host(struct msm_dsi_host *msm_host)
{
	return msm_host_list[(msm_host->id + 1) % DSI_HOST_MAX];
}

static inline struct msm_dsi_host *to_msm_dsi_host(struct mipi_dsi_host *host)
{
	return container_of(host, struct msm_dsi_host, base);
}

static void dsi_host_regulator_disable(struct msm_dsi_host *msm_host)
{
	struct regulator_bulk_data *supplies = msm_host->supplies;
	regulator_set_optimum_mode(supplies[MSM_DSI_HOST_SUPPLY_VDDIO].consumer,
					VDDIO_UA_OFF_LOAD);
	regulator_set_optimum_mode(supplies[MSM_DSI_HOST_SUPPLY_VDDIO].consumer,
					VDDA_UA_OFF_LOAD);
	regulator_set_optimum_mode(supplies[MSM_DSI_HOST_SUPPLY_VDDIO].consumer,
					VDD_UA_OFF_LOAD);
	regulator_bulk_disable(MSM_DSI_HOST_SUPPLY_NUM, supplies);
}

static int dsi_host_regulator_enable(struct msm_dsi_host *msm_host)
{
	struct regulator_bulk_data *supplies = msm_host->supplies;
	int ret;

	DBG("");
	ret = regulator_set_voltage(
			supplies[MSM_DSI_HOST_SUPPLY_VDD].consumer,
			VDD_MIN_UV, VDD_MAX_UV);
	if (ret < 0) {
		pr_err("vdd set voltage fail, %d\n", ret);
		goto vdd_set_fail;
	}
	ret = regulator_set_optimum_mode(
			supplies[MSM_DSI_HOST_SUPPLY_VDD].consumer,
			VDD_UA_ON_LOAD);
	if (ret < 0) {
		pr_err("vdd set load fail, %d\n", ret);
		goto vdd_set_fail;
	}

	ret = regulator_set_voltage(
			supplies[MSM_DSI_HOST_SUPPLY_VDDA].consumer,
			VDDA_MIN_UV, VDDA_MAX_UV);
	if (ret < 0) {
		pr_err("vdda set voltage fail, %d\n", ret);
		goto vdda_set_fail;
	}
	ret = regulator_set_optimum_mode(
			supplies[MSM_DSI_HOST_SUPPLY_VDDA].consumer,
			VDDA_UA_ON_LOAD);
	if (ret < 0) {
		pr_err("vdda set load fail, %d\n", ret);
		goto vdda_set_fail;
	}

	ret = regulator_set_voltage(
			supplies[MSM_DSI_HOST_SUPPLY_VDDIO].consumer,
			VDDIO_MIN_UV, VDDIO_MAX_UV);
	if (ret < 0) {
		pr_err("vddio set voltage fail, %d\n", ret);
		goto vddio_set_fail;
	}
	ret = regulator_set_optimum_mode(
			supplies[MSM_DSI_HOST_SUPPLY_VDDIO].consumer,
			VDDIO_UA_ON_LOAD);
	if (ret < 0) {
		pr_err("vddio set load fail, %d\n", ret);
		goto vddio_set_fail;
	}

	ret = regulator_bulk_enable(MSM_DSI_HOST_SUPPLY_NUM, supplies);
	if (ret < 0) {
		pr_err("regulator enable fail, %d\n", ret);
		goto enable_fail;
	}

	return 0;

enable_fail:
	regulator_set_optimum_mode(supplies[MSM_DSI_HOST_SUPPLY_VDDIO].consumer,
					VDDIO_UA_OFF_LOAD);
vddio_set_fail:
	regulator_set_optimum_mode(supplies[MSM_DSI_HOST_SUPPLY_VDDA].consumer,
					VDDA_UA_OFF_LOAD);
vdda_set_fail:
	regulator_set_optimum_mode(supplies[MSM_DSI_HOST_SUPPLY_VDD].consumer,
					VDD_UA_OFF_LOAD);
vdd_set_fail:
	return ret;
}

static int dsi_clk_init(struct msm_dsi_host *msm_host)
{
	struct device *dev = &msm_host->pdev->dev;
	int ret = 0;

	msm_host->mdp_core_clk = devm_clk_get(dev, "mdp_core_clk");
	if (IS_ERR(msm_host->mdp_core_clk)) {
		ret = PTR_ERR(msm_host->mdp_core_clk);
		pr_err("%s: Unable to get mdp core clk. ret=%d\n",
			__func__, ret);
		goto exit;
	}

	msm_host->ahb_clk = devm_clk_get(dev, "iface_clk");
	if (IS_ERR(msm_host->ahb_clk)) {
		ret = PTR_ERR(msm_host->ahb_clk);
		pr_err("%s: Unable to get mdss ahb clk. ret=%d\n",
			__func__, ret);
		goto exit;
	}

	msm_host->axi_clk = devm_clk_get(dev, "bus_clk");
	if (IS_ERR(msm_host->axi_clk)) {
		ret = PTR_ERR(msm_host->axi_clk);
		pr_err("%s: Unable to get axi bus clk. ret=%d\n",
			__func__, ret);
		goto exit;
	}

	if (msm_host->msm_panel &&
		(msm_host->msm_panel->mode == MSM_DSI_CMD_MODE)) {
		msm_host->mmss_misc_ahb_clk =
			devm_clk_get(dev, "core_mmss_clk");
		if (IS_ERR(msm_host->mmss_misc_ahb_clk)) {
			ret = PTR_ERR(msm_host->mmss_misc_ahb_clk);
			pr_err("%s: Unable to get mmss misc ahb clk. ret=%d\n",
				__func__, ret);
			goto exit;
		}
	}

	msm_host->byte_clk = devm_clk_get(dev, "byte_clk");
	if (IS_ERR(msm_host->byte_clk)) {
		ret = PTR_ERR(msm_host->byte_clk);
		pr_err("%s: can't find dsi_byte_clk. ret=%d\n",
			__func__, ret);
		msm_host->byte_clk = NULL;
		goto exit;
	}

	msm_host->pixel_clk = devm_clk_get(dev, "pixel_clk");
	if (IS_ERR(msm_host->pixel_clk)) {
		ret = PTR_ERR(msm_host->pixel_clk);
		pr_err("%s: can't find dsi_pixel_clk. ret=%d\n",
			__func__, ret);
		msm_host->pixel_clk = NULL;
		goto exit;
	}

	msm_host->esc_clk = devm_clk_get(dev, "core_clk");
	if (IS_ERR(msm_host->esc_clk)) {
		ret = PTR_ERR(msm_host->esc_clk);
		pr_err("%s: can't find dsi_esc_clk. ret=%d\n",
			__func__, ret);
		msm_host->esc_clk = NULL;
		goto exit;
	}

exit:
	return ret;
}

static int dsi_bus_clk_enable(struct msm_dsi_host *msm_host)
{
	int ret = 0;

	DBG("%s: id=%d\n", __func__, msm_host->id);

	ret = clk_prepare_enable(msm_host->mdp_core_clk);
	if (ret) {
		pr_err("%s: failed to enable mdp_core_clock, %d\n",
							 __func__, ret);
		goto core_clk_err;
	}

	ret = clk_prepare_enable(msm_host->ahb_clk);
	if (ret) {
		pr_err("%s: failed to enable ahb clock, %d\n", __func__, ret);
		goto ahb_clk_err;
	}

	ret = clk_prepare_enable(msm_host->axi_clk);
	if (ret) {
		pr_err("%s: failed to enable ahb clock, %d\n", __func__, ret);
		goto axi_clk_err;
	}

	if (msm_host->mmss_misc_ahb_clk) {
		ret = clk_prepare_enable(msm_host->mmss_misc_ahb_clk);
		if (ret) {
			pr_err("%s: failed to enable mmss misc ahb clk, %d\n",
				__func__, ret);
			goto misc_ahb_clk_err;
		}
	}

	return 0;

misc_ahb_clk_err:
	clk_disable_unprepare(msm_host->axi_clk);
axi_clk_err:
	clk_disable_unprepare(msm_host->ahb_clk);
ahb_clk_err:
	clk_disable_unprepare(msm_host->mdp_core_clk);
core_clk_err:
	return ret;
}

static void dsi_bus_clk_disable(struct msm_dsi_host *msm_host)
{
	DBG("");
	if (msm_host->mmss_misc_ahb_clk)
		clk_disable_unprepare(msm_host->mmss_misc_ahb_clk);
	clk_disable_unprepare(msm_host->axi_clk);
	clk_disable_unprepare(msm_host->ahb_clk);
	clk_disable_unprepare(msm_host->mdp_core_clk);
}

static int dsi_link_clk_enable(struct msm_dsi_host *msm_host)
{
	int ret = 0;
	u32 esc_clk_rate = 19200000;

	DBG("%s: Set clk rates: pclk=%d, byteclk=%d escclk=%d\n",
		__func__, msm_host->pclk_rate,
		msm_host->byte_clk_rate, esc_clk_rate);
	ret = clk_set_rate(msm_host->esc_clk, esc_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate esc clk, %d\n", __func__, ret);
		goto error;
	}

	ret =  clk_set_rate(msm_host->byte_clk, msm_host->byte_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate byte clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_set_rate(msm_host->pixel_clk, msm_host->pclk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate pixel clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_prepare_enable(msm_host->esc_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi esc clk\n", __func__);
		goto error;
	}

	ret = clk_prepare_enable(msm_host->byte_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi byte clk\n", __func__);
		goto byte_clk_err;
	}

	ret = clk_prepare_enable(msm_host->pixel_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi pixel clk\n", __func__);
		goto pixel_clk_err;
	}

	return ret;

pixel_clk_err:
	clk_disable_unprepare(msm_host->byte_clk);
byte_clk_err:
	clk_disable_unprepare(msm_host->esc_clk);
error:
	return ret;
}

static void dsi_link_clk_disable(struct msm_dsi_host *msm_host)
{
	clk_disable_unprepare(msm_host->esc_clk);
	clk_disable_unprepare(msm_host->pixel_clk);
	clk_disable_unprepare(msm_host->byte_clk);
}

static int dsi_clk_ctrl_sub(struct msm_dsi_host *msm_host, bool enable)
{
	int changed = 0;
	int ret;

	mutex_lock(&msm_host->clk_mutex);
	if (enable) {
		if (msm_host->clk_cnt == 0)
			changed++;
		msm_host->clk_cnt++;
	} else {
		if (msm_host->clk_cnt) {
			msm_host->clk_cnt--;
			if (msm_host->clk_cnt == 0)
				changed++;
		} else {
			pr_err("%s: Can not be turned off\n", __func__);
			ret = -EINVAL;
			goto unlock_ret;
		}
	}

	DBG("id=%d clk_cnt=%d changed=%d enable=%d\n",
		msm_host->id, msm_host->clk_cnt, changed, enable);
	if (changed) {
		if (enable) {
			ret = dsi_bus_clk_enable(msm_host);
			if (ret) {
				pr_err("%s: Can not enable bus clk, %d\n",
					__func__, ret);
				goto unlock_ret;
			}
			ret = dsi_link_clk_enable(msm_host);
			if (ret) {
				pr_err("%s: Can not enable link clk, %d\n",
					__func__, ret);
				dsi_bus_clk_disable(msm_host);
				goto unlock_ret;
			}
		} else {
			dsi_link_clk_disable(msm_host);
			dsi_bus_clk_disable(msm_host);
		}
	}

unlock_ret:
	mutex_unlock(&msm_host->clk_mutex);
	return 0;
}

static int dsi_clk_ctrl(struct msm_dsi_host *msm_host, bool enable)
{
	struct msm_dsi_host *mhost = NULL;
	int ret;

	DBG("%s: id=%d enable=%d\n",
		__func__, msm_host->id, enable);
	if (msm_host->msm_panel->desc.broadcast_enabled &&
		(msm_host->id == DSI_HOST_CLOCK_SLAVE))
		mhost = msm_host_list[DSI_HOST_CLOCK_MASTER];

	if (enable && mhost) {
		ret = dsi_clk_ctrl_sub(mhost, enable);
		if (ret)
			return ret;
	}

	ret = dsi_clk_ctrl_sub(msm_host, enable);
	if (ret) {
		if (enable && mhost)
			dsi_clk_ctrl_sub(mhost, !enable);
		return ret;
	}

	if (!enable && mhost) {
		ret = dsi_clk_ctrl_sub(mhost, enable);
		if (ret) {
			dsi_clk_ctrl_sub(msm_host, !enable);
			return ret;
		}
	}

	return 0;
}

static int dsi_calc_clk_rate(struct msm_dsi_host *msm_host,
				unsigned long pclk_rate, int frame_rate)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct videomode *vm = &desc->vm;
	u8 lanes = desc->lane_map.lane_num;
	u8 bpp = desc->bpp;
	u32 h_period, v_period;

	if ((frame_rate != desc->frame_rate) || !pclk_rate) {
		h_period = vm->hactive + vm->hback_porch + vm->hfront_porch +
								vm->hsync_len;
		v_period = vm->vactive + vm->vback_porch + vm->vfront_porch +
								vm->vsync_len;
		pclk_rate = h_period * v_period * frame_rate;
	}

	msm_host->pclk_rate = pclk_rate;

	if (lanes > 0) {
		msm_host->byte_clk_rate = (pclk_rate * bpp) / (8 * lanes);
	} else {
		pr_err("%s: forcing mdss_dsi lanes to 1\n", __func__);
		msm_host->byte_clk_rate = (pclk_rate * bpp) / 8;
	}

	DBG("%s: pclk=%d, bclk=%d\n", __func__,
			msm_host->pclk_rate, msm_host->byte_clk_rate);

	return 0;
}

static void dsi_phy_sw_reset(struct msm_dsi_host *msm_host)
{
	DBG("");
	dsi_write(msm_host, REG_DSI_PHY_RESET, DSI_PHY_RESET_RESET);
	/* Make sure fully reset */
	wmb();
	udelay(1000);
	dsi_write(msm_host, REG_DSI_PHY_RESET, 0);
	udelay(100);
}

static void dsi_intr_ctrl(struct msm_dsi_host *msm_host, u32 mask, int enable)
{
	u32 intr;
	unsigned long flags;

	spin_lock_irqsave(&msm_host->intr_lock, flags);
	intr = dsi_read(msm_host, REG_DSI_INTR_CTRL);

	if (enable)
		intr |= mask;
	else
		intr &= ~mask;

	DBG("intr=%x enable=%d", intr, enable);

	dsi_write(msm_host, REG_DSI_INTR_CTRL, intr); /* DSI_INTL_CTRL */
	spin_unlock_irqrestore(&msm_host->intr_lock, flags);
}

static void dsi_ctrl_init(struct msm_dsi_host *msm_host)
{
	u32 data = 0;
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct msm_dsi_panel_video_cfg *vid_cfg = &desc->vid_cfg;
	struct msm_dsi_panel_cmd_cfg *cmd_cfg = &desc->cmd_cfg;
	struct msm_dsi_panel_lane_map *lane_map = &desc->lane_map;
	struct msm_dsi_panel_color_map *color_map = &desc->color_map;

	if (msm_panel->mode == MSM_DSI_VIDEO_MODE) {
		if (vid_cfg->pulse_mode_hsa_he)
			data |= DSI_VID_CFG0_PULSE_MODE_HSA_HE;
		if (vid_cfg->hfp_power_stop)
			data |= DSI_VID_CFG0_HFP_POWER_STOP;
		if (vid_cfg->hbp_power_stop)
			data |= DSI_VID_CFG0_HBP_POWER_STOP;
		if (vid_cfg->hsa_power_stop)
			data |= DSI_VID_CFG0_HSA_POWER_STOP;
		if (vid_cfg->eof_bllp_power_stop)
			data |= DSI_VID_CFG0_EOF_BLLP_POWER_STOP;
		if (vid_cfg->bllp_power_stop)
			data |= DSI_VID_CFG0_BLLP_POWER_STOP;
		data |= DSI_VID_CFG0_TRAFFIC_MODE(vid_cfg->traffic_mode);
		data |= DSI_VID_CFG0_DST_FORMAT(vid_cfg->dst_format);
		data |= DSI_VID_CFG0_VIRT_CHANNEL(desc->vc);
		dsi_write(msm_host, REG_DSI_VID_CFG0, data);

		data = DSI_VID_CFG1_RGB_SWAP(color_map->rgb_swap);
		if (color_map->b_swap)
			data |= DSI_VID_CFG1_B_SEL;
		if (color_map->g_swap)
			data |= DSI_VID_CFG1_G_SEL;
		if (color_map->r_swap)
			data |= DSI_VID_CFG1_R_SEL;
		dsi_write(msm_host, REG_DSI_VID_CFG1, data);
	} else if (msm_panel->mode == MSM_DSI_CMD_MODE) {
		data = DSI_CMD_CFG0_RGB_SWAP(color_map->rgb_swap);
		if (color_map->b_swap)
			data |= DSI_CMD_CFG0_B_SEL;
		if (color_map->g_swap)
			data |= DSI_CMD_CFG0_G_SEL;
		if (color_map->r_swap)
			data |= DSI_CMD_CFG0_R_SEL;
		data |= DSI_CMD_CFG0_DST_FORMAT(cmd_cfg->dst_format);
		dsi_write(msm_host, REG_DSI_CMD_CFG0, data);

		data = DSI_CMD_CFG1_WR_MEM_START(MIPI_DCS_WRITE_MEMORY_START) |
			DSI_CMD_CFG1_WR_MEM_CONTINUE(
					MIPI_DCS_WRITE_MEMORY_CONTINUE);
		if (cmd_cfg->insert_dcs_cmd)
			data |= DSI_CMD_CFG1_INSERT_DCS_COMMAND;
		dsi_write(msm_host, REG_DSI_CMD_CFG1, data);
	} else
		pr_err("%s: Unknown DSI mode=%d\n", __func__, msm_panel->mode);

	data = DSI_CMD_DMA_CTRL_FROM_FRAME_BUFFER | DSI_CMD_DMA_CTRL_LOW_POWER;
	if (desc->broadcast_enabled)
		dsi_write(msm_host, REG_DSI_CMD_DMA_CTRL,
				data | DSI_CMD_DMA_CTRL_BROADCAST_EN);
	else
		dsi_write(msm_host, REG_DSI_CMD_DMA_CTRL, data);

	data = 0;
	if (cmd_cfg->te_sel)
		data |= DSI_TRIG_CTRL_TE;
	data |= DSI_TRIG_CTRL_MDP_TRIGGER(cmd_cfg->mdp_trigger);
	data |= DSI_TRIG_CTRL_DMA_TRIGGER(cmd_cfg->dma_trigger);
	data |= DSI_TRIG_CTRL_STREAM(desc->vc);
	if ((msm_host->cfg->major == MSM_DSI_VER_MAJOR_6G) &&
		(msm_host->cfg->minor >= MSM_DSI_6G_VER_MINOR_V1_2))
		data |= DSI_TRIG_CTRL_BLOCK_DMA_WITHIN_FRAME;
	dsi_write(msm_host, REG_DSI_TRIG_CTRL, data);

	dsi_write(msm_host, REG_DSI_LANE_SWAP_CTRL, lane_map->lane_swap);

	data = DSI_CLKOUT_TIMING_CTRL_T_CLK_POST(desc->t_clk_post) |
		DSI_CLKOUT_TIMING_CTRL_T_CLK_PRE(desc->t_clk_pre);
	dsi_write(msm_host, REG_DSI_CLKOUT_TIMING_CTRL, data);

	data = 0;
	if (desc->rx_eot_ignore)
		data |= DSI_EOT_PACKET_CTRL_RX_EOT_IGNORE;
	if (desc->tx_eot_append)
		data |= DSI_EOT_PACKET_CTRL_TX_EOT_APPEND;
	dsi_write(msm_host, REG_DSI_EOT_PACKET_CTRL, data);

	/* allow only ack-err-status to generate interrupt */
	dsi_write(msm_host, REG_DSI_ERR_INT_MASK0, 0x13ff3fe0);

	data = (DSI_IRQ_MASK_CMD_DMA_DONE | DSI_IRQ_MASK_CMD_MDP_DONE |
			DSI_IRQ_MASK_ERROR);
	dsi_intr_ctrl(msm_host, data, 1);

	dsi_write(msm_host, REG_DSI_CLK_CTRL,
		DSI_CLK_CTRL_AHBS_HCLK_ON | DSI_CLK_CTRL_AHBM_SCLK_ON |
		DSI_CLK_CTRL_PCLK_ON | DSI_CLK_CTRL_DSICLK_ON |
		DSI_CLK_CTRL_BYTECLK_ON | DSI_CLK_CTRL_ESCCLK_ON |
		DSI_CLK_CTRL_FORCE_ON_DYN_AHBM_HCLK);

	data = DSI_CTRL_CLK_EN | DSI_CTRL_CMD_MODE_EN;

	if (lane_map->lane3_enabled)
		data |= DSI_CTRL_LANE3;
	if (lane_map->lane2_enabled)
		data |= DSI_CTRL_LANE2;
	if (lane_map->lane1_enabled)
		data |= DSI_CTRL_LANE1;
	if (lane_map->lane0_enabled)
		data |= DSI_CTRL_LANE0;

	data |= DSI_CTRL_ENABLE;

	dsi_write(msm_host, REG_DSI_CTRL, data);
}

static void dsi_ctrl_setup(struct msm_dsi_host *msm_host)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct videomode *vm = &desc->vm;
	u32 hs_start = 0, vs_start = 0;
	u32 hs_end = vm->hsync_len;
	u32 vs_end = vm->vsync_len;
	u32 ha_start = hs_end + vm->hback_porch;
	u32 ha_end = ha_start + vm->hactive;
	u32 va_start = vs_end + vm->vback_porch;
	u32 va_end = va_start + vm->vactive;
	u32 h_total = ha_end + vm->hfront_porch;
	u32 v_total = va_end + vm->vfront_porch;
	u32 wc;

	DBG("");

	if (msm_panel->mode == MSM_DSI_VIDEO_MODE) {
		dsi_write(msm_host, REG_DSI_ACTIVE_H,
			DSI_ACTIVE_H_START(ha_start) |
			DSI_ACTIVE_H_END(ha_end));
		dsi_write(msm_host, REG_DSI_ACTIVE_V,
			DSI_ACTIVE_V_START(va_start) |
			DSI_ACTIVE_V_END(va_end));
		dsi_write(msm_host, REG_DSI_TOTAL,
			DSI_TOTAL_H_TOTAL(h_total - 1) |
			DSI_TOTAL_V_TOTAL(v_total - 1));

		dsi_write(msm_host, REG_DSI_ACTIVE_HSYNC,
			DSI_ACTIVE_HSYNC_START(hs_start) |
			DSI_ACTIVE_HSYNC_END(hs_end));
		dsi_write(msm_host, REG_DSI_ACTIVE_VSYNC_HPOS, 0);
		dsi_write(msm_host, REG_DSI_ACTIVE_VSYNC_VPOS,
			DSI_ACTIVE_VSYNC_VPOS_START(vs_start) |
			DSI_ACTIVE_VSYNC_VPOS_END(vs_end));
	} else {		/* command mode */
		/* image data and 1 byte write_memory_start cmd */
		wc = vm->hactive * desc->bpp / 8 + 1;

		dsi_write(msm_host, REG_DSI_CMD_MDP_STREAM_CTRL,
			DSI_CMD_MDP_STREAM_CTRL_WORD_COUNT(wc) |
			DSI_CMD_MDP_STREAM_CTRL_VIRTUAL_CHANNEL(desc->vc) |
			DSI_CMD_MDP_STREAM_CTRL_DATA_TYPE(
					MIPI_DSI_DCS_LONG_WRITE));

		dsi_write(msm_host, REG_DSI_CMD_MDP_STREAM_TOTAL,
			DSI_CMD_MDP_STREAM_TOTAL_H_TOTAL(vm->hactive) |
			DSI_CMD_MDP_STREAM_TOTAL_V_TOTAL(vm->vactive));
	}
}

static void dsi_sw_reset(struct msm_dsi_host *msm_host)
{
	dsi_write(msm_host, REG_DSI_CLK_CTRL,
		DSI_CLK_CTRL_AHBS_HCLK_ON | DSI_CLK_CTRL_AHBM_SCLK_ON |
		DSI_CLK_CTRL_PCLK_ON | DSI_CLK_CTRL_DSICLK_ON |
		DSI_CLK_CTRL_BYTECLK_ON | DSI_CLK_CTRL_ESCCLK_ON |
		DSI_CLK_CTRL_FORCE_ON_DYN_AHBM_HCLK);
	wmb(); /* clocks need to be enabled before reset */

	dsi_write(msm_host, REG_DSI_RESET, 1);
	wmb(); /* make sure reset happen */
	dsi_write(msm_host, REG_DSI_RESET, 0);
}

static void dsi_check_idle(struct msm_dsi_host *msm_host, int enable)
{
	u32 dsi_ctrl;
	u32 status;
	u32 sleep_us = 1000;
	u32 timeout_us = 16000;
	u8 *base = msm_host->ctrl_base + msm_host->cfg->reg_offset;

	/* Check for CMD_MODE_DMA_BUSY */
	if (readl_poll_timeout(base + REG_DSI_STATUS0,
			status,
			((status & DSI_STATUS0_CMD_MODE_DMA_BUSY) == 0),
			sleep_us, timeout_us))
		DBG("%s: DSI status=%x failed\n", __func__, status);

	/* Check for x_HS_FIFO_EMPTY */
	if (readl_poll_timeout(base + REG_DSI_FIFO_STATUS,
			status,
			((status & 0x11111000) == 0x11111000),
			sleep_us, timeout_us))
		DBG("%s: FIFO status=%x failed\n", __func__, status);

	/* Check for VIDEO_MODE_ENGINE_BUSY */
	if (readl_poll_timeout(base + REG_DSI_STATUS0,
			status,
			((status & DSI_STATUS0_VIDEO_MODE_ENGINE_BUSY) == 0),
			sleep_us, timeout_us)) {
		DBG("%s: DSI status=%x\n", __func__, status);
		DBG("%s: Doing sw reset\n", __func__);
		dsi_sw_reset(msm_host);
	}

	dsi_ctrl = dsi_read(msm_host, REG_DSI_CTRL);
	if (enable)
		dsi_ctrl |= DSI_CTRL_ENABLE;
	else
		dsi_ctrl &= ~DSI_CTRL_ENABLE;

	dsi_write(msm_host, REG_DSI_CTRL, dsi_ctrl);
}

static void dsi_op_mode_config(struct msm_dsi_host *msm_host,
					enum msm_dsi_mode mode)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	u32 dsi_ctrl, intr_ctrl;

	if (desc->broadcast_enabled && (msm_panel->id != desc->trigger_id)) {
		DBG("%s: Broadcast mode. non-trigger panel\n",
			 __func__);
		return;
	}

	dsi_ctrl = dsi_read(msm_host, REG_DSI_CTRL);
	/* Keep Video mode ON if already enabled */
	dsi_ctrl &= ~DSI_CTRL_CMD_MODE_EN;

	if (mode == MSM_DSI_VIDEO_MODE) {
		dsi_ctrl |= DSI_CTRL_VID_MODE_EN;
		intr_ctrl = DSI_IRQ_MASK_CMD_DMA_DONE | DSI_IRQ_MASK_BTA_DONE;
	} else {		/* command mode */
		dsi_ctrl |= DSI_CTRL_CMD_MODE_EN;

		/* if (msm_panel->mode == MSM_DSI_VIDEO_MODE)
			dsi_ctrl |= 0x02; */

		intr_ctrl = DSI_IRQ_MASK_CMD_DMA_DONE | DSI_IRQ_MASK_ERROR |
			DSI_IRQ_CMD_MDP_DONE | DSI_IRQ_MASK_BTA_DONE;
	}
	dsi_ctrl |= DSI_CTRL_ENABLE;

	if (desc->broadcast_enabled && (msm_panel->id == desc->trigger_id)
		&& other_host) {
		dsi_intr_ctrl(other_host, intr_ctrl, 1);
		dsi_write(other_host, REG_DSI_CTRL, dsi_ctrl);
	}

	dsi_intr_ctrl(msm_host, intr_ctrl, 1);
	dsi_write(msm_host, REG_DSI_CTRL, dsi_ctrl);
}

static void dsi_set_tx_power_mode(int mode, struct msm_dsi_host *msm_host)
{
	u32 data;

	data = dsi_read(msm_host, REG_DSI_CMD_DMA_CTRL);

	if (mode == 0)
		data &= ~DSI_CMD_DMA_CTRL_LOW_POWER;
	else
		data |= DSI_CMD_DMA_CTRL_LOW_POWER;

	dsi_write(msm_host, REG_DSI_CMD_DMA_CTRL, data);
}

static void dsi_wait4video_done(struct msm_dsi_host *msm_host)
{
	/* DSI_INTL_CTRL */
	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_VIDEO_DONE, 1);

	reinit_completion(&msm_host->video_comp);

	wait_for_completion_timeout(&msm_host->video_comp,
			msecs_to_jiffies(70));

	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_VIDEO_DONE, 0);
}

static void dsi_wait4video_eng_busy(struct msm_dsi_host *msm_host)
{
	if (msm_host->msm_panel->mode == MSM_DSI_CMD_MODE)
		return;

	if (msm_host->power_on) {
		dsi_wait4video_done(msm_host);
		/* delay 4 ms to BLLP */
		dsi_sleep(4000);
	}
}

/* dsi_cmd */
static int dsi_tx_buf_alloc(struct msm_dsi_host *msm_host, int size)
{
	struct drm_device *dev = msm_host->dev;
	int ret;
	u32 iova;

	mutex_lock(&dev->struct_mutex);
	msm_host->tx_gem_obj = msm_gem_new(dev, size, MSM_BO_WC);
	if (IS_ERR(msm_host->tx_gem_obj)) {
		ret = PTR_ERR(msm_host->tx_gem_obj);
		pr_err("%s: failed to allocate gem, %d\n", __func__, ret);
		msm_host->tx_gem_obj = NULL;
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	ret = msm_gem_get_iova_locked(msm_host->tx_gem_obj, 0, &iova);
	if (ret) {
		pr_err("%s: failed to get iova, %d\n", __func__, ret);
		return ret;
	}
	mutex_unlock(&dev->struct_mutex);

	if (iova & 0x07) {
		pr_err("%s: buf NOT 8 bytes aligned\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static void dsi_tx_buf_free(struct msm_dsi_host *msm_host)
{
	struct drm_device *dev = msm_host->dev;

	if (msm_host->tx_gem_obj) {
		msm_gem_put_iova(msm_host->tx_gem_obj, 0);
		mutex_lock(&dev->struct_mutex);
		msm_gem_free_object(msm_host->tx_gem_obj);
		mutex_unlock(&dev->struct_mutex);
		msm_host->tx_gem_obj = NULL;
	}
}

/*
 * prepare cmd buffer to be txed
 */
int dsi_cmd_dma_add(struct drm_gem_object *tx_gem, struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int i, len;
	int ret = 0;
	u8 *data;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		pr_err("%s: create packet failed, %d\n", __func__, ret);
		return ret;
	}
	len = (packet.size + 3) & (~0x3);

	if (len > tx_gem->size) {
		pr_err("%s: packet size is too big\n", __func__);
		return -EINVAL;
	}

	data = msm_gem_vaddr(tx_gem);

	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		pr_err("%s: get vaddr failed, %d\n", __func__, ret);
		return ret;
	}

	/* MSM specific command format in memory */
	data[0] = packet.header[1];
	data[1] = packet.header[2];
	data[2] = packet.header[0];
	data[3] = BIT(7); /* Last packet */
	if (mipi_dsi_packet_format_is_long(msg->type))
		data[3] |= BIT(6);
	if (msg->rx_buf && msg->rx_len)
		data[3] |= BIT(5);

	/* Long packet */
	for (i = 0; i < packet.payload_length; i++)
		data[i + 4] = packet.payload[i];

	/* Append 0xff to the end */
	for (i = packet.size; i < len; i++)
		data[i] = 0xff;

	return len;
}

/*
 * dsi_short_read1_resp: 1 parameter
 */
int dsi_short_read1_resp(u8 *buf, struct mipi_dsi_msg *msg)
{
	u8 *data = msg->rx_buf;
	if (data && (msg->rx_len >= 1)) {
		*data = buf[1]; /* strip out dcs type */
		return 1;
	} else {
		pr_err("%s: read data does not match with rx_buf len %d\n",
			__func__, msg->rx_len);
		return -EINVAL;
	}
}

/*
 * dsi_short_read2_resp: 2 parameter
 */
int dsi_short_read2_resp(u8 *buf, struct mipi_dsi_msg *msg)
{
	u8 *data = msg->rx_buf;
	if (data && (msg->rx_len >= 2)) {
		data[0] = buf[1]; /* strip out dcs type */
		data[1] = buf[2];
		return 2;
	} else {
		pr_err("%s: read data does not match with rx_buf len %d\n",
			__func__, msg->rx_len);
		return -EINVAL;
	}
}

int dsi_long_read_resp(u8 *buf, struct mipi_dsi_msg *msg)
{
	/* strip out 4 byte dcs header */
	if (msg->rx_buf && msg->rx_len)
		memcpy(msg->rx_buf, buf + 4, msg->rx_len);

	return msg->rx_len;
}


static int dsi_cmd_dma_tx(struct msm_dsi_host *msm_host, int len)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	int ret = 0;
	u32 iova;

	ret = msm_gem_get_iova(msm_host->tx_gem_obj, 0, &iova);
	if (ret) {
		pr_err("%s: failed to get iova: %d\n", __func__, ret);
		return ret;
	}

	reinit_completion(&msm_host->dma_comp);

	if (desc->broadcast_enabled && (msm_panel->id == desc->trigger_id)
		&& other_host) {
		dsi_write(other_host, REG_DSI_DMA_BASE, iova);
		dsi_write(other_host, REG_DSI_DMA_LEN, len);
	}

	dsi_write(msm_host, REG_DSI_DMA_BASE, iova);
	dsi_write(msm_host, REG_DSI_DMA_LEN, len);
	/* Make sure addr and len are set before trigger */
	wmb();

	if (desc->broadcast_enabled && (msm_panel->id == desc->trigger_id)
		&& other_host)
		dsi_write(other_host, REG_DSI_TRIG_DMA, 1);

	dsi_write(msm_host, REG_DSI_TRIG_DMA, 1);
	/* Make sure trigger happen before wait */
	wmb();

	ret = wait_for_completion_timeout(&msm_host->dma_comp,
				msecs_to_jiffies(200));
	DBG("ret=%d\n", ret);
	if (ret == 0)
		ret = -ETIMEDOUT;
	else
		ret = len;

	return ret;
}

static int dsi_cmd_dma_rx(struct msm_dsi_host *msm_host,
			u8 *buf, int rx_byte, int pkt_size)

{
	u32 *lp, *temp, data;
	int i, j = 0, cnt;
	bool ack_error = false;
	u32 read_cnt;
	u8 reg[16];
	int repeated_bytes = 0;
	int buf_offset = buf - msm_host->rx_buf;

	lp = (u32 *)buf;
	temp = (u32 *)reg;
	cnt = (rx_byte + 3) >> 2;
	if (cnt > 4)
		cnt = 4; /* 4 x 32 bits registers only */

	/* Calculate real read data count */
	read_cnt = dsi_read(msm_host, 0x1d4) >> 16;

	ack_error = (rx_byte == 4) ?
		(read_cnt == 8) : /* short pkt + 4-byte error pkt */
		(read_cnt == (pkt_size + 6 + 4)); /* long pkt+4-byte error pkt*/

	if (ack_error)
		read_cnt -= 4; /* Remove 4 byte error pkt */

	/*
	 * In case of multiple reads from the panel, after the first read, there
	 * is possibility that there are some bytes in the payload repeating in
	 * the RDBK_DATA registers. Since we read all the parameters from the
	 * panel right from the first byte for every pass. We need to skip the
	 * repeating bytes and then append the new parameters to the rx buffer.
	 */
	if (read_cnt > 16) {
		int bytes_shifted;
		/* Any data more than 16 bytes will be shifted out.
		 * The temp read buffer should already contain these bytes.
		 * The remaining bytes in read buffer are the repeated bytes.
		 */
		bytes_shifted = read_cnt - 16;
		repeated_bytes = buf_offset - bytes_shifted;
	}

	for (i = cnt - 1; i >= 0; i--) {
		data = dsi_read(msm_host, REG_DSI_RDBK_DATA(i));
		*temp++ = ntohl(data); /* to host byte order */
		DBG("data = 0x%x and ntohl(data) = 0x%x\n", data, ntohl(data));
	}

	for (i = repeated_bytes; i < 16; i++)
		buf[j++] = reg[i];

	return j;
}


static int dsi_cmds2buf_tx(struct msm_dsi_host *msm_host,
			struct mipi_dsi_msg *msg)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	int len, ret;

	len = dsi_cmd_dma_add(msm_host->tx_gem_obj, msg);
	if (!len) {
		pr_err("%s: failed to add cmd type = 0x%x\n",
			__func__,  msg->type);
		return -EINVAL;
	}

	/* for video mode, do not send cmds more than
	* one pixel line, since it only transmit it
	* during BLLP.
	*/
	if ((msm_panel->mode == MSM_DSI_VIDEO_MODE) &&
		(len > (desc->vm.hactive * desc->bpp / 8))) {
		/* TODO: If there is a such LONG command,
		 * we need to handle this case properly.
		 */
		pr_err("%s: cmd cannot fit into BLLP period, len=%d\n",
			__func__, len);
		return -EINVAL;
	}

	dsi_wait4video_eng_busy(msm_host);

	ret = dsi_cmd_dma_tx(msm_host, len);
	if (ret < len) {
		pr_err("%s: cmd dma tx failed, type=0x%x, %d\n",
			__func__, msg->type, len);
		return -ECOMM;
	}

	return len;
}

static int dsi_cmds_tx(struct msm_dsi_host *msm_host, struct mipi_dsi_msg *msg)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	u32 dsi_ctrl, data;
	int video_mode, ret = 0;
	u32 other_ctrl = 0;
	bool left_host_restore = false;

	if (!msg->tx_buf || !msg->tx_len)
		return 0;

	if (desc->broadcast_enabled) {
		if (msm_panel->id != desc->trigger_id) {
			DBG("%s: Broadcast mode. non-trigger panel\n",
				 __func__);
			return msg->tx_len;
		} else if (other_host != NULL) {
			other_ctrl = dsi_read(other_host, REG_DSI_CTRL);
			video_mode = other_ctrl & DSI_CTRL_VID_MODE_EN;
			if (video_mode) {
				data = other_ctrl | DSI_CTRL_CMD_MODE_EN;
				dsi_write(other_host, REG_DSI_CTRL, data);
				left_host_restore = true;
			}
		}
	}

	/* turn on cmd mode */
	dsi_ctrl = dsi_read(msm_host, REG_DSI_CTRL);
	video_mode = dsi_ctrl & DSI_CTRL_VID_MODE_EN;
	if (video_mode) {
		data = dsi_ctrl | DSI_CTRL_CMD_MODE_EN;
		dsi_write(msm_host, REG_DSI_CTRL, data);
	}

	ret = dsi_cmds2buf_tx(msm_host, msg);

	if (left_host_restore)
		dsi_write(other_host, REG_DSI_CTRL, other_ctrl); /*restore */

	if (video_mode)
		dsi_write(msm_host, REG_DSI_CTRL, dsi_ctrl); /* restore */

	return ret;
}

/*
 * dsi_cmds_rx() - dcs read from panel
 * @ctrl: dsi controller
 * @cmds: read command descriptor
 * @len: number of bytes to read back
 *
 * controller have 4 registers can hold 16 bytes of rxed data
 * dcs packet: 4 bytes header + payload + 2 bytes crc
 * 1st read: 4 bytes header + 10 bytes payload + 2 crc
 * 2nd read: 14 bytes payload + 2 crc
 * 3rd read: 14 bytes payload + 2 crc
 *
 */
static int dsi_cmds_rx(struct msm_dsi_host *msm_host, struct mipi_dsi_msg *msg)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	int data_byte, rx_byte, dlen, end;
	int short_response, diff, pkt_size, ret = 0;
	char cmd;
	u32 dsi_ctrl, data;
	int video_mode;
	u32 other_ctrl = 0;
	bool other_host_restore = false;
	int rlen = msg->rx_len;
	u8 *buf;

	if (!msg->rx_buf || !msg->rx_len || !msg->tx_buf || !msg->tx_len)
		return 0;

	if (desc->broadcast_enabled) {
		if (msm_panel->id != desc->trigger_id) {
			DBG("Broadcast mode. non-trigger ctrl");
			return msg->rx_len;
		} else if (other_host != NULL) {
			other_ctrl = dsi_read(other_host, REG_DSI_CTRL);
			video_mode = other_ctrl & DSI_CTRL_VID_MODE_EN;
			if (video_mode) {
				data = other_ctrl | DSI_CTRL_CMD_MODE_EN;
				dsi_write(other_host, REG_DSI_CTRL, data);
				other_host_restore = true;
			}
		}
	}

	/* turn on cmd mode
	* for video mode, do not send cmds more than
	* one pixel line, since it only transmit it
	* during BLLP.
	*/
	dsi_ctrl = dsi_read(msm_host, REG_DSI_CTRL);
	video_mode = dsi_ctrl & DSI_CTRL_VID_MODE_EN;
	if (video_mode) {
		data = dsi_ctrl | DSI_CTRL_CMD_MODE_EN;
		dsi_write(msm_host, REG_DSI_CTRL, data);
	}

	if (rlen <= 2) {
		short_response = 1;
		pkt_size = rlen;
		rx_byte = 4;
	} else {
		short_response = 0;
		data_byte = 10;	/* first read */
		if (rlen < data_byte)
			pkt_size = rlen;
		else
			pkt_size = data_byte;
		rx_byte = data_byte + 6; /* 4 header + 2 crc */
	}

	buf = msm_host->rx_buf;
	end = 0;
	while (!end) {
		u8 tx[2] = {pkt_size & 0xff, pkt_size >> 8};
		struct mipi_dsi_msg max_pkt_size_msg = {
			.channel = msg->channel,
			.type = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
			.tx_len = 2,
			.tx_buf = tx,
		};

		DBG("rlen=%d pkt_size=%d rx_byte=%d",
			rlen, pkt_size, rx_byte);

		ret = dsi_cmds2buf_tx(msm_host, &max_pkt_size_msg);
		if (ret < 2) {
			pr_err("%s: Set max pkt size failed, %d\n",
				__func__, ret);
			return -EINVAL;
		}

		if ((msm_host->cfg->major == MSM_DSI_VER_MAJOR_6G) &&
			(msm_host->cfg->minor >= MSM_DSI_6G_VER_MINOR_V1_1)) {
			/* Clear the RDBK_DATA registers */
			dsi_write(msm_host, REG_DSI_RDBK_DATA_CTRL,
					DSI_RDBK_DATA_CTRL_CLR);
			wmb(); /* make sure the RDBK registers are cleared */
			dsi_write(msm_host, REG_DSI_RDBK_DATA_CTRL, 0);
			wmb(); /* release cleared status before transfer */
		}

		ret = dsi_cmds2buf_tx(msm_host, msg);
		if (ret < msg->tx_len) {
			pr_err("%s: Read cmd Tx failed, %d\n", __func__, ret);
			return ret;
		}

		/*
		 * once cmd_dma_done interrupt received,
		 * return data from client is ready and stored
		 * at RDBK_DATA register already
		 * since rx fifo is 16 bytes, dcs header is kept at first loop,
		 * after that dcs header lost during shift into registers
		 */
		dlen = dsi_cmd_dma_rx(msm_host, buf, rx_byte, pkt_size);

		if (dlen <= 0) {
			ret = dlen;
			goto end;
		}

		if (short_response)
			break;

		if (rlen <= data_byte) {
			diff = data_byte - rlen;
			end = 1;
		} else {
			diff = 0;
			rlen -= data_byte;
		}

		if (!end) {
			dlen -= 2; /* 2 crc */
			dlen -= diff;
			buf += dlen;	/* next start position */
			data_byte = 14;	/* NOT first read */
			if (rlen < data_byte)
				pkt_size += rlen;
			else
				pkt_size += data_byte;
			DBG("buf=%x dlen=%d diff=%d\n", (int)buf, dlen, diff);
		}
	}

	/*
	 * For single Long read, if the requested rlen < 10,
	 * we need to shift the start position of rx
	 * data buffer to skip the bytes which are not
	 * updated.
	 */
	if (pkt_size < 10 && !short_response)
		buf = msm_host->rx_buf + (10 - rlen);
	else
		buf = msm_host->rx_buf;

	cmd = buf[0];
	switch (cmd) {
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		DBG("%s: rx ACK_ERR_PACLAGE\n", __func__);
		ret = 0;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		ret = dsi_short_read1_resp(buf, msg);
		break;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		ret = dsi_short_read2_resp(buf, msg);
		break;
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		ret = dsi_long_read_resp(buf, msg);
		break;
	default:
		pr_warn("%s:Invalid response cmd\n", __func__);
		ret = 0;
	}
end:
	if (other_host_restore)
		dsi_write(other_host, REG_DSI_CTRL, other_ctrl);
	if (video_mode)
		dsi_write(msm_host, REG_DSI_CTRL, dsi_ctrl);

	return ret;
}

static void dsi_pll_relock(struct msm_dsi_host *msm_host)
{
	int i, cnt;

	/* TODO: Need check if this function works with the real H/W behavior */
	cnt = msm_host->clk_cnt;
	/* disable dsi clk */
	for (i = 0; i < cnt; i++)
		dsi_clk_ctrl(msm_host, 0);

	/* enable dsi clk */
	for (i = 0; i < cnt; i++)
		dsi_clk_ctrl(msm_host, 1);
}

static void dsi_sw_reset_restore(struct msm_dsi_host *msm_host)
{
	u32 data0, data1;

	data0 = dsi_read(msm_host, REG_DSI_CTRL);
	data1 = data0;
	data1 &= ~DSI_CTRL_ENABLE;
	dsi_write(msm_host, REG_DSI_CTRL, data1);
	/*
	 * dsi controller need to be disabled before
	 * clocks turned on
	 */
	wmb();

	dsi_write(msm_host, REG_DSI_CLK_CTRL,
		DSI_CLK_CTRL_AHBS_HCLK_ON | DSI_CLK_CTRL_AHBM_SCLK_ON |
		DSI_CLK_CTRL_PCLK_ON | DSI_CLK_CTRL_DSICLK_ON |
		DSI_CLK_CTRL_BYTECLK_ON | DSI_CLK_CTRL_ESCCLK_ON |
		DSI_CLK_CTRL_FORCE_ON_DYN_AHBM_HCLK);
	wmb();	/* make sure clocks enabled */

	/* dsi controller can only be reset while clocks are running */
	dsi_write(msm_host, REG_DSI_RESET, 1);
	wmb();	/* make sure reset happen */
	dsi_write(msm_host, REG_DSI_RESET, 0);
	wmb();	/* controller out of reset */
	dsi_write(msm_host, REG_DSI_CTRL, data0);
	wmb();	/* make sure dsi controller enabled again */
}

static void dsi_err_worker(struct work_struct *work)
{
	struct msm_dsi_host *msm_host =
		container_of(work, struct msm_dsi_host, err_work);
	u32 status = msm_host->err_work_state;

	if (status & DSI_ERR_STATE_PLL_UNLOCKED)
		dsi_pll_relock(msm_host);

	if (status & DSI_ERR_STATE_MDP_FIFO_UNDERFLOW)
		dsi_sw_reset_restore(msm_host);

	complete(&msm_host->mdp_comp);

	/* enable dsi error interrupt */
	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_ERROR, 1);
}

static void dsi_ack_err_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_ACK_ERR_STATUS);

	if (status) {
		dsi_write(msm_host, REG_DSI_ACK_ERR_STATUS, status);
		/* Writing of an extra 0 needed to clear error bits */
		dsi_write(msm_host, REG_DSI_ACK_ERR_STATUS, 0);
		pr_err("%s: status=%x\n", __func__, status);
	}
}

static void dsi_timeout_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_TIMEOUT_STATUS);

	if (status & 0x0111) {
		dsi_write(msm_host, REG_DSI_TIMEOUT_STATUS, status);
		pr_err("%s: status=%x\n", __func__, status);
	}
}

static void dsi_dln0_phy_err(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_DLN0_PHY_ERR);

	if (status & 0x011111) {
		dsi_write(msm_host, REG_DSI_DLN0_PHY_ERR, status);
		pr_err("%s: status=%x\n", __func__, status);
	}
}

static void dsi_fifo_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_FIFO_STATUS);

	/* fifo underflow, overflow */
	if (status & 0xcccc4489) {
		dsi_write(msm_host, REG_DSI_FIFO_STATUS, status);
		pr_err("%s: %x, id=%d\n", __func__, status, msm_host->id);
		if (status & DSI_FIFO_STATUS_CMD_MDP_FIFO_UNDERFLOW)
			msm_host->err_work_state |=
					DSI_ERR_STATE_MDP_FIFO_UNDERFLOW;
	}
}

static void dsi_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_STATUS0);

	if (status & DSI_STATUS0_INTERLEAVE_OP_CONTENTION) {
		dsi_write(msm_host, REG_DSI_STATUS0, status);
		pr_err("%s: status=%x\n", __func__, status);
	}
}

static void dsi_clk_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_CLK_STATUS);

	if (status & DSI_CLK_STATUS_PLL_UNLOCKED) {
		dsi_write(msm_host, REG_DSI_CLK_STATUS, status);
		msm_host->err_work_state |= DSI_ERR_STATE_PLL_UNLOCKED;
		pr_err("%s: status=%x\n", __func__, status);
	}
}

static void dsi_error(struct msm_dsi_host *msm_host)
{

	pr_err("%s\n", __func__);
	/* disable dsi error interrupt */
	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_ERROR, 0);

	dsi_clk_status(msm_host);
	dsi_fifo_status(msm_host);
	dsi_ack_err_status(msm_host);
	dsi_timeout_status(msm_host);
	dsi_status(msm_host);
	dsi_dln0_phy_err(msm_host);

	queue_work(msm_host->workqueue, &msm_host->err_work);
}

static int dsi_host_parse_dt(struct msm_dsi_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	int index;

	DBG("");

	ret = of_property_read_u32(np, "cell-index", &index);
	if (ret) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, ret=%d\n",
			__func__, ret);
		return -EINVAL;
	}

	if (index == 0) {
		msm_host->id = DSI_HOST_0;
	} else if (index == 1) {
		msm_host->id = DSI_HOST_1;
	} else {
		pr_err("%s: Invalid index, %d\n", __func__, index);
		return -EINVAL;
	}
	msm_host_list[msm_host->id] = msm_host;
	pdev->id = index;

	DBG("exit");
	return 0;
}

static int dsi_host_enable(struct msm_dsi_host *msm_host)
{
	int ret = 0;
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;

	DBG("msm_host=%p id=%d\n", msm_host, msm_host->id);

	ret = dsi_host_regulator_enable(msm_host);
	if (ret) {
		pr_err("%s:Failed to enable vregs.ret=%d\n",
			__func__, ret);
		return ret;
	}

	ret = dsi_bus_clk_enable(msm_host);
	if (ret) {
		pr_err("%s: failed to enable bus clocks, %d\n", __func__, ret);
		goto fail_disable_reg;
	}

	dsi_phy_sw_reset(msm_host);
	if (msm_host->phy.enable) {
		ret = msm_host->phy.enable(&msm_host->phy,
				&msm_host_list[DSI_HOST_CLOCK_MASTER]->phy,
				msm_panel->desc.phy_timing_setting);
		if (ret) {
			pr_err("%s: failed to enable phy, %d\n", __func__, ret);
			dsi_bus_clk_disable(msm_host);
			goto fail_disable_reg;
		}
	}
	dsi_bus_clk_disable(msm_host);

	ret = dsi_calc_clk_rate(msm_host, msm_panel->desc.vm.pixelclock,
				msm_panel->frame_rate);
	if (ret) {
		pr_err("%s: unable to calc clk rate, %d\n", __func__, ret);
		goto fail_disable_reg;
	}

	ret = dsi_clk_ctrl(msm_host, 1);
	if (ret) {
		pr_err("%s: failed to enable clocks. ret=%d\n", __func__, ret);
		goto fail_disable_reg;
	}

	dsi_ctrl_setup(msm_host);
	dsi_sw_reset(msm_host);
	dsi_ctrl_init(msm_host);

	dsi_op_mode_config(msm_host, msm_panel->mode);


	/* TODO: clock should be turned off for command mode,
	 * and only turned on before MDP START.
	 * This part of code should be enabled once mdp driver support it.
	 */
	/* if (msm_panel->mode == MSM_DSI_CMD_MODE)
		dsi_clk_ctrl(msm_host, 0); */

	DBG("exit");
	return 0;

fail_disable_reg:
	dsi_host_regulator_disable(msm_host);
	return ret;
}

static int dsi_host_disable(struct msm_dsi_host *msm_host)
{

	DBG("%s+: msm_host=%p id=%d\n", __func__,
				msm_host, msm_host->id);

	/* TODO: We keep the clock on for now.
	 * Once we have a way to trun on/off clock for each frame,
	 * we need to turn on clock here for cmd mode.
	 */
	/* if (msm_panel->mode == MSM_DSI_CMD_MODE)
		dsi_clk_ctrl(msm_host, 1); */

	/* disable DSI controller */
	dsi_check_idle(msm_host, false);

	/* disable DSI phy
	 * In dual-dsi configuration, the phy should be disabled for the
	 * first controller only when the second controller is disabled.
	 * This is true regardless of whether broadcast mode is enabled.
	 */
	if (msm_host_list[DSI_HOST_CLOCK_SLAVE]->msm_panel) {
		if (msm_host->id == DSI_HOST_CLOCK_SLAVE) {
			struct msm_dsi_host *mhost =
				msm_host_list[DSI_HOST_CLOCK_MASTER];
			if (msm_host->phy.disable)
				msm_host->phy.disable(&msm_host->phy);
			if (mhost && mhost->phy.disable)
				mhost->phy.disable(&mhost->phy);
		}
	} else if (msm_host->phy.disable) {
		msm_host->phy.disable(&msm_host->phy);
	}

	dsi_clk_ctrl(msm_host, 0);

	dsi_host_regulator_disable(msm_host);

	DBG("-");

	return 0;
}

static int dsi_host_on_sub(struct msm_dsi_host *msm_host)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	int ret = 0;

	mutex_lock(&msm_host->dev_mutex);
	if (msm_host->power_on) {
		DBG("dsi already on");
		goto unlock_ret;
	}

	ret = drm_panel_prepare(&msm_panel->base);
	if (ret) {
		pr_err("%s: unable to prepare the panel, %d\n", __func__, ret);
		goto unlock_ret;
	}

	ret = dsi_host_enable(msm_host);
	if (ret) {
		pr_err("%s: unable to enable host, %d\n", __func__, ret);
		drm_panel_unprepare(&msm_panel->base);
		goto unlock_ret;
	}

	/* Set power on before panel enable to allow cmd transfer */
	msm_host->power_on = true;

	ret = drm_panel_enable(&msm_panel->base);
	if (ret) {
		pr_err("%s: unable to enable the panel, %d\n", __func__, ret);
		dsi_host_disable(msm_host);
		drm_panel_unprepare(&msm_panel->base);
		msm_host->power_on = false;
		goto unlock_ret;
	}

	if (msm_dsi_panel_bl_ctrl(&msm_panel->base, msm_panel->bl_max))
		DBG("%s: turn on backlight failed\n", __func__);

unlock_ret:
	mutex_unlock(&msm_host->dev_mutex);
	return ret;
}

static int dsi_host_off_sub(struct msm_dsi_host *msm_host)
{
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	int ret;

	mutex_lock(&msm_host->dev_mutex);
	if (!msm_host->power_on) {
		DBG("dsi already off");
		mutex_unlock(&msm_host->dev_mutex);
		return 0;
	}

	msm_dsi_panel_bl_ctrl(&msm_panel->base, 0);

	dsi_op_mode_config(msm_host, MSM_DSI_CMD_MODE);

	/* Since we have disabled INTF, the video engine won't stop so that
	 * the cmd engine will be blocked.
	 * Reset to disable video engine so that we can send off cmd.
	 */
	dsi_sw_reset(msm_host);

	ret = drm_panel_disable(&msm_panel->base);
	if (ret)
		pr_err("%s: Panel OFF failed\n", __func__);

	ret = dsi_host_disable(msm_host);
	if (ret)
		pr_err("%s: host disable failed\n", __func__);

	ret = drm_panel_unprepare(&msm_panel->base);
	if (ret)
		pr_err("%s: Panel unprepare failed\n", __func__);

	msm_host->power_on = false;

	mutex_unlock(&msm_host->dev_mutex);
	return 0;
}

static int msm_dsi_host_attach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *dsi)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	struct msm_dsi_panel *msm_panel = mipi_dsi_get_drvdata(dsi);

	msm_host->msm_panel = msm_panel;

	return 0;
}

static int msm_dsi_host_detach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *dsi)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	msm_host->msm_panel = NULL;

	return 0;
}

static ssize_t msm_dsi_host_transfer(struct mipi_dsi_host *host,
						struct mipi_dsi_msg *msg)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	int ret = -EINVAL;

	if (!msg || !msm_host->power_on)
		return -EINVAL;

	mutex_lock(&msm_host->cmd_mutex);

	/* TODO: make sure dsi_cmd_mdp is idle.
	 * Since DSI6G v1.2.0, we can set DSI_TRIG_CTRL.BLOCK_DMA_WITHIN_FRAME
	 * to ask H/W to wait until cmd mdp is idle. S/W wait is not needed.
	 * How to handle the old versions? Wait for mdp cmd done?
	 */

	DBG("%s: pid=%d\n", __func__, current->pid);

	/*
	 * mdss interrupt is generated in mdp core clock domain
	 * mdp clock need to be enabled to receive dsi interrupt
	 */
	dsi_clk_ctrl(msm_host, 1);

	/* TODO: vote for bus bandwidth */

	if (!(msg->flags & MIPI_DSI_MSG_USE_LPM))
		dsi_set_tx_power_mode(0, msm_host);

	if (msg->rx_buf && msg->rx_len)
		ret = dsi_cmds_rx(msm_host, msg);
	else
		ret = dsi_cmds_tx(msm_host, msg);

	if (!(msg->flags & MIPI_DSI_MSG_USE_LPM))
		dsi_set_tx_power_mode(1, msm_host);

	dsi_clk_ctrl(msm_host, 0);

	/* TODO: unvote bandwidth */

	mutex_unlock(&msm_host->cmd_mutex);
	return ret;
}

static struct mipi_dsi_host_ops msm_dsi_host_ops = {
	.attach = msm_dsi_host_attach,
	.detach = msm_dsi_host_detach,
	.transfer = msm_dsi_host_transfer,
};

int msm_dsi_host_init(struct msm_dsi *msm_dsi)
{
	int ret = 0;
	u32 index;
	struct msm_dsi_host *msm_host = NULL;
	struct platform_device *pdev = msm_dsi->pdev;

	msm_host = devm_kzalloc(&pdev->dev, sizeof(*msm_host), GFP_KERNEL);
	if (!msm_host) {
		pr_err("%s: FAILED: cannot alloc dsi host\n",
		       __func__);
		ret = -ENOMEM;
		goto fail;
	}
	msm_host->pdev = pdev;

	msm_host->base.dev = &pdev->dev;
	msm_host->base.ops = &msm_dsi_host_ops;
	ret = mipi_dsi_host_register(&msm_host->base);
	if (ret) {
		pr_err("%s: mipi_dsi_host_register failed\n", __func__);
		goto fail;
	}

	ret = dsi_host_parse_dt(msm_host);
	if (ret) {
		pr_err("%s: dsi panel dev reg failed\n", __func__);
		goto fail;
	}

	msm_host->supplies[MSM_DSI_HOST_SUPPLY_GDSC].supply = "gdsc";
	msm_host->supplies[MSM_DSI_HOST_SUPPLY_VDD].supply = "vdd";
	msm_host->supplies[MSM_DSI_HOST_SUPPLY_VDDA].supply = "vdda";
	msm_host->supplies[MSM_DSI_HOST_SUPPLY_VDDIO].supply = "vddio";
	ret = devm_regulator_bulk_get(&pdev->dev,
			MSM_DSI_HOST_SUPPLY_NUM, msm_host->supplies);
	if (ret) {
		pr_err("%s: failed to init regulator, ret=%d\n",
						__func__, ret);
		goto host_unregister;
	}

	ret = dsi_clk_init(msm_host);
	if (ret) {
		pr_err("%s: unable to initialize dsi clks\n", __func__);
		goto host_unregister;
	}

	msm_host->ctrl_base = msm_ioremap(pdev, "dsi_ctrl", "DSI CTRL");
	if (IS_ERR(msm_host->ctrl_base)) {
		pr_err("%s: unable to map Dsi ctrl base\n", __func__);
		ret = PTR_ERR(msm_host->ctrl_base);
		goto host_unregister;
	}

	ret = dsi_host_regulator_enable(msm_host);
	if (ret) {
		pr_err("%s: unable to enable regulators\n", __func__);
		goto host_unregister;
	}
	ret = dsi_bus_clk_enable(msm_host);
	if (ret) {
		pr_err("%s: unable to enable clks\n", __func__);
		dsi_host_regulator_disable(msm_host);
		goto host_unregister;
	}
	msm_host->cfg = dsi_get_config(msm_host);
	if (!msm_host->cfg) {
		ret = -EINVAL;
		pr_err("%s: get config failed\n", __func__);
		goto host_unregister;
	}
	dsi_bus_clk_disable(msm_host);
	dsi_host_regulator_disable(msm_host);

	ret = msm_dsi_phy_init(&msm_host->phy, pdev, msm_host->cfg->phy_type);
	if (ret) {
		pr_err("%s: phy init failed, %d\n", __func__, ret);
		goto host_unregister;
	}

	msm_host->rx_buf = devm_kzalloc(&pdev->dev, SZ_4K, GFP_KERNEL);
	if (!msm_host->rx_buf) {
		pr_err("%s: alloc rx temp buf failed\n", __func__);
		goto host_unregister;
	}

	init_completion(&msm_host->dma_comp);
	init_completion(&msm_host->mdp_comp);
	init_completion(&msm_host->video_comp);
	init_completion(&msm_host->bta_comp);
	mutex_init(&msm_host->dev_mutex);
	mutex_init(&msm_host->cmd_mutex);
	mutex_init(&msm_host->clk_mutex);
	spin_lock_init(&msm_host->intr_lock);

	/* setup workqueue */
	msm_host->workqueue = alloc_ordered_workqueue("dsi_drm_work", 0);
	INIT_WORK(&msm_host->err_work, dsi_err_worker);

	msm_dsi->host = &msm_host->base;

	if (msm_host->msm_panel)
		msm_dsi->panel = &msm_host->msm_panel->base;
	else
		pr_warn("%s: no panel connected to host %d\n",
				__func__, msm_host->id);

	DBG("%s: Dsi Ctrl->%d initialized\n", __func__, index);
	return 0;

host_unregister:
	mipi_dsi_host_unregister(&msm_host->base);
fail:
	return ret;
}

void msm_dsi_host_destroy(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	DBG("");
	dsi_tx_buf_free(msm_host);
	if (msm_host->workqueue) {
		flush_workqueue(msm_host->workqueue);
		destroy_workqueue(msm_host->workqueue);
		msm_host->workqueue = NULL;
	}

	mutex_destroy(&msm_host->clk_mutex);
	mutex_destroy(&msm_host->cmd_mutex);
	mutex_destroy(&msm_host->dev_mutex);

	mipi_dsi_host_unregister(&msm_host->base);
}

int msm_dsi_host_modeset_init(struct mipi_dsi_host *host,
					struct drm_device *dev)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	int ret;

	msm_host->dev = dev;
	ret = dsi_tx_buf_alloc(msm_host, SZ_4K);
	if (ret)
		pr_err("%s: alloc tx gem obj failed, %d\n", __func__, ret);

	return ret;
}

irqreturn_t msm_dsi_host_irq(int irq, void *ptr)
{
	struct mipi_dsi_host *host = ptr;
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	u32 isr;
	unsigned long flags;

	if (!msm_host->ctrl_base) {
		pr_err("%s:%d DSI base adr no Initialized",
						__func__, __LINE__);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&msm_host->intr_lock, flags);
	isr = dsi_read(msm_host, REG_DSI_INTR_CTRL);
	dsi_write(msm_host, REG_DSI_INTR_CTRL, isr);
	spin_unlock_irqrestore(&msm_host->intr_lock, flags);

	DBG("isr=0x%x, id=%d\n", isr, msm_host->id);

	if ((desc->broadcast_enabled) && (msm_panel->id == desc->trigger_id)
		&& other_host) {
		u32 isr0;
		spin_lock_irqsave(&other_host->intr_lock, flags);
		isr0 = dsi_read(other_host, REG_DSI_INTR_CTRL);
		if (isr0 & DSI_IRQ_CMD_DMA_DONE)
			dsi_write(other_host, REG_DSI_INTR_CTRL,
				DSI_IRQ_CMD_DMA_DONE);
		spin_unlock_irqrestore(&other_host->intr_lock, flags);
	}

	if (isr & DSI_IRQ_ERROR)
		dsi_error(msm_host);

	if (isr & DSI_IRQ_VIDEO_DONE)
		complete(&msm_host->video_comp);

	if (isr & DSI_IRQ_CMD_DMA_DONE)
		complete(&msm_host->dma_comp);

	if (isr & DSI_IRQ_CMD_MDP_DONE)
		complete(&msm_host->mdp_comp);

	if (isr & DSI_IRQ_BTA_DONE)
		complete(&msm_host->bta_comp);

	return IRQ_HANDLED;
}

int msm_dsi_host_on(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc;
	int ret;

	DBG("%d", msm_host->id);
	if (!msm_panel)
		return -ENODEV;

	desc = &msm_panel->desc;
	if (desc->broadcast_enabled && (msm_panel->id == desc->trigger_id))
		return 0;

	ret = dsi_host_on_sub(msm_host);
	if (ret < 0)
		return ret;

	if (desc->broadcast_enabled && (msm_panel->id != desc->trigger_id)
		&& other_host) {
		ret = dsi_host_on_sub(other_host);
		if (ret)
			dsi_host_off_sub(msm_host);
	}

	return 0;
}

int msm_dsi_host_off(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	struct msm_dsi_host *other_host = get_other_host(msm_host);
	struct msm_dsi_panel *msm_panel = msm_host->msm_panel;
	struct msm_dsi_panel_desc *desc;

	DBG("%d", msm_host->id);
	if (!msm_panel)
		return -ENODEV;

	desc = &msm_panel->desc;
	if (desc->broadcast_enabled && (msm_panel->id == desc->trigger_id))
		return 0;

	dsi_host_off_sub(msm_host);
	if (desc->broadcast_enabled && (msm_panel->id != desc->trigger_id)
		&& other_host)
		dsi_host_off_sub(other_host);

	return 0;
}

