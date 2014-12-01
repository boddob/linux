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
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/qpnp/pin.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/qpnp/pwm.h>
#include <linux/err.h>

#include <video/mipi_display.h>
#include <drm_mipi_dsi.h>
#include "dsi.h"

#include "panel/panel_init.h"

#define VDD_MIN_UV		3000000	/* uV units */
#define VDD_MAX_UV		3000000	/* uV units */
#define VDD_UA_ON_LOAD		150000	/* uA units */
#define VDD_UA_OFF_LOAD		100	/* uA units */

#define VDDIO_MIN_UV		1800000	/* uV units */
#define VDDIO_MAX_UV		1800000	/* uV units */
#define VDDIO_UA_ON_LOAD	100000	/* uA units */
#define VDDIO_UA_OFF_LOAD	100	/* uA units */

DEFINE_LED_TRIGGER(bl_led_trigger);

static inline struct msm_dsi_panel *to_msm_dsi_panel(struct drm_panel *panel)
{
	return container_of(panel, struct msm_dsi_panel, base);
};

static int dsi_panel_regulator_enable(struct msm_dsi_panel *msm_panel)
{
	struct regulator_bulk_data *supplies = msm_panel->supplies;
	int ret;

	ret = regulator_set_voltage(
			supplies[MSM_DSI_PANEL_SUPPLY_VDD].consumer,
			VDD_MIN_UV, VDD_MAX_UV);
	if (ret) {
		pr_err("%s:vdd set_voltage failed, %d\n", __func__, ret);
		goto vdd_set_fail;
	}

	ret = regulator_set_optimum_mode(
			supplies[MSM_DSI_PANEL_SUPPLY_VDD].consumer,
			VDD_UA_ON_LOAD);
	if (ret < 0) {
		pr_err("%s: vdd set regulator mode failed.\n", __func__);
		goto vdd_set_fail;
	}

	ret = regulator_set_voltage(
			supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].consumer,
			VDDIO_MIN_UV, VDDIO_MAX_UV);
	if (ret) {
		pr_err("%s:vddio set_voltage failed, %d\n", __func__, ret);
		goto vddio_set_fail;
	}

	ret = regulator_set_optimum_mode(
			supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].consumer,
			VDDIO_UA_ON_LOAD);
	if (ret < 0) {
		pr_err("%s: vddio set regulator mode failed.\n", __func__);
		goto vddio_set_fail;
	}

	ret = regulator_bulk_enable(MSM_DSI_PANEL_SUPPLY_NUM, supplies);
	if (ret) {
		pr_err("%s:Failed to enable regulator, %d\n", __func__, ret);
		goto enable_fail;
	}

	return 0;

enable_fail:
	regulator_set_optimum_mode(
			supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].consumer,
			VDDIO_UA_OFF_LOAD);
vddio_set_fail:
	regulator_set_optimum_mode(
			supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].consumer,
			VDD_UA_OFF_LOAD);
vdd_set_fail:
	return ret;
}

static void dsi_panel_regulator_disable(struct msm_dsi_panel *msm_panel)
{
	struct regulator_bulk_data *supplies = msm_panel->supplies;
	regulator_bulk_disable(MSM_DSI_PANEL_SUPPLY_NUM, supplies);
	regulator_set_optimum_mode(
			supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].consumer,
			VDDIO_UA_OFF_LOAD);
	regulator_set_optimum_mode(
			supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].consumer,
			VDD_UA_OFF_LOAD);
}

static int dsi_panel_bklt_pwm(struct msm_dsi_panel *msm_panel, u32 level)
{
	int ret;
	u32 duty;

	if (msm_panel->pwm_bl == NULL) {
		pr_err("%s: no PWM\n", __func__);
		return -ENODEV;
	}

	if (msm_panel->bl_level) {
		pwm_disable(msm_panel->pwm_bl);
		if (level == 0)
			return 0;
	}

	duty = mult_frac(level, msm_panel->pwm_period, msm_panel->bl_max);

	DBG("level=%d duty=%d\n", level, duty);

	ret = pwm_config(msm_panel->pwm_bl,
			duty*1000, msm_panel->pwm_period*1000);
	if (ret) {
		pr_err("%s: pwm_config() failed err=%d.\n", __func__, ret);
		return ret;
	}

	ret = pwm_enable(msm_panel->pwm_bl);
	if (ret) {
		pr_err("%s: pwm_enable() failed err=%d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int dsi_panel_request_gpios(struct msm_dsi_panel *msm_panel)
{
	struct device *dev = msm_panel->base.dev;
	int ret;

	/* For dual dsi case, 2 panels share the gpios,
	 * which are defined in the first panel only.
	 */
	if (msm_panel->id == DSI_PANEL_2)
		return 0;

	msm_panel->disp_en_gpio = devm_gpiod_get(dev, "disp-enable");
	if (IS_ERR(msm_panel->disp_en_gpio)) {
		ret = PTR_ERR(msm_panel->disp_en_gpio);
		msm_panel->disp_en_gpio = NULL;
		pr_err("%s: cannot get disp-enable-gpios, %d\n", __func__, ret);
		goto exit;
	}

	ret = gpiod_direction_output(msm_panel->disp_en_gpio, 0);
	if (ret) {
		pr_err("%s: cannot set direction to disp-enable, %d\n",
			__func__, ret);
		goto exit;
	}

	msm_panel->bklt_en_gpio = devm_gpiod_get(dev, "bkl-en");
	if (IS_ERR(msm_panel->bklt_en_gpio)) {
		msm_panel->bklt_en_gpio = NULL;
		pr_info("%s: cannot get bkl-en-gpios, %d\n", __func__, ret);
	}

	if (msm_panel->mode == MSM_DSI_CMD_MODE) {
		msm_panel->disp_te_gpio = devm_gpiod_get(dev, "disp-te");
		if (IS_ERR(msm_panel->disp_te_gpio)) {
			msm_panel->disp_te_gpio = NULL;
			pr_warn("%s:%d, Disp_te gpio not specified\n",
			       __func__, __LINE__);
		}

		if (msm_panel->disp_te_gpio) {
			ret = gpiod_direction_input(msm_panel->disp_te_gpio);
			if (ret) {
				pr_err("%s: unable to set dir to disp-te, %d\n",
					__func__, ret);
				goto exit;
			}
		}
	}

	if (msm_panel->desc.rst_seq_len) {
		msm_panel->rst_gpio = devm_gpiod_get(dev, "reset");
		if (IS_ERR(msm_panel->rst_gpio)) {
			ret = PTR_ERR(msm_panel->rst_gpio);
			msm_panel->rst_gpio = NULL;
			pr_err("%s: cannot get reset-gpios, %d\n",
							__func__, ret);
			goto exit;
		}

		ret = gpiod_direction_output(msm_panel->rst_gpio, 0);
		if (ret) {
			pr_err("%s: unable to set dir to reset, %d\n",
				__func__, ret);
			goto exit;
		}
	}

	if (msm_panel->desc.mode_gpio_state != MODE_GPIO_NOT_VALID) {
		msm_panel->mode_gpio = devm_gpiod_get(dev, "mode");
		if (IS_ERR(msm_panel->mode_gpio)) {
			ret = PTR_ERR(msm_panel->mode_gpio);
			msm_panel->mode_gpio = NULL;
			pr_err("%s: cannot get mode-gpios, %d\n",
							__func__, ret);
			goto exit;
		}

		if (msm_panel->desc.mode_gpio_state == MODE_GPIO_HIGH)
			ret = gpiod_direction_output(msm_panel->mode_gpio, 1);
		else if (msm_panel->desc.mode_gpio_state == MODE_GPIO_LOW)
			ret = gpiod_direction_output(msm_panel->mode_gpio, 0);
		if (ret) {
			pr_err("%s: unable to set dir to mode, %d\n",
				__func__, ret);
			goto exit;
		}
	}

exit:
	return ret;
}

static int dsi_panel_reset(struct msm_dsi_panel *msm_panel, int enable)
{
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	int i;

	if (!msm_panel->disp_en_gpio) {
		DBG("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return 0;
	}

	DBG("%s: enable = %d\n", __func__, enable);

	if (enable) {
		gpiod_set_value(msm_panel->disp_en_gpio, 1);

		for (i = 0; i < desc->rst_seq_len; ++i) {
			gpiod_set_value(msm_panel->rst_gpio,
				desc->rst_seq[i]);
			if (desc->rst_seq[++i])
				dsi_sleep(desc->rst_seq[i] * 1000);
		}

		if (msm_panel->bklt_en_gpio)
			gpiod_set_value(msm_panel->bklt_en_gpio, 1);

	} else {
		if (msm_panel->bklt_en_gpio)
			gpiod_set_value(msm_panel->bklt_en_gpio, 0);

		gpiod_set_value(msm_panel->disp_en_gpio, 0);
	}

	return 0;
}

static int dsi_panel_parse_dt(struct device_node *np,
			struct msm_dsi_panel *msm_panel)
{
	u32 tmp;
	int ret;
	const char *data;
	static const char *id;

	id = of_get_property(np,
		"qcom,mdss-dsi-panel-destination", NULL);

	if (id) {
		if (strlen(id) != 9) {
			pr_err("%s: Unknown id specified\n", __func__);
			return -EINVAL;
		}
		if (!strcmp(id, "display_1"))
			msm_panel->id = DSI_PANEL_1;
		else if (!strcmp(id, "display_2"))
			msm_panel->id = DSI_PANEL_2;
		else {
			pr_err("%s: incorrect id.\n", __func__);
			return -EINVAL;
		}
	} else {
		pr_err("%s: id not specified.\n", __func__);
		return -EINVAL;
	}

	msm_panel->bklt_ctrl_mode = UNKNOWN_MODE;
	data = of_get_property(np, "qcom,mdss-dsi-bl-pmic-control-type", NULL);
	if (data) {
		if (!strncmp(data, "bl_ctrl_wled", 12)) {
			led_trigger_register_simple("bkl-trigger",
				&bl_led_trigger);
			DBG("SUCCESS-> WLED TRIGGER register");
			msm_panel->bklt_ctrl_mode = BL_WLED;
		} else if (!strncmp(data, "bl_ctrl_pwm", 11)) {
			msm_panel->bklt_ctrl_mode = BL_PWM;
			ret = of_property_read_u32(np,
				"qcom,mdss-dsi-bl-pmic-pwm-frequency", &tmp);
			if (ret) {
				pr_err("%s:%d, Error, panel pwm_period\n",
						__func__, __LINE__);
				return ret;
			}
			msm_panel->pwm_period = tmp;
			ret = of_property_read_u32(np,
				"qcom,mdss-dsi-bl-pmic-bank-select", &tmp);
			if (ret) {
				pr_err("%s:%d, Error, dsi lpg channel\n",
						__func__, __LINE__);
				return ret;
			}
			msm_panel->pwm_bl = pwm_request(tmp, "lcd-bklt");
			if (IS_ERR_OR_NULL(msm_panel->pwm_bl)) {
				pr_err("%s: Error: pwm request failed",
						__func__);
				return -EINVAL;
			}
		} else if (!strncmp(data, "bl_ctrl_dcs", 11)) {
			msm_panel->bklt_ctrl_mode = BL_DCS_CMD;
		}
	}
	ret = of_property_read_u32(np, "qcom,mdss-dsi-bl-min-level", &tmp);
	msm_panel->bl_min = (!ret ? tmp : 0);
	ret = of_property_read_u32(np, "qcom,mdss-dsi-bl-max-level", &tmp);
	msm_panel->bl_max = (!ret ? tmp : 255);

	return 0;
}

int msm_dsi_panel_bl_ctrl(struct drm_panel *panel, u32 bl_level)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);
	int ret = 0;

	mutex_lock(&msm_panel->bl_mutex);
	bl_level = clamp_t(u32, bl_level, msm_panel->bl_min, msm_panel->bl_max);

	if (bl_level == msm_panel->bl_level) {
		mutex_unlock(&msm_panel->bl_mutex);
		return 0;
	}

	switch (msm_panel->bklt_ctrl_mode) {
	case BL_WLED:
		led_trigger_event(bl_led_trigger, bl_level);
		break;
	case BL_PWM:
		ret = dsi_panel_bklt_pwm(msm_panel, bl_level);
		break;
	case BL_DCS_CMD:
	{
		struct mipi_dsi_device *dsi =
				to_mipi_dsi_device(msm_panel->base.dev);
		u8 level = bl_level;
		ret = mipi_dsi_dcs_write(dsi, 0x51, &level, 1);
		if (ret == 1)
			ret = 0;
		else
			ret = -EINVAL;
		break;
	}
	default:
		DBG("%s: Unknown bl_ctrl configuration\n", __func__);
		ret = -EINVAL;
		break;
	}

	if (!ret)
		msm_panel->bl_level = bl_level;

	mutex_unlock(&msm_panel->bl_mutex);
	return ret;
}

struct msm_cmd_te_cfg *msm_dsi_panel_get_te_info(struct drm_panel *panel)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);

	return &msm_panel->desc.cmd_cfg.te;
}

enum msm_dsi_mode msm_dsi_panel_get_op_mode(struct drm_panel *panel)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);
	return msm_panel->mode;
}

static int msm_dsi_panel_disable(struct drm_panel *panel)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;

	if (desc->ops && desc->ops->off)
		return desc->ops->off(msm_panel);

	return 0;
}

static int msm_dsi_panel_unprepare(struct drm_panel *panel)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);

	dsi_panel_reset(msm_panel, 0);
	dsi_panel_regulator_disable(msm_panel);

	return 0;
}

static int msm_dsi_panel_prepare(struct drm_panel *panel)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);
	int ret;

	ret = dsi_panel_regulator_enable(msm_panel);
	if (ret)
		return ret;

	if (!msm_panel->desc.lp11_init) {
		ret = dsi_panel_reset(msm_panel, 1);
		if (ret) {
			pr_err("%s: Panel reset failed. rc=%d\n",
					__func__, ret);
			dsi_panel_regulator_disable(msm_panel);
			return ret;
		}
	}

	return 0;
}

static int msm_dsi_panel_enable(struct drm_panel *panel)
{
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	int ret = 0;

	/*
	 * Issue hardware reset line after enabling the DSI clocks and
	 * data lanes for LP11 init
	 */
	if (msm_panel->desc.lp11_init) {
		ret = dsi_panel_reset(msm_panel, 1);
		if (ret)
			return ret;
	}

	if (msm_panel->desc.init_delay)
		dsi_sleep(msm_panel->desc.init_delay);

	if (desc->ops && desc->ops->on) {
		ret = desc->ops->on(msm_panel);
		if (ret && msm_panel->desc.lp11_init)
			dsi_panel_reset(msm_panel, 0);
	}

	return ret;
}

static int msm_dsi_panel_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct msm_dsi_panel *msm_panel = to_msm_dsi_panel(panel);
	struct msm_dsi_panel_desc *desc = &msm_panel->desc;
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_ERROR("failed to create a new display mode\n");
		return 0;
	}

	DBG("%s: %d,%d,%d,%d,%d,%d,%d,%d\n", __func__, desc->vm.hactive,
		desc->vm.hback_porch, desc->vm.hfront_porch,
		desc->vm.hsync_len, desc->vm.vactive,
		desc->vm.vback_porch, desc->vm.vfront_porch,
		desc->vm.vsync_len);

	drm_display_mode_from_videomode(&desc->vm, mode);
	mode->width_mm = desc->physical_width;
	mode->height_mm = desc->physical_height;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs msm_dsi_panel_drm_funcs = {
	.disable = msm_dsi_panel_disable,
	.unprepare = msm_dsi_panel_unprepare,
	.prepare = msm_dsi_panel_prepare,
	.enable = msm_dsi_panel_enable,
	.get_modes = msm_dsi_panel_get_modes,
};

static struct of_device_id msm_dsi_panel_of_match[] = {
	{ .compatible = "qcom,dsi_jdi_qhd_dual_cmd",
	 .data = jdi_qhd_dual_cmd_panel_init },
	{ .compatible = "qcom,dsi_jdi_qhd_dual_video",
	 .data = jdi_qhd_dual_video_panel_init },
	{ .compatible = "qcom,dsi_jdi_1080_video",
	 .data = jdi_1080_video_panel_init },
	{ }
};

static int msm_dsi_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct msm_dsi_panel *msm_panel;
	struct msm_dsi_panel_desc *desc;
	const struct of_device_id *id;
	int ret;

	msm_panel = devm_kzalloc(dev, sizeof(*msm_panel), GFP_KERNEL);
	if (!msm_panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, msm_panel);

	desc = &msm_panel->desc;

	id = of_match_node(msm_dsi_panel_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	msm_panel->init = id->data;

	if (msm_panel->init) {
		ret = msm_panel->init(desc);
		if (ret < 0)
			return ret;
	}

	ret = dsi_panel_parse_dt(dsi->dev.of_node, msm_panel);
	if (ret < 0)
		return ret;

	msm_panel->roi_x = 0;
	msm_panel->roi_y = 0;
	msm_panel->roi_w = desc->vm.hactive;
	msm_panel->roi_h = desc->vm.vactive;
	msm_panel->frame_rate = desc->frame_rate;
	if ((desc->mode != MSM_DSI_VIDEO_MODE) &&
		(desc->mode != MSM_DSI_CMD_MODE)) {
		pr_err("%s: panel mode undefined, %d\n", __func__, desc->mode);
		return -EINVAL;
	}
	msm_panel->mode = desc->mode;

	dsi->lanes = desc->lane_map.lane_num;
	/* Do not use it because it only supports video mode */
	dsi->format = -1;

	msm_panel->base.dev = dev;
	msm_panel->base.funcs = &msm_dsi_panel_drm_funcs;

	msm_panel->supplies[MSM_DSI_PANEL_SUPPLY_VDD].supply = "vdd";
	msm_panel->supplies[MSM_DSI_PANEL_SUPPLY_VDDIO].supply = "vddio";
	ret = devm_regulator_bulk_get(dev, MSM_DSI_PANEL_SUPPLY_NUM,
					msm_panel->supplies);
	if (ret)
		return ret;

	ret = dsi_panel_request_gpios(msm_panel);
	if (ret)
		return ret;

	drm_panel_init(&msm_panel->base);

	ret = drm_panel_add(&msm_panel->base);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&msm_panel->base);

	mutex_init(&msm_panel->bl_mutex);

	return ret;
}

static int msm_dsi_panel_remove(struct mipi_dsi_device *dsi)
{
	struct msm_dsi_panel *msm_panel = mipi_dsi_get_drvdata(dsi);

	mutex_destroy(&msm_panel->bl_mutex);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&msm_panel->base);

	return 0;
}

static struct mipi_dsi_driver msm_dsi_panel_driver = {
	.probe = msm_dsi_panel_probe,
	.remove = msm_dsi_panel_remove,
	.driver = {
		.name = "msm_dsi_panel",
		.owner = THIS_MODULE,
		.of_match_table = msm_dsi_panel_of_match,
	},
};
module_mipi_dsi_driver(msm_dsi_panel_driver);


