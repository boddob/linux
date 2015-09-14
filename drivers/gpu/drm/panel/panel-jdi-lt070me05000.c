/*
 * Copyright (C) 2015 InforceComputing
 * Author: Vinay Simha BN <simhavcs@gmail.com>
 *
 * Copyright (C) 2015 Linaro Ltd
 * Author: Sumit Semwal <sumit.semwal@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

/*
 * From internet archives, the panel for Nexus 7 2nd Gen, 2013 model is a
 * JDI model LT070ME05000, and its data sheet is at:
 *  http://panelone.net/en/7-0-inch/JDI_LT070ME05000_7.0_inch-datasheet
 */
struct jdi_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	/* TODO: Add backilght support */
	struct backlight_device *backlight;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *vcc_gpio;

	bool prepared;
	bool enabled;

	const struct drm_display_mode *mode;
};

static inline struct jdi_panel *to_jdi_panel(struct drm_panel *panel)
{
	return container_of(panel, struct jdi_panel, base);
}

static char MCAP[2] = {0xB0, 0x00};
/* static char interface_setting[6] = {0xB3, 0x04, 0x08, 0x00, 0x22, 0x00}; */
static char interface_setting[2] = {0xB3, 0x6F};
static char interface_ID_setting[2] = {0xB4, 0x0C};
static char DSI_control[3] = {0xB6, 0x3A, 0xD3};

/* for fps control, set fps to 60.32Hz */
static char LTPS_timing_setting[2] = {0xC6, 0x78};
static char sequencer_timing_control[2] = {0xD6, 0x01};

/* set brightness */
static char write_display_brightness[] = {0x51, 0xFF};
/* enable LEDPWM pin output, turn on LEDPWM output, turn off pwm dimming */
static char write_control_display[] = {0x53, 0x24};
/* choose cabc mode, 0x00(-0%), 0x01(-15%), 0x02(-40%), 0x03(-54%),
    disable SRE(sunlight readability enhancement) */
static char write_cabc[] = {0x55, 0x00};
/* for cabc mode 0x1(-15%) */
static char backlight_control1[] = {0xB8, 0x07, 0x87, 0x26, 0x18, 0x00, 0x32};
/* for cabc mode 0x2(-40%) */
static char backlight_control2[] = {0xB9, 0x07, 0x75, 0x61, 0x20, 0x16, 0x87};
/* for cabc mode 0x3(-54%) */
static char backlight_control3[] = {0xBA, 0x07, 0x70, 0x81, 0x20, 0x45, 0xB4};
/* for pwm frequency and dimming control */
static char backlight_control4[] = {0xCE, 0x7D, 0x40, 0x48, 0x56, 0x67, 0x78,
                0x88, 0x98, 0xA7, 0xB5, 0xC3, 0xD1, 0xDE, 0xE9, 0xF2, 0xFA,
                0xFF, 0x37, 0xF5, 0x0F, 0x0F, 0x42, 0x00};

static int jdi_panel_init(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x77);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_column_address(dsi, 0x00, 0xAF);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_page_address(dsi, 0x00, 0x7F);
	if (ret < 0)
		return ret;
	mdelay(20);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0)
		return ret;
	mdelay(5);

	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_TEAR_SCANLINE,
			(u8[]){ 0x03, 0x00 }, 2);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write_buffer(dsi, (u8[]){ write_display_brightness[0],
					write_display_brightness[1] }, 1);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write_buffer(dsi, (u8[]){ write_control_display[0],
					 write_control_display[1] }, 1);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_write_buffer(dsi, (u8[]){ write_cabc[0],
					write_cabc[1] }, 1);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	mdelay(5);

	ret = mipi_dsi_generic_write(dsi, MCAP, sizeof(MCAP));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, interface_setting,
					sizeof(interface_setting));
	if (ret < 0)
		return ret;

        ret = mipi_dsi_generic_write(dsi, backlight_control4,
                                        sizeof(backlight_control4));
        if (ret < 0)
                return ret;

	MCAP[1] = 0x03;
	ret = mipi_dsi_generic_write(dsi, MCAP, sizeof(MCAP));
	if (ret < 0)
		return ret;

if(0) {
	ret = mipi_dsi_generic_write(dsi, interface_ID_setting,
					sizeof(interface_ID_setting));
        if (ret < 0)
                return ret;

	ret = mipi_dsi_generic_write(dsi, DSI_control, sizeof(DSI_control));
        if (ret < 0)
                return ret;

	ret = mipi_dsi_generic_write(dsi, LTPS_timing_setting,
					sizeof(LTPS_timing_setting));
        if (ret < 0)
                return ret;

	ret = mipi_dsi_generic_write(dsi, sequencer_timing_control,
					sizeof(sequencer_timing_control));
        if (ret < 0)
                return ret;

        ret = mipi_dsi_generic_write(dsi, backlight_control1,
                                        sizeof(backlight_control1));
        if (ret < 0)
                return ret;

        ret = mipi_dsi_generic_write(dsi, backlight_control2,
                                        sizeof(backlight_control2));
        if (ret < 0)
                return ret;

        ret = mipi_dsi_generic_write(dsi, backlight_control3,
                                        sizeof(backlight_control3));
        if (ret < 0)
                return ret;
}
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;
	mdelay(5);

	return 0;
}

static int jdi_panel_on(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;

	msleep(40);

	return 0;
}

static int jdi_panel_off(struct jdi_panel *jdi)
{
	struct mipi_dsi_device *dsi = jdi->dsi;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_tear_off(dsi);
	if (ret < 0)
		return ret;

	msleep(100);

	return 0;
}

static int jdi_panel_disable(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);

	if (!jdi->enabled)
		return 0;

	DRM_DEBUG("disable\n");

	if (jdi->backlight) {
		jdi->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(jdi->backlight);
	}

	jdi->enabled = false;

	return 0;
}

static int jdi_panel_unprepare(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	int ret;

	if (!jdi->prepared)
		return 0;

	DRM_DEBUG("unprepare\n");

	ret = jdi_panel_off(jdi);
	if (ret) {
		dev_err(panel->dev, "failed to set panel off: %d\n", ret);
		return ret;
	}

	regulator_disable(jdi->supply);

	if (jdi->vcc_gpio)
		gpiod_set_value(jdi->vcc_gpio, 0);

	if (jdi->reset_gpio)
		gpiod_set_value(jdi->reset_gpio, 0);

	if (jdi->enable_gpio)
		gpiod_set_value(jdi->enable_gpio, 0);

	jdi->prepared = false;

	return 0;
}

static int jdi_panel_prepare(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);
	int ret;

	if (jdi->prepared)
		return 0;

	DRM_DEBUG("prepare\n");

	if (jdi->vcc_gpio) {
		gpiod_set_value(jdi->vcc_gpio, 0);
		msleep(5);
	}

	if (jdi->reset_gpio) {
		gpiod_set_value(jdi->reset_gpio, 0);
		msleep(5);
	}

	if (jdi->enable_gpio) {
		gpiod_set_value(jdi->enable_gpio, 0);
		msleep(5);
	}

	ret = regulator_enable(jdi->supply);
	if (ret < 0)
		return ret;

	msleep(20);

	if (jdi->vcc_gpio) {
		gpiod_set_value(jdi->vcc_gpio, 1);
		msleep(20);
	}

	if (jdi->reset_gpio) {
		gpiod_set_value(jdi->reset_gpio, 1);
		msleep(10);
	}

	if (jdi->enable_gpio) {
		gpiod_set_value(jdi->enable_gpio, 1);
		msleep(10);
	}

	msleep(150);

	ret = jdi_panel_init(jdi);
	if (ret) {
		dev_err(panel->dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	ret = jdi_panel_on(jdi);
	if (ret) {
		dev_err(panel->dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}

	jdi->prepared = true;

	return 0;

poweroff:
	regulator_disable(jdi->supply);
	if (jdi->reset_gpio)
		gpiod_set_value(jdi->reset_gpio, 0);
	if (jdi->enable_gpio)
		gpiod_set_value(jdi->enable_gpio, 0);
	if (jdi->vcc_gpio)
		gpiod_set_value(jdi->vcc_gpio, 0);

	return ret;
}

static int jdi_panel_enable(struct drm_panel *panel)
{
	struct jdi_panel *jdi = to_jdi_panel(panel);

	if (jdi->enabled)
		return 0;

	DRM_DEBUG("enable\n");

	if (jdi->backlight) {
		jdi->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(jdi->backlight);
	}

	jdi->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
		.clock = 1000000,
		.hdisplay = 1200,
		.hsync_start = 1200 + 48,
		.hsync_end = 1200 + 48 + 32,
		.htotal = 1200 + 48 + 32 + 60,
		.vdisplay = 1920,
		.vsync_start = 1920 + 3,
		.vsync_end = 1920 + 3 + 5,
		.vtotal = 1920 + 3 + 5 + 6,
		.vrefresh = 60,
		.flags = 0,
};

static int jdi_panel_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
				default_mode.hdisplay, default_mode.vdisplay,
				default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 95;
	panel->connector->display_info.height_mm = 151;

	return 1;
}

static const struct drm_panel_funcs jdi_panel_funcs = {
		.disable = jdi_panel_disable,
		.unprepare = jdi_panel_unprepare,
		.prepare = jdi_panel_prepare,
		.enable = jdi_panel_enable,
		.get_modes = jdi_panel_get_modes,
};

static const struct of_device_id jdi_of_match[] = {
		{ .compatible = "jdi,lt070me05000", },
		{ }
};
MODULE_DEVICE_TABLE(of, jdi_of_match);

static int jdi_panel_add(struct jdi_panel *jdi)
{
	struct device *dev= &jdi->dsi->dev;
	struct device_node *np;
	int ret;

	jdi->mode = &default_mode;

	jdi->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(jdi->supply))
		return PTR_ERR(jdi->supply);

	jdi->vcc_gpio = devm_gpiod_get(dev, "vcc", GPIOD_OUT_LOW);
	if (IS_ERR(jdi->vcc_gpio)) {
		dev_err(dev, "cannot get vcc-gpio %ld\n",
			PTR_ERR(jdi->vcc_gpio));
		jdi->vcc_gpio = NULL;
	} else {
		gpiod_direction_output(jdi->vcc_gpio, 0);
	}

	jdi->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(jdi->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(jdi->reset_gpio));
		jdi->reset_gpio = NULL;
	} else {
		gpiod_direction_output(jdi->reset_gpio, 0);
	}

	jdi->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(jdi->enable_gpio)) {
		dev_err(dev, "cannot get enable-gpio %ld\n",
			PTR_ERR(jdi->enable_gpio));
		jdi->enable_gpio = NULL;
	} else {
		gpiod_direction_output(jdi->enable_gpio, 0);
	}

	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (np) {
		jdi->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!jdi->backlight)
			return -EPROBE_DEFER;
	}

	drm_panel_init(&jdi->base);
	jdi->base.funcs = &jdi_panel_funcs;
	jdi->base.dev = &jdi->dsi->dev;

	ret = drm_panel_add(&jdi->base);
	if (ret < 0)
		goto put_backlight;

	return 0;

	put_backlight:
	if (jdi->backlight)
		put_device(&jdi->backlight->dev);

	return ret;
}

static void jdi_panel_del(struct jdi_panel *jdi)
{
	if (jdi->base.dev)
		drm_panel_remove(&jdi->base);

	if (jdi->backlight)
		put_device(&jdi->backlight->dev);
}

static int jdi_panel_probe(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi;
	int ret;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_EOT_PACKET;

	jdi = devm_kzalloc(&dsi->dev, sizeof(*jdi), GFP_KERNEL);
	if (!jdi) {
		return -ENOMEM;
	}

	mipi_dsi_set_drvdata(dsi, jdi);

	jdi->dsi = dsi;

	ret = jdi_panel_add(jdi);
	if (ret < 0) {
		return ret;
	}

	return mipi_dsi_attach(dsi);
}

static int jdi_panel_remove(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = jdi_panel_disable(&jdi->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_detach(&jdi->base);
	jdi_panel_del(jdi);

	return 0;
}

static void jdi_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct jdi_panel *jdi = mipi_dsi_get_drvdata(dsi);

	jdi_panel_disable(&jdi->base);
}

static struct mipi_dsi_driver jdi_panel_driver = {
	.driver = {
		.name = "panel-jdi-lt070me05000",
		.of_match_table = jdi_of_match,
	},
	.probe = jdi_panel_probe,
	.remove = jdi_panel_remove,
	.shutdown = jdi_panel_shutdown,
};
module_mipi_dsi_driver(jdi_panel_driver);

MODULE_AUTHOR("Sumit Semwal <sumit.semwal@linaro.org>");
MODULE_AUTHOR("Vinay Simha BN <simhavcs@gmail.com>");
MODULE_DESCRIPTION("JDI WUXGA LT070ME05000 DSI command mode panel driver");
MODULE_LICENSE("GPL v2");
