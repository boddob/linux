/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of_irq.h>
#include "dsi.h"

static void dsi_destroy(struct msm_dsi *msm_dsi)
{
	if (!msm_dsi)
		return;

	if (msm_dsi->host) {
		msm_dsi_host_destroy(msm_dsi->host);
		msm_dsi->host = NULL;
	}

	platform_set_drvdata(msm_dsi->pdev, NULL);
}

static struct msm_dsi *dsi_init(struct platform_device *pdev)
{
	struct msm_dsi *msm_dsi = NULL;
	int ret;

	if (!pdev) {
		dev_err(&pdev->dev, "no dsi device\n");
		ret = -ENXIO;
		goto fail;
	}

	msm_dsi = devm_kzalloc(&pdev->dev, sizeof(*msm_dsi), GFP_KERNEL);
	if (!msm_dsi) {
		ret = -ENOMEM;
		goto fail;
	}
	DBG("dsi probed=%p", msm_dsi);

	msm_dsi->pdev = pdev;
	platform_set_drvdata(pdev, msm_dsi);

	/* Init dsi internal */
	ret = msm_dsi_host_init(msm_dsi);
	if (ret)
		goto fail;

	return msm_dsi;

fail:
	if (msm_dsi)
		dsi_destroy(msm_dsi);

	return ERR_PTR(ret);
}

static int dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_dsi *msm_dsi;

	DBG("");
	msm_dsi = dsi_init(pdev);
	if (IS_ERR(msm_dsi))
		return PTR_ERR(msm_dsi);

	priv->dsi[pdev->id] = msm_dsi;

	return 0;
}

static void dsi_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct platform_device *pdev = to_platform_device(dev);

	if (priv->dsi[pdev->id]) {
		dsi_destroy(priv->dsi[pdev->id]);
		priv->dsi[pdev->id] = NULL;
	}
}

static const struct component_ops dsi_ops = {
	.bind   = dsi_bind,
	.unbind = dsi_unbind,
};

static int dsi_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dsi_ops);
}

static int dsi_dev_remove(struct platform_device *pdev)
{
	DBG("");
	component_del(&pdev->dev, &dsi_ops);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,mdss-dsi-ctrl" },
	{}
};

static struct platform_driver dsi_driver = {
	.probe = dsi_dev_probe,
	.remove = dsi_dev_remove,
	.driver = {
		.name = "msm_dsi",
		.of_match_table = dt_match,
	},
};

void __init msm_dsi_register(void)
{
	DBG("");
	platform_driver_register(&dsi_driver);
}

void __exit msm_dsi_unregister(void)
{
	DBG("");
	platform_driver_unregister(&dsi_driver);
}

int msm_dsi_modeset_init(struct msm_dsi *msm_dsi, struct drm_device *dev,
		struct drm_encoder *encoder, enum msm_dsi_mode *dsi_mode)
{
	struct platform_device *pdev = msm_dsi->pdev;
	struct msm_drm_private *priv = dev->dev_private;
	int ret;

	msm_dsi->dev = dev;
	msm_dsi->encoder = encoder;

	ret = msm_dsi_host_modeset_init(msm_dsi->host, dev);
	if (ret) {
		dev_err(dev->dev, "failed to modeset init host: %d\n", ret);
		goto fail;
	}

	msm_dsi->bridge = dsi_bridge_init(msm_dsi);
	if (IS_ERR(msm_dsi->bridge)) {
		ret = PTR_ERR(msm_dsi->bridge);
		dev_err(dev->dev, "failed to create dsi bridge: %d\n", ret);
		msm_dsi->bridge = NULL;
		goto fail;
	}

	msm_dsi->connector = dsi_connector_init(msm_dsi);
	if (IS_ERR(msm_dsi->connector)) {
		ret = PTR_ERR(msm_dsi->connector);
		dev_err(dev->dev, "failed to create dsi connector: %d\n", ret);
		msm_dsi->connector = NULL;
		goto fail;
	}

	msm_dsi->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (msm_dsi->irq < 0) {
		ret = msm_dsi->irq;
		dev_err(dev->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, msm_dsi->irq,
			msm_dsi_host_irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"dsi_isr", msm_dsi->host);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ%u: %d\n",
				msm_dsi->irq, ret);
		goto fail;
	}


	if (ret < 0) {
		dev_err(dev->dev, "failed to register shared irq: %d\n", ret);
		goto fail;
	}

	encoder->bridge = msm_dsi->bridge;

	priv->bridges[priv->num_bridges++]       = msm_dsi->bridge;
	priv->connectors[priv->num_connectors++] = msm_dsi->connector;

	if (dsi_mode && msm_dsi->panel)
		*dsi_mode = msm_dsi_panel_get_op_mode(msm_dsi->panel);

	return 0;
fail:
	if (msm_dsi) {
		/* bridge/connector are normally destroyed by drm: */
		if (msm_dsi->bridge) {
			msm_dsi->bridge->funcs->destroy(msm_dsi->bridge);
			msm_dsi->bridge = NULL;
		}
		if (msm_dsi->connector) {
			msm_dsi->connector->funcs->destroy(msm_dsi->connector);
			msm_dsi->connector = NULL;
		}
	}

	return ret;
}

struct msm_cmd_te_cfg *msm_dsi_get_te_info(struct msm_dsi *msm_dsi)
{
	return msm_dsi_panel_get_te_info(msm_dsi->panel);
}

