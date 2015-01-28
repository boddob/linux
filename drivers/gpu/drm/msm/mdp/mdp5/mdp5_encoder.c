/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#include "mdp5_kms.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

struct mdp5_encoder {
	struct drm_encoder base;
	struct mdp5_interface intf;
	spinlock_t intf_lock;	/* protect REG_MDP5_INTF_* registers */
	bool enabled;
	uint32_t bsc;
};
#define to_mdp5_encoder(x) container_of(x, struct mdp5_encoder, base)

static struct mdp5_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

#ifdef CONFIG_MSM_BUS_SCALING
#include <mach/board.h>
#include <mach/msm_bus.h>
#include <mach/msm_bus_board.h>
#define MDP_BUS_VECTOR_ENTRY(ab_val, ib_val)		\
	{						\
		.src = MSM_BUS_MASTER_MDP_PORT0,	\
		.dst = MSM_BUS_SLAVE_EBI_CH0,		\
		.ab = (ab_val),				\
		.ib = (ib_val),				\
	}

static struct msm_bus_vectors mdp_bus_vectors[] = {
	MDP_BUS_VECTOR_ENTRY(0, 0),
	MDP_BUS_VECTOR_ENTRY(2000000000, 2000000000),
};
static struct msm_bus_paths mdp_bus_usecases[] = { {
		.num_paths = 1,
		.vectors = &mdp_bus_vectors[0],
}, {
		.num_paths = 1,
		.vectors = &mdp_bus_vectors[1],
} };
static struct msm_bus_scale_pdata mdp_bus_scale_table = {
	.usecase = mdp_bus_usecases,
	.num_usecases = ARRAY_SIZE(mdp_bus_usecases),
	.name = "mdss_mdp",
};

static void bs_init(struct mdp5_encoder *mdp5_encoder)
{
	mdp5_encoder->bsc = msm_bus_scale_register_client(
			&mdp_bus_scale_table);
	DBG("bus scale client: %08x", mdp5_encoder->bsc);
}

static void bs_fini(struct mdp5_encoder *mdp5_encoder)
{
	if (mdp5_encoder->bsc) {
		msm_bus_scale_unregister_client(mdp5_encoder->bsc);
		mdp5_encoder->bsc = 0;
	}
}

static void bs_set(struct mdp5_encoder *mdp5_encoder, int idx)
{
	if (mdp5_encoder->bsc) {
		DBG("set bus scaling: %d", idx);
		/* HACK: scaling down, and then immediately back up
		 * seems to leave things broken (underflow).. so
		 * never disable:
		 */
		idx = 1;
		msm_bus_scale_client_update_request(mdp5_encoder->bsc, idx);
	}
}
#else
static void bs_init(struct mdp5_encoder *mdp5_encoder) {}
static void bs_fini(struct mdp5_encoder *mdp5_encoder) {}
static void bs_set(struct mdp5_encoder *mdp5_encoder, int idx) {}
#endif

/*
 * Command mode encoder, used by DSI command mode path.
 * Should move to a separate file, once global bandwidth
 * functions are available.
 */
#define VSYNC_CLK_RATE 19200000
static int pingpong_tearcheck_setup(struct drm_encoder *encoder,
					struct drm_display_mode *mode)
{
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct device *dev = encoder->dev->dev;
	u32 total_lines_x100, vclks_line, cfg;
	long vsync_clk_speed;
	int pp_id = GET_PING_PONG_ID(mdp5_crtc_get_lm(encoder->crtc));

	if (IS_ERR_OR_NULL(mdp5_kms->vsync_clk)) {
		dev_err(dev, "vsync_clk is not initialized\n");
		return -EINVAL;
	}

	total_lines_x100 = mode->vtotal * mode->vrefresh;
	if (!total_lines_x100) {
		dev_err(dev, "%s: vtotal(%d) or vrefresh(%d) is 0\n",
				__func__, mode->vtotal, mode->vrefresh);
		return -EINVAL;
	}

	vsync_clk_speed = clk_round_rate(mdp5_kms->vsync_clk, VSYNC_CLK_RATE);
	if (vsync_clk_speed <= 0) {
		dev_err(dev, "vsync_clk round rate failed %ld\n",
							vsync_clk_speed);
		return -EINVAL;
	}
	vclks_line = vsync_clk_speed * 100 / total_lines_x100;

	cfg = MDP5_PP_SYNC_CONFIG_VSYNC_COUNTER_EN
		| MDP5_PP_SYNC_CONFIG_VSYNC_IN_EN;
	cfg |= MDP5_PP_SYNC_CONFIG_VSYNC_COUNT(vclks_line);

	mdp5_write(mdp5_kms, REG_MDP5_PP_SYNC_CONFIG_VSYNC(pp_id), cfg);
	mdp5_write(mdp5_kms,
		REG_MDP5_PP_SYNC_CONFIG_HEIGHT(pp_id), 0xfff0);
	mdp5_write(mdp5_kms,
		REG_MDP5_PP_VSYNC_INIT_VAL(pp_id), mode->vdisplay);
	mdp5_write(mdp5_kms, REG_MDP5_PP_RD_PTR_IRQ(pp_id), mode->vdisplay + 1);
	mdp5_write(mdp5_kms, REG_MDP5_PP_START_POS(pp_id), mode->vdisplay);
	mdp5_write(mdp5_kms, REG_MDP5_PP_SYNC_THRESH(pp_id),
			MDP5_PP_SYNC_THRESH_START(4) |
			MDP5_PP_SYNC_THRESH_CONTINUE(4));

	return 0;
}

static int pingpong_tearcheck_enable(struct drm_encoder *encoder)
{
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int pp_id = GET_PING_PONG_ID(mdp5_crtc_get_lm(encoder->crtc));
	int ret;

	ret = clk_set_rate(mdp5_kms->vsync_clk,
		clk_round_rate(mdp5_kms->vsync_clk, VSYNC_CLK_RATE));
	if (ret) {
		dev_err(encoder->dev->dev,
			"vsync_clk clk_set_rate failed, %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(mdp5_kms->vsync_clk);
	if (ret) {
		dev_err(encoder->dev->dev,
			"vsync_clk clk_prepare_enable failed, %d\n", ret);
		return ret;
	}

	mdp5_write(mdp5_kms, REG_MDP5_PP_TEAR_CHECK_EN(pp_id), 1);

	return 0;
}

static void pingpong_tearcheck_disable(struct drm_encoder *encoder)
{
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int pp_id = GET_PING_PONG_ID(mdp5_crtc_get_lm(encoder->crtc));

	mdp5_write(mdp5_kms, REG_MDP5_PP_TEAR_CHECK_EN(pp_id), 0);
	clk_disable_unprepare(mdp5_kms->vsync_clk);
}

static void mdp5_cmd_encoder_destroy(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	bs_fini(mdp5_encoder);
	drm_encoder_cleanup(encoder);
	kfree(mdp5_encoder);
}

static const struct drm_encoder_funcs mdp5_cmd_encoder_funcs = {
	.destroy = mdp5_cmd_encoder_destroy,
};

static bool mdp5_cmd_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mdp5_cmd_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);

	mode = adjusted_mode;

	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);
	pingpong_tearcheck_setup(encoder, mode);
	mdp5_crtc_set_intf(encoder->crtc, &mdp5_encoder->intf);
}

static void mdp5_cmd_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_crtc_get_ctl(encoder->crtc);
	struct mdp5_interface *intf = &mdp5_encoder->intf;
	int lm = mdp5_crtc_get_lm(encoder->crtc);

	if (WARN_ON(!mdp5_encoder->enabled))
		return;

	/* Wait for the last frame done */
	mdp_irq_wait(&mdp5_kms->base, lm2ppdone(lm));
	pingpong_tearcheck_disable(encoder);

	mdp5_ctl_set_encoder_state(ctl, false);
	mdp5_ctl_commit(ctl, mdp_ctl_flush_mask_encoder(intf));

	bs_set(mdp5_encoder, 0);

	mdp5_encoder->enabled = false;
}

static void mdp5_cmd_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_ctl *ctl = mdp5_crtc_get_ctl(encoder->crtc);
	struct mdp5_interface *intf = &mdp5_encoder->intf;

	if (WARN_ON(mdp5_encoder->enabled))
		return;

	bs_set(mdp5_encoder, 1);
	if (pingpong_tearcheck_enable(encoder))
		return;

	mdp5_ctl_commit(ctl, mdp_ctl_flush_mask_encoder(intf));

	mdp5_ctl_set_encoder_state(ctl, true);

	mdp5_encoder->enabled = true;
}

static const struct drm_encoder_helper_funcs mdp5_cmd_encoder_helper_funcs = {
	.mode_fixup = mdp5_cmd_encoder_mode_fixup,
	.mode_set = mdp5_cmd_encoder_mode_set,
	.disable = mdp5_cmd_encoder_disable,
	.enable = mdp5_cmd_encoder_enable,
};

int mdp5_cmd_encoder_set_split_display(struct drm_encoder *encoder,
					struct drm_encoder *slave_encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms;
	int intf_num;
	u32 data = 0;

	if (!encoder || !slave_encoder)
		return -EINVAL;

	mdp5_kms = get_kms(encoder);
	intf_num = mdp5_encoder->intf.num;

	/* Switch slave encoder's trigger MUX, to use the master's
	 * start signal for the slave encoder
	 */
	if (intf_num == 1)
		data |= MDP5_SPLIT_DPL_UPPER_INTF2_SW_TRG_MUX;
	else if (intf_num == 2)
		data |= MDP5_SPLIT_DPL_UPPER_INTF1_SW_TRG_MUX;
	else
		return -EINVAL;

	/* Smart Panel, Sync mode */
	data |= MDP5_SPLIT_DPL_UPPER_SMART_PANEL;

	/* Make sure clocks are on when connectors calling this function. */
	mdp5_enable(mdp5_kms);
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_UPPER, data);

	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_LOWER,
			MDP5_SPLIT_DPL_LOWER_SMART_PANEL);
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_EN, 1);
	mdp5_disable(mdp5_kms);

	return 0;
}

static void mdp5_encoder_destroy(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	bs_fini(mdp5_encoder);
	drm_encoder_cleanup(encoder);
	kfree(mdp5_encoder);
}

static const struct drm_encoder_funcs mdp5_encoder_funcs = {
	.destroy = mdp5_encoder_destroy,
};

static bool mdp5_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mdp5_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	int intf = mdp5_encoder->intf.num;
	uint32_t dtv_hsync_skew, vsync_period, vsync_len, ctrl_pol;
	uint32_t display_v_start, display_v_end;
	uint32_t hsync_start_x, hsync_end_x;
	uint32_t format = 0x2100;
	unsigned long flags;

	mode = adjusted_mode;

	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	ctrl_pol = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		ctrl_pol |= MDP5_INTF_POLARITY_CTL_HSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		ctrl_pol |= MDP5_INTF_POLARITY_CTL_VSYNC_LOW;
	/* probably need to get DATA_EN polarity from panel.. */

	dtv_hsync_skew = 0;  /* get this from panel? */

	/* Get color format from panel, default is 8bpc */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			switch (connector->display_info.bpc) {
			case 4:
				format |= 0;
				break;
			case 5:
				format |= 0x15;
				break;
			case 6:
				format |= 0x2A;
				break;
			case 8:
			default:
				format |= 0x3F;
				break;
			}
			break;
		}
	}

	hsync_start_x = (mode->htotal - mode->hsync_start);
	hsync_end_x = mode->htotal - (mode->hsync_start - mode->hdisplay) - 1;

	vsync_period = mode->vtotal * mode->htotal;
	vsync_len = (mode->vsync_end - mode->vsync_start) * mode->htotal;
	display_v_start = (mode->vtotal - mode->vsync_start) * mode->htotal + dtv_hsync_skew;
	display_v_end = vsync_period - ((mode->vsync_start - mode->vdisplay) * mode->htotal) + dtv_hsync_skew - 1;

	/*
	 * For edp only:
	 * DISPLAY_V_START = (VBP * HCYCLE) + HBP
	 * DISPLAY_V_END = (VBP + VACTIVE) * HCYCLE - 1 - HFP
	 */
	if (mdp5_encoder->intf.type == INTF_eDP) {
		display_v_start += mode->htotal - mode->hsync_start;
		display_v_end -= mode->hsync_start - mode->hdisplay;
	}

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);

	mdp5_write(mdp5_kms, REG_MDP5_INTF_HSYNC_CTL(intf),
			MDP5_INTF_HSYNC_CTL_PULSEW(mode->hsync_end - mode->hsync_start) |
			MDP5_INTF_HSYNC_CTL_PERIOD(mode->htotal));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_VSYNC_PERIOD_F0(intf), vsync_period);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_VSYNC_LEN_F0(intf), vsync_len);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_HCTL(intf),
			MDP5_INTF_DISPLAY_HCTL_START(hsync_start_x) |
			MDP5_INTF_DISPLAY_HCTL_END(hsync_end_x));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_VSTART_F0(intf), display_v_start);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_VEND_F0(intf), display_v_end);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_BORDER_COLOR(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_UNDERFLOW_COLOR(intf), 0xff);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_HSYNC_SKEW(intf), dtv_hsync_skew);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_POLARITY_CTL(intf), ctrl_pol);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_HCTL(intf),
			MDP5_INTF_ACTIVE_HCTL_START(0) |
			MDP5_INTF_ACTIVE_HCTL_END(0));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_VSTART_F0(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_VEND_F0(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_PANEL_FORMAT(intf), format);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_FRAME_LINE_COUNT_EN(intf), 0x3);  /* frame+line? */

	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);

	mdp5_crtc_set_intf(encoder->crtc, &mdp5_encoder->intf);
}

static void mdp5_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_crtc_get_ctl(encoder->crtc);
	int lm = mdp5_crtc_get_lm(encoder->crtc);
	struct mdp5_interface *intf = &mdp5_encoder->intf;
	int intfn = mdp5_encoder->intf.num;
	unsigned long flags;

	if (WARN_ON(!mdp5_encoder->enabled))
		return;

	mdp5_ctl_set_encoder_state(ctl, false);

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intfn), 0);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
	mdp5_ctl_commit(ctl, mdp_ctl_flush_mask_encoder(intf));

	/*
	 * Wait for a vsync so we know the ENABLE=0 latched before
	 * the (connector) source of the vsync's gets disabled,
	 * otherwise we end up in a funny state if we re-enable
	 * before the disable latches, which results that some of
	 * the settings changes for the new modeset (like new
	 * scanout buffer) don't latch properly..
	 */
	mdp_irq_wait(&mdp5_kms->base, intf2vblank(lm, intf));

	bs_set(mdp5_encoder, 0);

	mdp5_encoder->enabled = false;
}

static void mdp5_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_crtc_get_ctl(encoder->crtc);
	struct mdp5_interface *intf = &mdp5_encoder->intf;
	int intfn = mdp5_encoder->intf.num;
	unsigned long flags;

	if (WARN_ON(mdp5_encoder->enabled))
		return;

	bs_set(mdp5_encoder, 1);
	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intfn), 1);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
	mdp5_ctl_commit(ctl, mdp_ctl_flush_mask_encoder(intf));

	mdp5_ctl_set_encoder_state(ctl, true);

	mdp5_encoder->enabled = true;
}

static const struct drm_encoder_helper_funcs mdp5_encoder_helper_funcs = {
	.mode_fixup = mdp5_encoder_mode_fixup,
	.mode_set = mdp5_encoder_mode_set,
	.disable = mdp5_encoder_disable,
	.enable = mdp5_encoder_enable,
};

int mdp5_encoder_set_split_display(struct drm_encoder *encoder,
					struct drm_encoder *slave_encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms;
	int intf_num;
	u32 data = 0;

	if (!encoder || !slave_encoder)
		return -EINVAL;

	mdp5_kms = get_kms(encoder);
	intf_num = mdp5_encoder->intf.num;

	mdp5_write(mdp5_kms, REG_MDP5_SPARE_0,
		MDP5_SPARE_0_SPLIT_DPL_SINGLE_FLUSH_EN);

	/* Switch slave encoder's TimingGen Sync mode,
	 * to use the master's enable signal for the slave encoder.
	 */
	if (intf_num == 1)
		data |= MDP5_SPLIT_DPL_LOWER_INTF2_TG_SYNC;
	else if (intf_num == 2)
		data |= MDP5_SPLIT_DPL_LOWER_INTF1_TG_SYNC;
	else
		return -EINVAL;

	/* Make sure clocks are on when connectors calling this function. */
	mdp5_enable(mdp5_kms);
	/* Dumb Panel, Sync mode */
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_UPPER, 0);
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_LOWER, data);
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_EN, 1);
	mdp5_disable(mdp5_kms);

	return 0;
}

/* initialize encoder */
struct drm_encoder *mdp5_encoder_init(struct drm_device *dev,
				struct mdp5_interface *intf)
{
	struct drm_encoder *encoder = NULL;
	struct mdp5_encoder *mdp5_encoder;
	const struct drm_encoder_funcs *funcs =
		(intf->mode == MDP5_INTF_DSI_MODE_COMMAND) ?
		(&mdp5_cmd_encoder_funcs) : (&mdp5_encoder_funcs);
	const struct drm_encoder_helper_funcs *helper_funcs =
		(intf->mode == MDP5_INTF_DSI_MODE_COMMAND) ?
		(&mdp5_cmd_encoder_helper_funcs) : (&mdp5_encoder_helper_funcs);
	int enc_type = (intf->type == INTF_DSI) ?
		DRM_MODE_ENCODER_DSI : DRM_MODE_ENCODER_TMDS;

	int ret;

	mdp5_encoder = kzalloc(sizeof(*mdp5_encoder), GFP_KERNEL);
	if (!mdp5_encoder) {
		ret = -ENOMEM;
		goto fail;
	}

	memcpy(&mdp5_encoder->intf, intf, sizeof(mdp5_encoder->intf));
	encoder = &mdp5_encoder->base;

	spin_lock_init(&mdp5_encoder->intf_lock);

	drm_encoder_init(dev, encoder, funcs, enc_type);

	drm_encoder_helper_add(encoder, helper_funcs);

	bs_init(mdp5_encoder);

	return encoder;

fail:
	if (encoder)
		mdp5_encoder_destroy(encoder);

	return ERR_PTR(ret);
}
