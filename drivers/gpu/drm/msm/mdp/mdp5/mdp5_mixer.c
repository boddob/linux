/*
 * Copyright (C) 2016 The Linux Foundation. All rights reserved.
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

void mdp5_mixer_destroy(struct mdp5_hw_mixer *mixer)
{
	kfree(mixer);
}

#define MIXER(s) #s
#define MIXER_NAME(i) MIXER(LM##i)

struct mdp5_hw_mixer *mdp5_mixer_init(const struct mdp5_lm_instance *lm)
{
	struct mdp5_hw_mixer *mixer;

	/* ignore WB bound mixers for now */
	if (lm->caps & MDP_LM_CAP_WB)
		return NULL;

	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return ERR_PTR(-ENOMEM);

	mixer->name = MIXER_NAME(lm->id);
	mixer->lm = lm->id;
	mixer->caps = lm->caps;
	mixer->pp = lm->pp;
	mixer->dspp = lm->dspp;
	mixer->flush_mask = mdp_ctl_flush_mask_lm(lm->id);

	spin_lock_init(&mixer->lm_lock);

	return mixer;
}
