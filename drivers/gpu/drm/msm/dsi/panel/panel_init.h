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

#ifndef __MSM_DSI_PANEL_INIT_H
#define __MSM_DSI_PANEL_INIT_H

#include "../dsi_panel.h"

enum {
	DSI_PANEL_1 = 1,/* attached as first device */
	DSI_PANEL_2,	/* attached on second device */
};

enum {
	MODE_GPIO_NOT_VALID = 0,
	MODE_GPIO_HIGH,
	MODE_GPIO_LOW,
};

enum dsi_panel_bl_mode {
	UNKNOWN_MODE,
	BL_PWM,
	BL_WLED,
	BL_DCS_CMD,
};

int jdi_qhd_dual_video_panel_init(struct msm_dsi_panel_desc *desc);
int jdi_qhd_dual_cmd_panel_init(struct msm_dsi_panel_desc *desc);
int jdi_1080_video_panel_init(struct msm_dsi_panel_desc *desc);

#endif /* __MSM_DSI_PANEL_INIT_H */

