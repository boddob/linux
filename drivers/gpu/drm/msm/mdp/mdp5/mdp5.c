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
	struct platform_device *pdev = to_platform_device(dev);

	DBG("");

	return mdp5_init(pdev, ddev);
}

static void mdp_unbind(struct device *dev, struct device *master,
		       void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

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

static int msm_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdp5_kms *mdp5_kms = platform_get_drvdata(pdev);

	DBG("E");

	mdp5_disable(mdp5_kms);

	return 0;
}

static int msm_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdp5_kms *mdp5_kms = platform_get_drvdata(pdev);

	DBG("E");

	mdp5_enable(mdp5_kms);

	return 0;
}

static const struct dev_pm_ops msm_pm_ops = {
	SET_RUNTIME_PM_OPS(msm_runtime_suspend, msm_runtime_resume, NULL)
};

static struct platform_driver mdp_driver = {
	.probe = mdp_dev_probe,
	.remove = mdp_dev_remove,
	.driver = {
		.name = "msm_mdp",
		.of_match_table = dt_match,
		.pm = &msm_pm_ops,
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
