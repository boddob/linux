/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#define WRITE_DISPLAY_BRIGHTNESS			0x51
#define WRITE_CONTROL_DISPLAY				0x53
#define WRITE_CONTENT_ADAPTIVE_BRIGHTNESS_CONTROL	0x55

struct sharp_wqxga {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct videomode vm;
	u32 width_mm;
	u32 height_mm;

	struct mipi_dsi_device *dsi[2];

	bool prepared;
	bool enabled;
};

static inline struct sharp_wqxga *panel_to_sharp_wqxga(struct drm_panel *panel)
{
	return container_of(panel, struct sharp_wqxga, panel);
}

static int sharp_wqxga_power_on(struct sharp_wqxga *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(2000, 2010);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5000, 5010);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(12000, 12010);

	return 0;
}

static int sharp_wqxga_power_off(struct sharp_wqxga *ctx)
{
	gpiod_set_value(ctx->reset_gpio, 0);
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int sharp_wqxga_disable(struct drm_panel *panel)
{
	struct sharp_wqxga *ctx = panel_to_sharp_wqxga(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
	return 0;
}

static int sharp_wqxga_unprepare(struct drm_panel *panel)
{
	struct sharp_wqxga *ctx = panel_to_sharp_wqxga(panel);
	struct mipi_dsi_device **dsis = ctx->dsi;
	int ret = 0, i;

	if (!ctx->prepared)
		return 0;

	for (i = 0; i < 2; i++)
		if (mipi_dsi_dcs_set_display_off(dsis[i]) < 0)
			ret = -ECOMM;
	msleep(50);

	for (i = 0; i < 2; i++)
		if (mipi_dsi_dcs_set_tear_off(dsis[i]) < 0)
			ret = -ECOMM;
	usleep_range(5000, 10000);

	for (i = 0; i < 2; i++)
		if (mipi_dsi_dcs_enter_sleep_mode(dsis[i]) < 0)
			ret = -ECOMM;
	msleep(120);

	sharp_wqxga_power_off(ctx);

	ctx->prepared = false;

	return ret;
}

static int sharp_wqxga_prepare(struct drm_panel *panel)
{
	struct sharp_wqxga *ctx = panel_to_sharp_wqxga(panel);
	struct mipi_dsi_device **dsis = ctx->dsi;
	struct mipi_dsi_device *d;
	u8 scanline[2] = {0, 0};
	u8 dpy_brightness = 0xff;
	u8 ctl_brightness = 0x24;
	u8 cabc = 0;
	int ret, i;

	if (ctx->prepared)
		return 0;

	ret = sharp_wqxga_power_on(ctx);
	if (ret < 0)
		return ret;

	/* reset command */
	for (i = 0; i < 2; i++)
		if (mipi_dsi_dcs_soft_reset(dsis[i]) < 0) {
			printk(KERN_ERR "failed to send soft reset!\n");
			return -ECOMM;
	}

	usleep_range(5000, 10000);

	/* Configure 2 links */
	for (i = 0; i < 2; i++) {
		u8 mode;

		d = dsis[i];

		if (mipi_dsi_dcs_nop(d) < 0) {
			printk(KERN_ERR "failed to send NOP\n");
			return -ECOMM;
		}

		mode = 0;
		ret = mipi_dsi_dcs_get_pixel_format(d, &mode);
		printk(KERN_ERR "mode %x ret %d\n", ret);
		
	}

#if 0
		/* write DBI format */
		if (mipi_dsi_dcs_set_pixel_format(d,
					MIPI_DCS_PIXEL_FMT_24BIT) < 0)
			return -ECOMM;

		if (mipi_dsi_dcs_set_column_address(d, 0,
					ctx->vm.hactive / 2 - 1) < 0)
			return -ECOMM;

		if (mipi_dsi_dcs_set_page_address(d, 0,
					ctx->vm.vactive - 1) < 0)
			return -ECOMM;

		if (mipi_dsi_dcs_set_tear_on(d,
					MIPI_DSI_DCS_TEAR_MODE_VBLANK) < 0)
			return -ECOMM;

		if (mipi_dsi_dcs_write(d, MIPI_DCS_SET_TEAR_SCANLINE,
			scanline, sizeof(scanline)) < (sizeof(scanline) + 1))
			return -ECOMM;

		if (mipi_dsi_dcs_write(d, WRITE_DISPLAY_BRIGHTNESS,
			&dpy_brightness, sizeof(dpy_brightness)) <
			(sizeof(dpy_brightness) + 1))
			return -ECOMM;

		/* Enable LED PWM */
		if (mipi_dsi_dcs_write(d, WRITE_CONTROL_DISPLAY,
			&ctl_brightness, sizeof(ctl_brightness)) <
			(sizeof(ctl_brightness) + 1))
			return -ECOMM;

		/* Turn off CABC */
		if (mipi_dsi_dcs_write(d,
			WRITE_CONTENT_ADAPTIVE_BRIGHTNESS_CONTROL, &cabc,
			sizeof(cabc)) < (sizeof(cabc) + 1))
			return -ECOMM;
	}
#endif
	usleep_range(5000, 10000);

	for (i = 0; i < 2; i++)
		if (mipi_dsi_dcs_exit_sleep_mode(dsis[i]) < 0) {
			printk(KERN_ERR "failed to exit sleep mode\n");
			return -ECOMM;
		}
	msleep(120);

	for (i = 0; i < 2; i++)
		if (mipi_dsi_dcs_set_display_on(dsis[i]) < 0) {
			printk(KERN_ERR "failed to enable\n");
			return -ECOMM;
		}
	usleep_range(10000, 15000);

	ctx->prepared = true;

	return ret;
}

static int sharp_wqxga_enable(struct drm_panel *panel)
{
	struct sharp_wqxga *ctx = panel_to_sharp_wqxga(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}
	ctx->enabled = true;

	return 0;
}

static int sharp_wqxga_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct sharp_wqxga *ctx = panel_to_sharp_wqxga(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_ERROR("failed to create a new display mode\n");
		return 0;
	}

	drm_display_mode_from_videomode(&ctx->vm, mode);
	mode->width_mm = ctx->width_mm;
	mode->height_mm = ctx->height_mm;
	connector->display_info.width_mm = ctx->width_mm;
	connector->display_info.height_mm = ctx->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs sharp_wqxga_drm_funcs = {
	.disable = sharp_wqxga_disable,
	.unprepare = sharp_wqxga_unprepare,
	.prepare = sharp_wqxga_prepare,
	.enable = sharp_wqxga_enable,
	.get_modes = sharp_wqxga_get_modes,
};

static int sharp_wqxga_panel_add(struct sharp_wqxga *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *np;
	int ret;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vdda";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
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

	ret = of_get_videomode(dev->of_node, &ctx->vm, 0);
	if (ret < 0)
		return ret;
	of_property_read_u32(dev->of_node, "panel-width-mm", &ctx->width_mm);
	of_property_read_u32(dev->of_node, "panel-height-mm", &ctx->height_mm);

#if 0
	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
#endif
	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &sharp_wqxga_drm_funcs;
	drm_panel_add(&ctx->panel);

	return 0;
}

static void sharp_wqxga_panel_del(struct sharp_wqxga *ctx)
{
	if (ctx->panel.dev)
		drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	if (ctx->dsi[1])
		put_device(&ctx->dsi[1]->dev);
}

static int sharp_wqxga_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sharp_wqxga *ctx;
	struct mipi_dsi_device *secondary = NULL;
	struct device_node *np;
	int ret;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	/* Only DSI-LINK1 node has "link2" entry. */
	np = of_parse_phandle(dsi->dev.of_node, "link2", 0);
	if (np) {
		secondary = of_find_mipi_dsi_device_by_node(np);
		of_node_put(np);

		if (!secondary)
			return -EPROBE_DEFER;

		ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
		if (!ctx) {
			put_device(&secondary->dev);
			return -ENOMEM;
		}
		mipi_dsi_set_drvdata(dsi, ctx);

		ctx->dev = dev;
		ctx->dsi[0] = dsi;
		ctx->dsi[1] = secondary;

		ret = sharp_wqxga_panel_add(ctx);
		if (ret) {
			put_device(&secondary->dev);
			return ret;
		}
	}

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		if (secondary)
			sharp_wqxga_panel_del(ctx);
		return ret;
	}

	return ret;
}

static int sharp_wqxga_remove(struct mipi_dsi_device *dsi)
{
	struct sharp_wqxga *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);

	/* delete panel only for the DSI1 interface */
	if (ctx)
		sharp_wqxga_panel_del(ctx);

	return 0;
}

static struct of_device_id sharp_wqxga_of_match[] = {
	{ .compatible = "sharp,dual_wqxga", },
	{ }
};
MODULE_DEVICE_TABLE(of, sharp_wqxga_of_match);

static struct mipi_dsi_driver sharp_wqxga_driver = {
	.driver = {
		.name = "panel_sharp_wqxga",
		.of_match_table = sharp_wqxga_of_match,
	},
	.probe = sharp_wqxga_probe,
	.remove = sharp_wqxga_remove,
};
module_mipi_dsi_driver(sharp_wqxga_driver);

MODULE_DESCRIPTION("sharp wqxga DSI Panel Driver");
MODULE_LICENSE("GPL v2");
