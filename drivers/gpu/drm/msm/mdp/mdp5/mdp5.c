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

static int mdp_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *ddev = dev_get_drvdata(master);
	struct msm_drm_private *priv = ddev->dev_private;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_kms *kms;
	int ret;

	kms = mdp5_kms_init(pdev, ddev);
	if (IS_ERR(kms)) {
		dev_err(dev, "failed to init kms\n");
		ret = PTR_ERR(kms);
		goto fail;
	}

	priv->kms = kms;

	pm_runtime_enable(dev);

	ret = mdp5_hw_init(pdev);
	if (ret) {
		dev_err(dev, "kms hw init failed: %d\n", ret);
		goto fail;
	}

	return 0;
fail:
	mdp5_destroy(pdev);
	return ret;
}

static void mdp_unbind(struct device *dev, struct device *master,
		       void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	pm_runtime_disable(dev);
	mdp5_destroy(pdev);
}

static const struct component_ops mdp_ops = {
	.bind   = mdp_bind,
	.unbind = mdp_unbind,
};

static int mdp_dev_probe(struct platform_device *pdev)
{
	DBG("");
	return component_add(&pdev->dev, &mdp_ops);
}

static int mdp_dev_remove(struct platform_device *pdev)
{
	DBG("");
	component_del(&pdev->dev, &mdp_ops);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,mdp5", .data = (void *) 5 },	/* mdp5 */
	/* to support downstream DT files */
	{ .compatible = "qcom,mdss_mdp", .data = (void *) 5 },  /* mdp5 */
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver mdp_driver = {
	.probe = mdp_dev_probe,
	.remove = mdp_dev_remove,
	.driver = {
		.name = "msm_mdp",
		.of_match_table = dt_match,
	},
};

void __init msm_mdp_register(void)
{
	DBG("");
	platform_driver_register(&mdp_driver);
}

void __exit msm_mdp_unregister(void)
{
	DBG("");
	platform_driver_unregister(&mdp_driver);
}
