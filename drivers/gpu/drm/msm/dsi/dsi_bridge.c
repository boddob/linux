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

#include "dsi.h"

struct dsi_bridge {
	struct drm_bridge base;
	struct msm_dsi *msm_dsi;
};
#define to_dsi_bridge(x) container_of(x, struct dsi_bridge, base)

static void dsi_bridge_destroy(struct drm_bridge *bridge)
{
	struct dsi_bridge *dsi_bridge = to_dsi_bridge(bridge);
	DBG("");
	drm_bridge_cleanup(bridge);
	kfree(dsi_bridge);
}

static void dsi_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct dsi_bridge *dsi_bridge = to_dsi_bridge(bridge);
	struct msm_dsi *msm_dsi = dsi_bridge->msm_dsi;

	DBG("");
	if (msm_dsi->host)
		msm_dsi_host_on(msm_dsi->host);
}

static void dsi_bridge_enable(struct drm_bridge *bridge)
{
	DBG("");
}

static void dsi_bridge_disable(struct drm_bridge *bridge)
{
	DBG("");
}

static void dsi_bridge_post_disable(struct drm_bridge *bridge)
{
	struct dsi_bridge *dsi_bridge = to_dsi_bridge(bridge);
	struct msm_dsi *msm_dsi = dsi_bridge->msm_dsi;

	DBG("");
	if (msm_dsi->host)
		msm_dsi_host_off(msm_dsi->host);
}

static void dsi_bridge_mode_set(struct drm_bridge *bridge,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);
}

static const struct drm_bridge_funcs dsi_bridge_funcs = {
		.pre_enable = dsi_bridge_pre_enable,
		.enable = dsi_bridge_enable,
		.disable = dsi_bridge_disable,
		.post_disable = dsi_bridge_post_disable,
		.mode_set = dsi_bridge_mode_set,
		.destroy = dsi_bridge_destroy,
};

/* initialize bridge */
struct drm_bridge *dsi_bridge_init(struct msm_dsi *msm_dsi)
{
	struct drm_bridge *bridge = NULL;
	struct dsi_bridge *dsi_bridge;
	int ret;

	dsi_bridge = kzalloc(sizeof(*dsi_bridge), GFP_KERNEL);
	if (!dsi_bridge) {
		ret = -ENOMEM;
		goto fail;
	}

	dsi_bridge->msm_dsi = msm_dsi;

	bridge = &dsi_bridge->base;

	ret = drm_bridge_init(msm_dsi->dev, bridge, &dsi_bridge_funcs);
	if (ret)
		goto fail;

	return bridge;

fail:
	if (bridge)
		dsi_bridge_destroy(bridge);

	return ERR_PTR(ret);
}

