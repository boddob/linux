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

#include <video/mipi_display.h>

#include "panel_init.h"

static int jdi_1080_video_panel_on(struct msm_dsi_panel *msm_panel)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(msm_panel->base.dev);
	u8 pwrsave = 0;
	u8 ctrl_display = 0x2c;

	/* mode_flags affects msg flags */
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

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

	return 0;
}

static int jdi_1080_video_panel_off(struct msm_dsi_panel *msm_panel)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(msm_panel->base.dev);
	int ret = 0;

	/* mode_flags affects msg flags */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	if (mipi_dsi_dcs_set_display_off(dsi) < 0)
		ret = -ECOMM;

	if (mipi_dsi_dcs_enter_sleep_mode(dsi) < 0)
		ret = -ECOMM;
	msleep(120);

	return ret;
}

static struct msm_dsi_panel_ops jdi_1080_video_panel_ops = {
	.on = jdi_1080_video_panel_on,
	.off = jdi_1080_video_panel_off,
};

int jdi_1080_video_panel_init(struct msm_dsi_panel_desc *desc)
{
	struct videomode *vm = &desc->vm;
	struct msm_dsi_panel_video_cfg *vid_cfg = &desc->vid_cfg;
	struct msm_dsi_panel_cmd_cfg *cmd_cfg = &desc->cmd_cfg;
	struct msm_dsi_panel_lane_map *lane_map = &desc->lane_map;
	struct msm_dsi_panel_color_map *color_map = &desc->color_map;
	u32 *phy_timing = desc->phy_timing_setting;

	desc->ops = &jdi_1080_video_panel_ops;

	vm->pixelclock = 146273760;
	vm->hactive = 1080;
	vm->hfront_porch = 96;
	vm->hsync_len = 16;
	vm->hback_porch = 64;
	vm->vactive = 1920;
	vm->vfront_porch = 4;
	vm->vsync_len = 1;
	vm->vback_porch = 16;

	desc->physical_width = 61;
	desc->physical_height = 110;

	desc->frame_rate = 60;
	desc->bpp = 24;
	desc->mode = MSM_DSI_VIDEO_MODE;

	desc->vc = 0;

	vid_cfg->pulse_mode_hsa_he = false;
	vid_cfg->hfp_power_stop = false;
	vid_cfg->hsa_power_stop = false;
	vid_cfg->hbp_power_stop = false;
	vid_cfg->bllp_power_stop = true;
	vid_cfg->eof_bllp_power_stop = true;
	vid_cfg->traffic_mode = BURST_MODE;
	vid_cfg->dst_format = VID_DST_FORMAT_RGB888;
	vid_cfg->valid = true;

	cmd_cfg->mdp_trigger = TRIGGER_NONE;
	cmd_cfg->dma_trigger = TRIGGER_SW;
	cmd_cfg->valid = false;

	desc->mode_switch_enabled = false;

	color_map->rgb_swap = SWAP_RGB;
	color_map->r_swap = false;
	color_map->g_swap = false;
	color_map->b_swap = false;

	lane_map->lane0_enabled = true;
	lane_map->lane1_enabled = true;
	lane_map->lane2_enabled = true;
	lane_map->lane3_enabled = true;
	lane_map->lane_num = 4;
	lane_map->lane_swap = LANE_SWAP_0123;

	phy_timing[0] = 0xe7;
	phy_timing[1] = 0x36;
	phy_timing[2] = 0x24;
	phy_timing[3] = 0x00;
	phy_timing[4] = 0x66;
	phy_timing[5] = 0x6a;
	phy_timing[6] = 0x2a;
	phy_timing[7] = 0x3a;
	phy_timing[8] = 0x2d;
	phy_timing[9] = 0x03;
	phy_timing[10] = 0x04;
	phy_timing[11] = 0x00;

	desc->rst_seq[0] = 1;
	desc->rst_seq[1] = 10;
	desc->rst_seq[2] = 0;
	desc->rst_seq[3] = 10;
	desc->rst_seq[4] = 1;
	desc->rst_seq[5] = 10;
	desc->rst_seq_len = 6;

	desc->t_clk_pre = 0x1b;
	desc->t_clk_post = 0x04;

	desc->rx_eot_ignore = false;
	desc->tx_eot_append = false;

	desc->lp11_init = false;
	desc->mode_gpio_state = MODE_GPIO_NOT_VALID;

	return 0;
}



