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

static int jdi_qhd_dual_cmd_panel_on(struct msm_dsi_panel *msm_panel)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(msm_panel->base.dev);
	u8 scanline[2] = {0, 0};
	u8 display_brightness = 0xff;
	u8 control_brightness = 0x24;
	u8 cabc = 0; /* Content Adaptive Brightness Control */
	u8 mcap[2] = {0xb0, 0x3}; /* Manufacturer Command Access Protect */

	/* mode_flags affects msg flags */
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	if (mipi_dsi_generic_write(dsi, mcap, sizeof(mcap)) < sizeof(mcap))
		return -ECOMM;

	if (mipi_dsi_dcs_soft_reset(dsi) < 0)
		return -ECOMM;
	usleep_range(5000, 10000);

	/* write DBI format */
	if (mipi_dsi_dcs_set_pixel_format(dsi, MIPI_DCS_PIXEL_FMT_24BIT) < 0)
		return -ECOMM;
	usleep_range(5000, 10000);

	if (mipi_dsi_dcs_set_column_address(dsi, 0, 1279) < 0)
		return -ECOMM;
	usleep_range(5000, 10000);

	if (mipi_dsi_dcs_set_page_address(dsi, 0, 1439) < 0)
		return -ECOMM;
	usleep_range(5000, 10000);

	if (mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK) < 0)
		return -ECOMM;
	usleep_range(5000, 10000);

	if (mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_TEAR_SCANLINE, scanline,
			sizeof(scanline)) < (sizeof(scanline) + 1))
		return -ECOMM;
	usleep_range(5000, 10000);

	/* write display brightness */
	if (mipi_dsi_dcs_write(dsi, 0x51, &display_brightness,
		sizeof(display_brightness)) < (sizeof(display_brightness) + 1))
		return -ECOMM;
	usleep_range(5000, 10000);

	/* write control brightness */
	if (mipi_dsi_dcs_write(dsi, 0x53, &control_brightness,
		sizeof(control_brightness)) < (sizeof(control_brightness) + 1))
		return -ECOMM;
	usleep_range(5000, 10000);

	/* write CABC */
	if (mipi_dsi_dcs_write(dsi, 0x55, &cabc, sizeof(cabc)) <
		(sizeof(cabc) + 1))
		return -ECOMM;
	usleep_range(5000, 10000);

	if (mipi_dsi_dcs_exit_sleep_mode(dsi) < 0)
		return -ECOMM;
	msleep(120);

	if (mipi_dsi_dcs_set_display_on(dsi) < 0)
		return -ECOMM;
	usleep_range(10000, 15000);

	return 0;
}

static int jdi_qhd_dual_cmd_panel_off(struct msm_dsi_panel *msm_panel)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(msm_panel->base.dev);
	int ret = 0;

	/* mode_flags affects msg flags */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	if (mipi_dsi_dcs_set_display_off(dsi) < 0)
		ret = -ECOMM;
	msleep(50);

	if (mipi_dsi_dcs_set_tear_off(dsi) < 0)
		ret = -ECOMM;
	usleep_range(5000, 10000);

	if (mipi_dsi_dcs_enter_sleep_mode(dsi) < 0)
		ret = -ECOMM;
	msleep(120);

	return 0;
}

static struct msm_dsi_panel_ops jdi_qhd_dual_cmd_panel_ops = {
	.on = jdi_qhd_dual_cmd_panel_on,
	.off = jdi_qhd_dual_cmd_panel_off,
};

int jdi_qhd_dual_cmd_panel_init(struct msm_dsi_panel_desc *desc)
{
	struct videomode *vm = &desc->vm;
	struct msm_dsi_panel_video_cfg *vid_cfg = &desc->vid_cfg;
	struct msm_dsi_panel_cmd_cfg *cmd_cfg = &desc->cmd_cfg;
	struct msm_dsi_panel_lane_map *lane_map = &desc->lane_map;
	struct msm_dsi_panel_color_map *color_map = &desc->color_map;
	u32 *phy_timing = desc->phy_timing_setting;

	desc->ops = &jdi_qhd_dual_cmd_panel_ops;

	vm->pixelclock = 127545600;
	vm->hactive = 1280;
	vm->hfront_porch = 120;
	vm->hsync_len = 16;
	vm->hback_porch = 44;
	vm->vactive = 1440;
	vm->vfront_porch = 8;
	vm->vsync_len = 4;
	vm->vback_porch = 4;

	desc->physical_width = 0;
	desc->physical_height = 0;

	desc->frame_rate = 60;
	desc->bpp = 24;
	desc->mode = MSM_DSI_CMD_MODE;

	desc->vc = 0;

	vid_cfg->pulse_mode_hsa_he = false;
	vid_cfg->hfp_power_stop = false;
	vid_cfg->hsa_power_stop = false;
	vid_cfg->hbp_power_stop = false;
	vid_cfg->bllp_power_stop = true;
	vid_cfg->eof_bllp_power_stop = true;
	vid_cfg->traffic_mode = NON_BURST_SYNCH_EVENT;
	vid_cfg->dst_format = VID_DST_FORMAT_RGB888;
	vid_cfg->valid = true;

	cmd_cfg->insert_dcs_cmd = true;
	cmd_cfg->te_sel = true;
	cmd_cfg->mdp_trigger = TRIGGER_NONE;
	cmd_cfg->dma_trigger = TRIGGER_SW;
	cmd_cfg->dst_format = CMD_DST_FORMAT_RGB888;

	cmd_cfg->te.hw_vsync_mode = 1;
	cmd_cfg->te.sync_cfg_height = 0xfff0;
	cmd_cfg->te.vsync_init_val = 1440;
	cmd_cfg->te.sync_thres_start = 4;
	cmd_cfg->te.sync_thres_cont = 4;
	cmd_cfg->te.start_pos = 1440;
	cmd_cfg->te.rd_ptr_irq = 1441;
	cmd_cfg->te.refx100 = 6000;
	cmd_cfg->te.vtotal = 1456;
	cmd_cfg->valid = true;

	desc->mode_switch_enabled = true;

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

	phy_timing[0] = 0xcd;
	phy_timing[1] = 0x32;
	phy_timing[2] = 0x22;
	phy_timing[3] = 0x00;
	phy_timing[4] = 0x60;
	phy_timing[5] = 0x64;
	phy_timing[6] = 0x26;
	phy_timing[7] = 0x34;
	phy_timing[8] = 0x29;
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

	desc->t_clk_pre = 0x27;
	desc->t_clk_post = 0x03;

	desc->rx_eot_ignore = false;
	desc->tx_eot_append = false;

	desc->lp11_init = false;
	desc->mode_gpio_state = MODE_GPIO_NOT_VALID;

	desc->broadcast_enabled = true;
	desc->trigger_id = DSI_PANEL_2;

	return 0;
}

