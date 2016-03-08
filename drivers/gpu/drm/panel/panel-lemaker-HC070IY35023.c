/*
 * Copyright (C) 2016 LeMaker
 *
 * Based on AUO panel driver 
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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>
#include <linux/gpio/consumer.h>


/*
 * When power is turned off to this panel a minimum off time of 500ms has to be
 * observed before powering back on as there's no external reset pin. Keep
 * track of earliest wakeup time and delay subsequent prepare call accordingly
 */
#define MIN_POFF_MS (500)

#define REGFLAG_DELAY         	0XFFE
#define REGFLAG_END_OF_TABLE    0xFFF   // END OF REGISTERS MARKER


struct lemaker_nt_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct backlight_device *backlight;
	struct regulator *supply;

	bool prepared;
	bool enabled;

	ktime_t earliest_wake;

	const struct drm_display_mode *mode;
	struct gpio_desc *gpio_mipisw;
	struct gpio_desc *gpio_pwr_en;
	struct gpio_desc *gpio_bl_en;
	struct gpio_desc *gpio_pwm;
};


struct LCM_setting_table 
{    
	unsigned cmd;    
	unsigned char count;    
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting[] = 
{

    {0xFF,4,{0xAA,0x55,0xA5,0x80}},//========== Internal setting ==========

	{0x6F,2,{0x11,0x00}},// MIPI related Timing Setting
	{0xF7,2,{0x20,0x00}},

	{0x6F,1,{0x06}},//  Improve ESD option
	{0xF7,1,{0xA0}},
	{0x6F,1,{0x19}},
	{0xF7,1,{0x12}},
	{0xF4,1,{0x03}},

	{0x6F,1,{0x08}},// Vcom floating
	{0xFA,1,{0x40}},
	{0x6F,1,{0x11}},
	{0xF3,1,{0x01}},

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x00}},//========== page0 relative ==========
	{0xC8,1,{0x80}},

	{0xB1,2,{0x6C,0x01}},// Set WXGA resolution

	{0xB6,1,{0x08}},// Set source output hold time

	{0x6F,1,{0x02}},//EQ control function
	{0xB8,1,{0x08}},

	{0xBB,2,{0x54,0x54}},// Set bias current for GOP and SOP

	{0xBC,2,{0x05,0x05}},// Inversion setting 

	{0xC7,1,{0x01}},// zigzag setting

	{0xBD,5,{0x02,0xB0,0x0C,0x0A,0x00}},// DSP Timing Settings update for BIST

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x01}},//========== page1 relative ==========

	{0xB0,2,{0x05,0x05}},// Setting AVDD, AVEE clamp
	{0xB1,2,{0x05,0x05}},

	{0xBC,2,{0x3A,0x01}},// VGMP, VGMN, VGSP, VGSN setting
	{0xBD,2,{0x3E,0x01}},

	{0xCA,1,{0x00}},// gate signal control

	{0xC0,1,{0x04}},// power IC control

	{0xB2,2,{0x00,0x00}},// VCL SET -2.5V

	{0xBE,1,{0x80}},// VCOM = -1.888V

	{0xB3,2,{0x19,0x19}},// Setting VGH=15V, VGL=-11V
	{0xB4,2,{0x12,0x12}},

	{0xB9,2,{0x24,0x24}},// power control for VGH, VGL
	{0xBA,2,{0x14,0x14}},

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x02}},//========== page2 relative ==========

	{0xEE,1,{0x01}},//gamma setting
	{0xEF,4,{0x09,0x06,0x15,0x18}},//Gradient Control for Gamma Voltage

	{0xB0,6,{0x00,0x00,0x00,0x08,0x00,0x17}},
	{0x6F,1,{0x06}},
	{0xB0,6,{0x00,0x25,0x00,0x30,0x00,0x45}},
	{0x6F,1,{0x0C}},
	{0xB0,4,{0x00,0x56,0x00,0x7A}},                                                                                                                                            
	{0xB1,6,{0x00,0xA3,0x00,0xE7,0x01,0x20}},
	{0x6F,1,{0x06}},
	{0xB1,6,{0x01,0x7A,0x01,0xC2,0x01,0xC5}},
	{0x6F,1,{0x0C}},
	{0xB1,4,{0x02,0x06,0x02,0x5F}},
	{0xB2,6,{0x02,0x92,0x02,0xD0,0x02,0xFC}},
	{0x6F,1,{0x06}},
	{0xB2,6,{0x03,0x35,0x03,0x5D,0x03,0x8B}},
	{0x6F,1,{0x0C}},
	{0xB2,4,{0x03,0xA2,0x03,0xBF}},
	{0xB3,4,{0x03,0xD2,0x03,0xFF}},

	//========== GOA relative ==========
	{0xF0,5,{0x55,0xAA,0x52,0x08,0x06}},// PAGE6 : GOUT Mapping, VGLO select
	{0xB0,2,{0x00,0x17}},
	{0xB1,2,{0x16,0x15}},
	{0xB2,2,{0x14,0x13}},
	{0xB3,2,{0x12,0x11}},
	{0xB4,2,{0x10,0x2D}},
	{0xB5,2,{0x01,0x08}},
	{0xB6,2,{0x09,0x31}},
	{0xB7,2,{0x31,0x31}},
	{0xB8,2,{0x31,0x31}},
	{0xB9,2,{0x31,0x31}},
	{0xBA,2,{0x31,0x31}},
	{0xBB,2,{0x31,0x31}},
	{0xBC,2,{0x31,0x31}},
	{0xBD,2,{0x31,0x09}},
	{0xBE,2,{0x08,0x01}},
	{0xBF,2,{0x2D,0x10}},
	{0xC0,2,{0x11,0x12}},
	{0xC1,2,{0x13,0x14}},
	{0xC2,2,{0x15,0x16}},
	{0xC3,2,{0x17,0x00}},
	{0xE5,2,{0x31,0x31}},
	{0xC4,2,{0x00,0x17}},
	{0xC5,2,{0x16,0x15}},
	{0xC6,2,{0x14,0x13}},
	{0xC7,2,{0x12,0x11}},
	{0xC8,2,{0x10,0x2D}},
	{0xC9,2,{0x01,0x08}},
	{0xCA,2,{0x09,0x31}},
	{0xCB,2,{0x31,0x31}},
	{0xCC,2,{0x31,0x31}},
	{0xCD,2,{0x31,0x31}},
	{0xCE,2,{0x31,0x31}},
	{0xCF,2,{0x31,0x31}},
	{0xD0,2,{0x31,0x31}},
	{0xD1,2,{0x31,0x09}},
	{0xD2,2,{0x08,0x01}},
	{0xD3,2,{0x2D,0x10}},
	{0xD4,2,{0x11,0x12}},
	{0xD5,2,{0x13,0x14}},
	{0xD6,2,{0x15,0x16}},
	{0xD7,2,{0x17,0x00}},
	{0xE6,2,{0x31,0x31}},
	{0xD8,5,{0x00,0x00,0x00,0x00,0x00}},//VGL level select
	{0xD9,5,{0x00,0x00,0x00,0x00,0x00}},
	{0xE7,1,{0x00}},

	// PAGE3 :
	{0xF0,5,{0x55,0xAA,0x52,0x08,0x03}},//gate timing control
	{0xB0,2,{0x20,0x00}},
	{0xB1,2,{0x20,0x00}},
	{0xB2,5,{0x05,0x00,0x42,0x00,0x00}},
	{0xB6,5,{0x05,0x00,0x42,0x00,0x00}},
	{0xBA,5,{0x53,0x00,0x42,0x00,0x00}},
	{0xBB,5,{0x53,0x00,0x42,0x00,0x00}},
	{0xC4,1,{0x40}},

	// gate CLK EQ
	// gate STV EQ

	// PAGE5 :
	{0xF0,5,{0x55,0xAA,0x52,0x08,0x05}},
	{0xB0,2,{0x17,0x06}},
	{0xB8,1,{0x00}},
	{0xBD,5,{0x03,0x01,0x01,0x00,0x01}},
	{0xB1,2,{0x17,0x06}},
	{0xB9,2,{0x00,0x01}},
	{0xB2,2,{0x17,0x06}},
	{0xBA,2,{0x00,0x01}},
	{0xB3,2,{0x17,0x06}},
	{0xBB,2,{0x0A,0x00}},
	{0xB4,2,{0x17,0x06}},
	{0xB5,2,{0x17,0x06}},
	{0xB6,2,{0x14,0x03}},
	{0xB7,2,{0x00,0x00}},
	{0xBC,2,{0x02,0x01}},
	{0xC0,1,{0x05}},
	{0xC4,1,{0xA5}},
	{0xC8,2,{0x03,0x30}},
	{0xC9,2,{0x03,0x51}},
	{0xD1,5,{0x00,0x05,0x03,0x00,0x00}},
	{0xD2,5,{0x00,0x05,0x09,0x00,0x00}},
	{0xE5,1,{0x02}},
	{0xE6,1,{0x02}},
	{0xE7,1,{0x02}},
	{0xE9,1,{0x02}},
	{0xED,1,{0x33}},
	
	/* bist test mode
	{0xF0,5,{0x55,0xAA,0x52,0x08,0x00}},
	{0xEF,2,{0x07,0xFF}},
	{0xEE,4,{0x87,0x78,0x02,0x40}},
	*/

	{0x11,0,{0x00}},
	{REGFLAG_DELAY, 120, {}},
	{0x29,0,{0x00}},
	{REGFLAG_DELAY, 20, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};



static void push_table(struct lemaker_nt_panel *lemaker_nt, struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;
    for(i = 0; i < count; i++) {	
        unsigned cmd;
        cmd = table[i].cmd;
        switch (cmd) {
            case REGFLAG_DELAY :
                msleep(table[i].count);
                break;
            case REGFLAG_END_OF_TABLE :
                break;
            default:
		mipi_dsi_dcs_write(lemaker_nt->dsi, cmd, table[i].para_list, table[i].count);
       	}
    }
	
}



static inline struct lemaker_nt_panel *to_lemaker_nt_panel(struct drm_panel *panel)
{
	return container_of(panel, struct lemaker_nt_panel, base);
}

static int lemaker_nt_panel_on(struct lemaker_nt_panel *lemaker_nt)
{
	struct mipi_dsi_device *dsi = lemaker_nt->dsi;
	int ret;

	ret = mipi_dsi_turn_on_peripheral(dsi);
	if (ret < 0)
		return ret;

	return 0;
}

static int lemaker_nt_panel_disable(struct drm_panel *panel)
{
	struct lemaker_nt_panel *lemaker_nt = to_lemaker_nt_panel(panel);

	if (!lemaker_nt->enabled)
		return 0;

	mipi_dsi_shutdown_peripheral(lemaker_nt->dsi);

	if (lemaker_nt->backlight) {
		lemaker_nt->backlight->props.power = FB_BLANK_POWERDOWN;
		lemaker_nt->backlight->props.state |= BL_CORE_FBBLANK;
		backlight_update_status(lemaker_nt->backlight);
	}

	lemaker_nt->enabled = false;

	return 0;
}

static int lemaker_nt_panel_unprepare(struct drm_panel *panel)
{
	struct lemaker_nt_panel *lemaker_nt = to_lemaker_nt_panel(panel);

	if (!lemaker_nt->prepared)
		return 0;

	regulator_disable(lemaker_nt->supply);
	lemaker_nt->earliest_wake = ktime_add_ms(ktime_get_real(), MIN_POFF_MS);
	lemaker_nt->prepared = false;

	return 0;
}

static int lemaker_nt_panel_prepare(struct drm_panel *panel)
{
	struct lemaker_nt_panel *lemaker_nt = to_lemaker_nt_panel(panel);
	int ret;
	s64 enablewait;

	if (lemaker_nt->prepared)
		return 0;

	/*
	 * If the user re-enabled the panel before the required off-time then
	 * we need to wait the remaining period before re-enabling regulator
	 */
	enablewait = ktime_ms_delta(lemaker_nt->earliest_wake, ktime_get_real());

	/* Sanity check, this should never happen */
	if (enablewait > MIN_POFF_MS)
		enablewait = MIN_POFF_MS;

	if (enablewait > 0)
		msleep(enablewait);

	ret = regulator_enable(lemaker_nt->supply);
	if (ret < 0)
		return ret;

	/*
	 * A minimum delay of 250ms is required after power-up until commands
	 * can be sent
	 */
	msleep(250);

#if 00
	ret = lemaker_nt_panel_on(lemaker_nt);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}
#endif

	lemaker_nt->prepared = true;

	push_table(lemaker_nt, lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);

	return 0;

poweroff:
	regulator_disable(lemaker_nt->supply);

	return ret;
}

static int lemaker_nt_panel_enable(struct drm_panel *panel)
{
	struct lemaker_nt_panel *lemaker_nt = to_lemaker_nt_panel(panel);

	if (lemaker_nt->enabled)
		return 0;

	if (lemaker_nt->backlight) {
		lemaker_nt->backlight->props.power = FB_BLANK_UNBLANK;
		lemaker_nt->backlight->props.state &= ~BL_CORE_FBBLANK;
		backlight_update_status(lemaker_nt->backlight);
	}

	msleep(200);
	gpiod_set_value(lemaker_nt->gpio_bl_en, 1);
	gpiod_set_value(lemaker_nt->gpio_pwm, 1);

	lemaker_nt->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 66800,

	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 4,
	.htotal = 800 + 40 + 4 + 40,

	.vdisplay = 1280,
	.vsync_start = 1280 + 10,
	.vsync_end = 1280 + 10 + 4,
	.vtotal = 1280 + 10 + 4 + 12,
};

static int lemaker_nt_panel_get_modes(struct drm_panel *panel)
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

	panel->connector->display_info.width_mm = 94;
	panel->connector->display_info.height_mm = 150;

	return 1;
}

static const struct drm_panel_funcs lemaker_nt_panel_funcs = {
	.disable = lemaker_nt_panel_disable,
	.unprepare = lemaker_nt_panel_unprepare,
	.prepare = lemaker_nt_panel_prepare,
	.enable = lemaker_nt_panel_enable,
	.get_modes = lemaker_nt_panel_get_modes,
};

static const struct of_device_id lemaker_nt_of_match[] = {
	{ .compatible = "innolux,n070icn-pb1", },
	{ }
};
MODULE_DEVICE_TABLE(of, lemaker_nt_of_match);

static int lemaker_nt_panel_add(struct lemaker_nt_panel *lemaker_nt)
{
	struct device *dev = &lemaker_nt->dsi->dev;
	struct device_node *np;
	int ret;

	lemaker_nt->mode = &default_mode;

	lemaker_nt->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(lemaker_nt->supply))
		return PTR_ERR(lemaker_nt->supply);

	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (np) {
		lemaker_nt->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!lemaker_nt->backlight)
			return -EPROBE_DEFER;
	}

	drm_panel_init(&lemaker_nt->base);
	lemaker_nt->base.funcs = &lemaker_nt_panel_funcs;
	lemaker_nt->base.dev = &lemaker_nt->dsi->dev;

	ret = drm_panel_add(&lemaker_nt->base);
	if (ret < 0)
		goto put_backlight;
	printk("..................................................BBB..................**********************\n");
	return 0;

put_backlight:
	if (lemaker_nt->backlight)
		put_device(&lemaker_nt->backlight->dev);

	return ret;
}

static void lemaker_nt_panel_del(struct lemaker_nt_panel *lemaker_nt)
{
	if (lemaker_nt->base.dev)
		drm_panel_remove(&lemaker_nt->base);

	if (lemaker_nt->backlight)
		put_device(&lemaker_nt->backlight->dev);
}

static int lemaker_nt_panel_probe(struct mipi_dsi_device *dsi)
{
	struct lemaker_nt_panel *lemaker_nt;
	int ret;

	DRM_INFO("lemaker_nt_panel_probe enter\n");

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_VIDEO_HSE |
			MIPI_DSI_CLOCK_NON_CONTINUOUS |
			MIPI_DSI_MODE_LPM;
	lemaker_nt = devm_kzalloc(&dsi->dev, sizeof(*lemaker_nt), GFP_KERNEL);
	if (!lemaker_nt)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, lemaker_nt);
	
	lemaker_nt->dsi = dsi;

	ret = lemaker_nt_panel_add(lemaker_nt);
	if (ret < 0)
		return ret;

	lemaker_nt->gpio_mipisw = devm_gpiod_get_optional(&dsi->dev, "mipisw", GPIOD_OUT_HIGH);
	if (IS_ERR(lemaker_nt->gpio_mipisw))
		return PTR_ERR(lemaker_nt->gpio_mipisw);

	lemaker_nt->gpio_pwr_en = devm_gpiod_get_optional(&dsi->dev, "pwr-en", GPIOD_OUT_HIGH);
	if (IS_ERR(lemaker_nt->gpio_pwr_en))
		return PTR_ERR(lemaker_nt->gpio_pwr_en);

	lemaker_nt->gpio_bl_en = devm_gpiod_get_optional(&dsi->dev, "bl-en", GPIOD_OUT_LOW);
	if (IS_ERR(lemaker_nt->gpio_bl_en))
		return PTR_ERR(lemaker_nt->gpio_bl_en);

	lemaker_nt->gpio_pwm = devm_gpiod_get_optional(&dsi->dev, "pwm", GPIOD_OUT_LOW);
	if (IS_ERR(lemaker_nt->gpio_pwm))
		return PTR_ERR(lemaker_nt->gpio_pwm);

	if (lemaker_nt->gpio_mipisw) {
		mdelay(5);	
		gpiod_set_value_cansleep(lemaker_nt->gpio_mipisw, 1);
	}

	DRM_INFO("lemaker_nt_panel_probe exit\n");
	return mipi_dsi_attach(dsi);
}

static int lemaker_nt_panel_remove(struct mipi_dsi_device *dsi)
{
	struct lemaker_nt_panel *lemaker_nt = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = lemaker_nt_panel_disable(&lemaker_nt->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_detach(&lemaker_nt->base);
	lemaker_nt_panel_del(lemaker_nt);

	return 0;
}

static void lemaker_nt_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct lemaker_nt_panel *lemaker_nt = mipi_dsi_get_drvdata(dsi);

	lemaker_nt_panel_disable(&lemaker_nt->base);
}

static struct mipi_dsi_driver lemaker_nt_panel_driver = {
	.driver = {
		.name = "panel-innolux-n070icn-pb1",
		.of_match_table = lemaker_nt_of_match,
	},
	.probe = lemaker_nt_panel_probe,
	.remove = lemaker_nt_panel_remove,
	.shutdown = lemaker_nt_panel_shutdown,
};
module_mipi_dsi_driver(lemaker_nt_panel_driver);

MODULE_AUTHOR("support@lemaker.org");
MODULE_DESCRIPTION("INNOLUX N070ICN-PB1 (800x1280) video mode panel driver");
MODULE_LICENSE("GPL v2");
