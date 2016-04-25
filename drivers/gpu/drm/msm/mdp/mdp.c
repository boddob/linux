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

#include "msm_kms.h"

static int get_mdp_ver(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return (int) (unsigned long) of_device_get_match_data(dev);
}

static int mdp_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *ddev = dev_get_drvdata(master);
	struct msm_drm_private *priv = ddev->dev_private;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_kms *kms;
	int ret;

	switch (get_mdp_ver(pdev)) {
	case 4:
		kms = mdp4_kms_init(ddev);
		break;
	case 5:
		kms = mdp5_kms_init(ddev);
		break;
	default:
		kms = ERR_PTR(-ENODEV);
		break;
	}

	if (IS_ERR(kms)) {
		/*
		 * NOTE: once we have GPU support, having no kms should not
		 * be considered fatal.. ideally we would still support gpu
		 * and (for example) use dmabuf/prime to share buffers with
		 * imx drm driver on iMX5
		 */
		dev_err(dev, "failed to init kms\n");
		ret = PTR_ERR(kms);
		goto fail;
	}

	priv->kms = kms;

	pm_runtime_enable(dev);

	ret = kms->funcs->hw_init(kms);
	if (ret) {
		dev_err(dev, "kms hw init failed: %d\n", ret);
		goto fail;
	}

	return 0;
fail:
	return ret;
}

static void mdp_unbind(struct device *dev, struct device *master,
		       void *data)
{
	struct drm_device *ddev = dev_get_drvdata(master);
	struct msm_drm_private *priv = ddev->dev_private;
	struct msm_kms *kms = priv->kms;

	pm_runtime_disable(dev);

	kms->funcs->destroy(kms);
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
	{ .compatible = "qcom,mdp4", .data = (void *) 4 },	/* mdp4 */
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
