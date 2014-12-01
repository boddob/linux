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

#include "msm_kms.h"

#include <drm/drm_panel.h>
#include "dsi.h"

struct dsi_connector {
	struct drm_connector base;
	struct msm_dsi *msm_dsi;
};
#define to_dsi_connector(x) container_of(x, struct dsi_connector, base)

static enum drm_connector_status dsi_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);
	struct msm_dsi *msm_dsi = dsi_connector->msm_dsi;

	DBG("");
	return msm_dsi->panel ?
		connector_status_connected : connector_status_disconnected;
}

static void dsi_connector_destroy(struct drm_connector *connector)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);
	struct msm_dsi *msm_dsi = dsi_connector->msm_dsi;

	DBG("");
	if (msm_dsi->panel)
		drm_panel_detach(msm_dsi->panel);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	kfree(dsi_connector);
}

static int dsi_connector_get_modes(struct drm_connector *connector)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);
	struct msm_dsi *msm_dsi = dsi_connector->msm_dsi;

	if (!msm_dsi->panel)
		return 0;

	return drm_panel_get_modes(msm_dsi->panel);
}

static int dsi_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual, requested;

	DBG("");
	requested = 1000 * mode->clock;
	actual = kms->funcs->round_pixclk(kms,
			requested, dsi_connector->msm_dsi->encoder);

	DBG("requested=%ld, actual=%ld", requested, actual);
	if (actual != requested)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static struct drm_encoder *
dsi_connector_best_encoder(struct drm_connector *connector)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);

	DBG("");
	return dsi_connector->msm_dsi->encoder;
}

static const struct drm_connector_funcs dsi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = dsi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dsi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs dsi_connector_helper_funcs = {
	.get_modes = dsi_connector_get_modes,
	.mode_valid = dsi_connector_mode_valid,
	.best_encoder = dsi_connector_best_encoder,
};

/* initialize connector */
struct drm_connector *dsi_connector_init(struct msm_dsi *msm_dsi)
{
	struct drm_connector *connector = NULL;
	struct dsi_connector *dsi_connector;
	int ret;

	dsi_connector = kzalloc(sizeof(*dsi_connector), GFP_KERNEL);
	if (!dsi_connector) {
		ret = -ENOMEM;
		goto fail;
	}

	dsi_connector->msm_dsi = msm_dsi;

	connector = &dsi_connector->base;

	ret = drm_connector_init(msm_dsi->dev, connector, &dsi_connector_funcs,
			DRM_MODE_CONNECTOR_DSI);
	if (ret)
		goto fail;

	drm_connector_helper_add(connector, &dsi_connector_helper_funcs);

	/* We don't support HPD, so only poll status until connected. */
	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	/* Display driver doesn't support interlace now. */
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	ret = drm_connector_register(connector);
	if (ret)
		goto fail;

	if (msm_dsi->panel) {
		ret = drm_panel_attach(msm_dsi->panel, connector);
		if (ret)
			goto fail;
	}

	return connector;

fail:
	if (connector)
		dsi_connector_destroy(connector);

	return ERR_PTR(ret);
}

