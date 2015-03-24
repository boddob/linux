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

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drmP.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>

struct acx450akm {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct videomode vm;

	bool prepared;
	bool enabled;
};

static inline struct acx450akm *panel_to_acx450akm(struct drm_panel *panel)
{
	return container_of(panel, struct acx450akm, panel);
}

static int acx450akm_power_on(struct acx450akm *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(5000, 10000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5000, 10000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(5000, 10000);

	return 0;
}

static int acx450akm_power_off(struct acx450akm *ctx)
{
	gpiod_set_value(ctx->reset_gpio, 0);
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int acx450akm_disable(struct drm_panel *panel)
{
	struct acx450akm *ctx = panel_to_acx450akm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
	return 0;
}

static int acx450akm_unprepare(struct drm_panel *panel)
{
	struct acx450akm *ctx = panel_to_acx450akm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret = 0;

	if (!ctx->prepared)
		return 0;

	if (mipi_dsi_dcs_set_display_off(dsi) < 0)
		ret = -ECOMM;

	if (mipi_dsi_dcs_enter_sleep_mode(dsi) < 0)
		ret = -ECOMM;
	msleep(120);

	acx450akm_power_off(ctx);

	ctx->prepared = false;

	return ret;
}

static int acx450akm_prepare(struct drm_panel *panel)
{
	struct acx450akm *ctx = panel_to_acx450akm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 pwrsave = 0;
	u8 ctrl_display = 0x2c;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = acx450akm_power_on(ctx);
	if (ret < 0)
		return ret;

	if (mipi_dsi_dcs_write(dsi, 0x55, &pwrsave, sizeof(pwrsave)) <
		(sizeof(pwrsave) + 1))
		return -ECOMM;

	if (mipi_dsi_dcs_write(dsi, 0x53, &ctrl_display,
		sizeof(ctrl_display)) < (sizeof(ctrl_display) + 1))
		return -ECOMM;

	if (mipi_dsi_dcs_set_display_on(dsi) < 0)
		return -ECOMM;

	if (mipi_dsi_dcs_exit_sleep_mode(dsi) < 0)
		return -ECOMM;

	ctx->prepared = true;

	return ret;
}

static int acx450akm_enable(struct drm_panel *panel)
{
	struct acx450akm *ctx = panel_to_acx450akm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}
	ctx->enabled = true;

	return 0;
}

static int acx450akm_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct acx450akm *ctx = panel_to_acx450akm(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_ERROR("failed to create a new display mode\n");
		return 0;
	}

	drm_display_mode_from_videomode(&ctx->vm, mode);
	mode->width_mm = 61;
	mode->height_mm = 110;
	connector->display_info.width_mm = 61;
	connector->display_info.height_mm = 110;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs acx450akm_drm_funcs = {
	.disable = acx450akm_disable,
	.unprepare = acx450akm_unprepare,
	.prepare = acx450akm_prepare,
	.enable = acx450akm_enable,
	.get_modes = acx450akm_get_modes,
};

static int acx450akm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct acx450akm *ctx;
	struct device_node *np;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/* Video burst mode, disable EoT packets in HS mode */
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_MODE_EOT_PACKET;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdda";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset");
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	ret = gpiod_direction_output(ctx->reset_gpio, 0);
	if (ret) {
		dev_err(dev, "cannot set dir to reset-gpios %d\n", ret);
		return ret;
	}

	ret = of_get_videomode(ctx->dev->of_node, &ctx->vm, 0);
	if (ret < 0)
		return ret;

#if 0
	np = of_parse_phandle(ctx->dev->of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
#endif

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &acx450akm_drm_funcs;
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	return ret;
}

static int acx450akm_remove(struct mipi_dsi_device *dsi)
{
	struct acx450akm *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static struct of_device_id acx450akm_of_match[] = {
	{ .compatible = "jdi,acx450akm", },
	{ }
};
MODULE_DEVICE_TABLE(of, acx450akm_of_match);

static struct mipi_dsi_driver acx450akm_driver = {
	.driver = {
		.name = "panel_jdi_acx450akm",
		.of_match_table = acx450akm_of_match,
	},
	.probe = acx450akm_probe,
	.remove = acx450akm_remove,
};
module_mipi_dsi_driver(acx450akm_driver);

MODULE_DESCRIPTION("JDI ACX450AKM DSI Panel Driver");
MODULE_LICENSE("GPL v2");

