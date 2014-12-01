/* Copyright (c) 2008-2014, The Linux Foundation. All rights reserved.
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

#ifndef MDSS_PANEL_H
#define MDSS_PANEL_H

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/regulator/consumer.h>

#include <video/videomode.h>
#include <video/of_videomode.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include "msm_drv.h"
#include "dsi.xml.h"

#define MDSS_DSI_RST_SEQ_LEN	10

struct msm_dsi_panel;

struct msm_dsi_panel_ops {
	int (*on)(struct msm_dsi_panel *msm_panel);
	int (*off)(struct msm_dsi_panel *msm_panel);
};

struct msm_dsi_panel_video_cfg {
	bool valid;
	bool pulse_mode_hsa_he;
	bool hfp_power_stop;
	bool hbp_power_stop;
	bool hsa_power_stop;
	bool eof_bllp_power_stop;
	bool bllp_power_stop;
	u32 traffic_mode;
	u32 dst_format;
};

struct msm_dsi_panel_cmd_cfg {
	bool valid;
	bool insert_dcs_cmd;
	bool te_sel;
	u32 mdp_trigger;
	u32 dma_trigger;
	u32 dst_format;

	struct msm_cmd_te_cfg te;
};

struct msm_dsi_panel_lane_map {
	bool lane0_enabled;
	bool lane1_enabled;
	bool lane2_enabled;
	bool lane3_enabled;
	u32 lane_num;
	u32 lane_swap;
};

struct msm_dsi_panel_color_map {
	bool r_swap;
	bool g_swap;
	bool b_swap;
	u32 rgb_swap;
};

struct msm_dsi_panel_desc {
	struct videomode vm;
	u32 bpp;
	u32 frame_rate;
	u32 physical_width;
	u32 physical_height;

	enum msm_dsi_mode mode;
	struct msm_dsi_panel_video_cfg vid_cfg;
	struct msm_dsi_panel_cmd_cfg cmd_cfg;
	bool mode_switch_enabled;

	struct msm_dsi_panel_lane_map lane_map;
	struct msm_dsi_panel_color_map color_map;
	u32 vc;

	bool rx_eot_ignore;
	bool tx_eot_append;
	bool lp11_init;
	u32  init_delay;
	u32 t_clk_post;
	u32 t_clk_pre;

	u32 rst_seq[MDSS_DSI_RST_SEQ_LEN];
	u32 rst_seq_len;

	u32 phy_timing_setting[12];

	u32 mode_gpio_state;

	bool broadcast_enabled;
	u32 trigger_id; /* which is the trigger panel in broadcast mode */

	struct msm_dsi_panel_ops *ops;
};

enum msm_dsi_panel_supply {
	MSM_DSI_PANEL_SUPPLY_VDD = 0,
	MSM_DSI_PANEL_SUPPLY_VDDIO,
	MSM_DSI_PANEL_SUPPLY_NUM
};

struct msm_dsi_panel {
	struct drm_panel base;

	u32 id;
	u32 bl_max;
	u32 bl_min;
	u32 bl_level;
	int bklt_ctrl_mode;
	int pwm_period;
	struct pwm_device *pwm_bl;
	struct mutex bl_mutex;

	struct regulator_bulk_data supplies[MSM_DSI_PANEL_SUPPLY_NUM];
	struct gpio_desc *rst_gpio;
	struct gpio_desc *disp_en_gpio;
	struct gpio_desc *disp_te_gpio;
	struct gpio_desc *bklt_en_gpio;
	struct gpio_desc *mode_gpio;

	/* current ROI */
	u32 roi_x;
	u32 roi_y;
	u32 roi_w;
	u32 roi_h;

	u32 frame_rate; /* current fps */
	enum msm_dsi_mode mode; /* current mode */

	struct msm_dsi_panel_desc desc; /* Panel specific info */

	int (*init)(struct msm_dsi_panel_desc *desc);
};
#endif /* MDSS_PANEL_H */

