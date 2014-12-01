/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#ifndef __DSI_CONNECTOR_H__
#define __DSI_CONNECTOR_H__

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/qpnp/pwm.h>

#include "drm_crtc.h"
#include "msm_drv.h"

/* Refer to documentations/timers/timers-howto.txt, NON-ATOMIC */
static inline void dsi_sleep(u32 sleep_us)
{
	if (sleep_us < 10)
		udelay(sleep_us);
	else if (sleep_us < 20000)
		usleep_range(sleep_us, sleep_us * 2);
	else
		msleep_interruptible((sleep_us + 500) / 1000);
}

struct msm_dsi {
	struct drm_device *dev;
	struct platform_device *pdev;

	struct drm_connector *connector;
	struct drm_bridge *bridge;

	struct mipi_dsi_host *host;
	struct drm_panel *panel;

	/* the encoder we are hooked to (outside of dsi block) */
	struct drm_encoder *encoder;

	int irq;
};

/* dsi bridge */
struct drm_bridge *dsi_bridge_init(struct msm_dsi *msm_dsi);

/* dsi connector */
struct drm_connector *dsi_connector_init(struct msm_dsi *msm_dsi);

/* dsi control */
int msm_dsi_host_init(struct msm_dsi *msm_dsi);
void msm_dsi_host_destroy(struct mipi_dsi_host *host);
int msm_dsi_host_on(struct mipi_dsi_host *host);
int msm_dsi_host_off(struct mipi_dsi_host *host);
irqreturn_t msm_dsi_host_irq(int irq, void *ptr);
int msm_dsi_host_modeset_init(struct mipi_dsi_host *host,
					struct drm_device *dev);

/* dsi panel */
struct msm_cmd_te_cfg *msm_dsi_panel_get_te_info(struct drm_panel *panel);
enum msm_dsi_mode msm_dsi_panel_get_op_mode(struct drm_panel *panel);
int msm_dsi_panel_bl_ctrl(struct drm_panel *panel, u32 bl_level);

/* dsi phy */
struct dsi_phy {
	void __iomem *base;
	int (*enable)(struct dsi_phy *phy, struct dsi_phy *master_phy,
			u32 timing_ctrl[]);
	void (*disable)(struct dsi_phy *phy);
};
enum msm_dsi_phy_type {
	MSM_DSI_PHY_UNKNOWN,
	MSM_DSI_PHY_28HPM,
	MSM_DSI_PHY_MAX
};
int msm_dsi_phy_init(struct dsi_phy *dsi_phy, struct platform_device *pdev,
			enum msm_dsi_phy_type type);

#endif /* __DSI_CONNECTOR_H__ */

