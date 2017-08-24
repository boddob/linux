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

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "mdp5_kms.h"

static struct mdp5_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

#ifdef DOWNSTREAM_CONFIG_MSM_BUS_SCALING
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

static void mdp5_encoder_setup_crtc_state(struct drm_encoder *encoder,
		struct drm_crtc_state *crtc_state)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc_state);
	struct mdp5_interface *intf = mdp5_encoder->intf;
	struct mdp5_ctl *ctl = mdp5_encoder->ctl;

	mdp5_cstate->ctl = ctl;
	mdp5_cstate->pipeline.intf = intf;
}

static void mdp5_vid_encoder_mode_set(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	int intf = mdp5_encoder->intf->num;
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

	/* DSI controller cannot handle active-low sync signals. */
	if (mdp5_encoder->intf->type != INTF_DSI) {
		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			ctrl_pol |= MDP5_INTF_POLARITY_CTL_HSYNC_LOW;
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ctrl_pol |= MDP5_INTF_POLARITY_CTL_VSYNC_LOW;
	}
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
	if (mdp5_encoder->intf->type == INTF_eDP) {
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

	mdp5_crtc_set_pipeline(encoder->crtc);
}

static struct drm_display_mode *
mdp5_vid_encoder_readback_mode(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct drm_display_mode *mode;
	int intfn = mdp5_encoder->intf->num;
	uint32_t hsync_ctl, display_hctl;
	uint32_t vsync_period, vsync_len;
	uint32_t display_v_start, display_v_end;
	uint32_t dtv_hsync_skew = 0;  /* get this from panel? */

	mode = drm_mode_create(encoder->dev);

	hsync_ctl    = mdp5_read(mdp5_kms, REG_MDP5_INTF_HSYNC_CTL(intfn));
	display_hctl = mdp5_read(mdp5_kms, REG_MDP5_INTF_DISPLAY_HCTL(intfn));
	vsync_period = mdp5_read(mdp5_kms, REG_MDP5_INTF_VSYNC_PERIOD_F0(intfn));
	display_v_start = mdp5_read(mdp5_kms, REG_MDP5_INTF_DISPLAY_VSTART_F0(intfn));
	display_v_end   = mdp5_read(mdp5_kms, REG_MDP5_INTF_DISPLAY_VEND_F0(intfn));
	vsync_len    = mdp5_read(mdp5_kms, REG_MDP5_INTF_VSYNC_LEN_F0(intfn));

	// TODO I don't think there is a way to recover mode->clock??
	mode->htotal      = FIELD(hsync_ctl, MDP5_INTF_HSYNC_CTL_PERIOD);
	mode->hsync_start = mode->htotal - FIELD(display_hctl, MDP5_INTF_DISPLAY_HCTL_START);
	mode->hsync_end   = FIELD(hsync_ctl, MDP5_INTF_HSYNC_CTL_PULSEW) + mode->hsync_start;
	mode->hdisplay    = FIELD(display_hctl, MDP5_INTF_DISPLAY_HCTL_END) + 1 -
			mode->htotal + mode->hsync_start;

	if (mdp5_encoder->intf->type == INTF_eDP) {
		display_v_start -= mode->htotal - mode->hsync_start;
	}

	mode->vtotal      = vsync_period / mode->htotal;
	mode->vsync_start = mode->vtotal - ((display_v_start - dtv_hsync_skew) / mode->htotal);
	mode->vsync_end   = (vsync_len / mode->htotal) + mode->vsync_start;
	mode->vdisplay    = mode->vsync_start - ((vsync_period + dtv_hsync_skew - 1 - display_v_end) / mode->htotal);

	// TODO maybe we want a flag to indicate that this is a readback
	// mode, so not all fields are valid.  (Ie. when comparing to
	// another mode to decide whether to do full modeset or not,
	// ignore the fields that are zero.)
	mode->type = DRM_MODE_TYPE_DRIVER;

	// XXX how can we get these?  Maybe get modes from connector and
	// see what fits (although that sounds ugly and annoying)
	mode->clock = 148500;
	mode->vrefresh = 60;
	mode->flags = DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_PHSYNC;

	drm_mode_set_name(mode);

	drm_mode_set_crtcinfo(mode, 0);

	DBG("readback: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	return mode;
}

static void mdp5_vid_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_encoder->ctl;
	struct mdp5_pipeline *pipeline = mdp5_crtc_get_pipeline(encoder->crtc);
	struct mdp5_hw_mixer *mixer = mdp5_crtc_get_mixer(encoder->crtc);
	struct mdp5_interface *intf = mdp5_encoder->intf;
	int intfn = mdp5_encoder->intf->num;
	unsigned long flags;

	if (WARN_ON(!mdp5_encoder->enabled))
		return;

	mdp5_ctl_set_encoder_state(ctl, pipeline, false);

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intfn), 0);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
	mdp5_ctl_commit(ctl, pipeline, mdp_ctl_flush_mask_encoder(intf));

	/*
	 * Wait for a vsync so we know the ENABLE=0 latched before
	 * the (connector) source of the vsync's gets disabled,
	 * otherwise we end up in a funny state if we re-enable
	 * before the disable latches, which results that some of
	 * the settings changes for the new modeset (like new
	 * scanout buffer) don't latch properly..
	 */
	mdp_irq_wait(&mdp5_kms->base, intf2vblank(mixer, intf));

	bs_set(mdp5_encoder, 0);

	mdp5_encoder->enabled = false;
}

static void mdp5_vid_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_encoder->ctl;
	struct mdp5_interface *intf = mdp5_encoder->intf;
	struct mdp5_pipeline *pipeline = mdp5_crtc_get_pipeline(encoder->crtc);
	int intfn = intf->num;
	unsigned long flags;

	if (WARN_ON(mdp5_encoder->enabled))
		return;

	bs_set(mdp5_encoder, 1);
	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intfn), 1);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
	mdp5_ctl_commit(ctl, pipeline, mdp_ctl_flush_mask_encoder(intf));

	mdp5_ctl_set_encoder_state(ctl, pipeline, true);

	mdp5_encoder->enabled = true;
}

static void mdp5_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;

	if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mdp5_cmd_encoder_mode_set(encoder, mode, adjusted_mode);
	else
		mdp5_vid_encoder_mode_set(encoder, mode, adjusted_mode);
}

void mdp5_encoder_readback(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct msm_drm_private *priv = encoder->dev->dev_private;
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct drm_display_mode *mode;
	struct mdp5_crtc_state *mdp5_cstate;
	struct mdp5_hw_mixer *mixer;
	enum mdp5_pipe pipe;

	if (mdp5_encoder->intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mode = mdp5_cmd_encoder_readback_mode(encoder);
	else
		mode = mdp5_vid_encoder_readback_mode(encoder);

	if (!mode)
		return;

	/* things like the chosen ctl and mixer need to be read back
	 * and punched in to crtc state so that crtc knows what reg's
	 * to readback.  The ctl is based on the encoder connected to
	 * the crtc (ie. us) and the ctl is needed to figure out what
	 * mixer is used.
	 *
	 * The good news is that since the crtc is fully virtualized,
	 * we can just pick any one!
	 */
	encoder->crtc = priv->crtcs[0];
	encoder->crtc->state->encoder_mask = (1 << drm_encoder_index(encoder));

	mdp5_cstate = to_mdp5_crtc_state(encoder->crtc->state);

	mdp5_encoder_setup_crtc_state(encoder, encoder->crtc->state);

	mixer = mdp5_ctl_readback(mdp5_cstate->ctl, &pipe);
	if (WARN_ON(!mixer))
		return;

	DBG("got mixer=%u, pipe=%s", mixer->lm, pipe2name(pipe));

	mdp5_cstate->pipeline.mixer = mixer;

	/* some more mdp5_crtc_state stuff */
	mdp5_cstate->err_irqmask = intf2err(mdp5_encoder->intf->num);
	mdp5_cstate->vblank_irqmask = intf2vblank(mixer, mdp5_encoder->intf);
	mdp5_cstate->pp_done_irqmask = 0;
	mdp5_cstate->cmd_mode = false;

	mdp5_kms->state->hwmixer.hwmixer_to_crtc[mixer->idx] = encoder->crtc;

	mdp5_encoder->enabled = true;

	mdp5_crtc_readback(encoder->crtc, mode, pipe);
}

static void mdp5_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;

	if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mdp5_cmd_encoder_disable(encoder);
	else
		mdp5_vid_encoder_disable(encoder);
}

static void mdp5_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;
	/* this isn't right I think */
	struct drm_crtc_state *cstate = encoder->crtc->state;

	mdp5_encoder_mode_set(encoder, &cstate->mode, &cstate->adjusted_mode);

	if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mdp5_cmd_encoder_enable(encoder);
	else
		mdp5_vid_encoder_enable(encoder);
}

static int mdp5_encoder_atomic_check(struct drm_encoder *encoder,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	mdp5_encoder_setup_crtc_state(encoder, crtc_state);

	return 0;
}

static const struct drm_encoder_helper_funcs mdp5_encoder_helper_funcs = {
	.disable = mdp5_encoder_disable,
	.enable = mdp5_encoder_enable,
	.atomic_check = mdp5_encoder_atomic_check,
};

int mdp5_encoder_get_linecount(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int intf = mdp5_encoder->intf->num;

	return mdp5_read(mdp5_kms, REG_MDP5_INTF_LINE_COUNT(intf));
}

u32 mdp5_encoder_get_framecount(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int intf = mdp5_encoder->intf->num;

	return mdp5_read(mdp5_kms, REG_MDP5_INTF_FRAME_COUNT(intf));
}

int mdp5_vid_encoder_set_split_display(struct drm_encoder *encoder,
				       struct drm_encoder *slave_encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_encoder *mdp5_slave_enc = to_mdp5_encoder(slave_encoder);
	struct mdp5_kms *mdp5_kms;
	struct device *dev;
	int intf_num;
	u32 data = 0;

	if (!encoder || !slave_encoder)
		return -EINVAL;

	mdp5_kms = get_kms(encoder);
	intf_num = mdp5_encoder->intf->num;

	/* Switch slave encoder's TimingGen Sync mode,
	 * to use the master's enable signal for the slave encoder.
	 */
	if (intf_num == 1)
		data |= MDP5_SPLIT_DPL_LOWER_INTF2_TG_SYNC;
	else if (intf_num == 2)
		data |= MDP5_SPLIT_DPL_LOWER_INTF1_TG_SYNC;
	else
		return -EINVAL;

	dev = &mdp5_kms->pdev->dev;
	/* Make sure clocks are on when connectors calling this function. */
	pm_runtime_get_sync(dev);

	/* Dumb Panel, Sync mode */
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_UPPER, 0);
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_LOWER, data);
	mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_EN, 1);

	mdp5_ctl_pair(mdp5_encoder->ctl, mdp5_slave_enc->ctl, true);

	pm_runtime_put_autosuspend(dev);

	return 0;
}

void mdp5_encoder_set_intf_mode(struct drm_encoder *encoder, bool cmd_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;

	/* TODO: Expand this to set writeback modes too */
	if (cmd_mode) {
		WARN_ON(intf->type != INTF_DSI);
		intf->mode = MDP5_INTF_DSI_MODE_COMMAND;
	} else {
		if (intf->type == INTF_DSI)
			intf->mode = MDP5_INTF_DSI_MODE_VIDEO;
		else
			intf->mode = MDP5_INTF_MODE_NONE;
	}
}

/* initialize encoder */
struct drm_encoder *mdp5_encoder_init(struct drm_device *dev,
				      struct mdp5_interface *intf,
				      struct mdp5_ctl *ctl)
{
	struct drm_encoder *encoder = NULL;
	struct mdp5_encoder *mdp5_encoder;
	int enc_type = (intf->type == INTF_DSI) ?
		DRM_MODE_ENCODER_DSI : DRM_MODE_ENCODER_TMDS;
	int ret;

	mdp5_encoder = kzalloc(sizeof(*mdp5_encoder), GFP_KERNEL);
	if (!mdp5_encoder) {
		ret = -ENOMEM;
		goto fail;
	}

	encoder = &mdp5_encoder->base;
	mdp5_encoder->ctl = ctl;
	mdp5_encoder->intf = intf;

	spin_lock_init(&mdp5_encoder->intf_lock);

	drm_encoder_init(dev, encoder, &mdp5_encoder_funcs, enc_type, NULL);

	drm_encoder_helper_add(encoder, &mdp5_encoder_helper_funcs);

	bs_init(mdp5_encoder);

	return encoder;

fail:
	if (encoder)
		mdp5_encoder_destroy(encoder);

	return ERR_PTR(ret);
}
