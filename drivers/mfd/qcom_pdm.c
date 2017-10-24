/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/sysfs.h>
#include <linux/reset.h>
#include <linux/of.h>

#define PDM_TCXO_PDM0_TOP_CTL	0x00
#define PDM_PDM0_CTL		0x04
#define PDM_TCXO4_TEST		0x08

#define PDM_TCXO_PDM1_TOP_CTL	0x1000
#define PDM_PDM1_CTL		0x1004

#define PDM_TCXO_PDM2_TOP_CTL	0x2000
#define PDM_PDM2_CTL		0x2004

#define PDM_GP_MN_CLK_MDIV	0x2000
#define PDM_GP_MN_CLK_NDIV	0x2004
#define PDM_GP_MN_CLK_DUTY	0x2008

struct qcom_pdm {
	void __iomem *base;
	struct clk *iface;
	struct clk *pdm2;
	struct clk *tcxo;
	struct platform_device *pdev;
};

static ssize_t pdm_value_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct qcom_pdm *pdm = dev_get_drvdata(dev);
	u32 val;

	val = ioread32(pdm->base + PDM_PDM0_CTL);

	return snprintf(buf, PAGE_SIZE, "%x\n", val);
}

static ssize_t pdm_value_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct qcom_pdm *pdm = dev_get_drvdata(dev);
	u32 val;
	int ret;

	ret = kstrtou32(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val > 0xffff) {
		dev_err(dev, "enter a 16 bit value only\n");
		return -EINVAL;
	}

	iowrite32(val, pdm->base + PDM_PDM0_CTL);

	return count;
}

static DEVICE_ATTR(value, S_IRUGO | S_IWUSR,
		pdm_value_show, pdm_value_store);

static struct attribute *pdm_attrs[] = {
	&dev_attr_value.attr,
	NULL,
};

static struct attribute_group pdm_attr_group = {
	.attrs = pdm_attrs,
};

static const struct of_device_id qcom_pdm_of_match[] = {
	{ .compatible = "qcom,pdm", },
};
MODULE_DEVICE_TABLE(of, qcom_pdm_of_match);

static int qcom_pdm_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct qcom_pdm *pdm;
	struct reset_control *reset;
	int ret;

	pdm = devm_kzalloc(&pdev->dev, sizeof(*pdm), GFP_KERNEL);
	if (!pdm)
		return -ENOMEM;


	pdm->iface = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(pdm->iface))
		return PTR_ERR(pdm->iface);

	pdm->tcxo = devm_clk_get(&pdev->dev, "tcxo");
	if (IS_ERR(pdm->tcxo))
		return PTR_ERR(pdm->tcxo);

	pdm->pdm2 = devm_clk_get(&pdev->dev, "pdm2");
	if (IS_ERR(pdm->pdm2))
		return PTR_ERR(pdm->pdm2);

	clk_prepare_enable(pdm->iface);
	clk_prepare_enable(pdm->pdm2);
	clk_prepare_enable(pdm->tcxo);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdm->base))
		return PTR_ERR(pdm->base);

	ret = pinctrl_pm_select_default_state(&pdev->dev);
	if (ret)
		return ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &pdm_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, pdm);

	reset = devm_reset_control_get(&pdev->dev, "core");
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	reset_control_assert(reset);
	usleep_range(10000, 12000);
	reset_control_deassert(reset);

	/* enable PDM0 with 0 polarity */
	iowrite32(0x1, pdm->base + PDM_TCXO_PDM0_TOP_CTL);

	return 0;
}

static int qcom_pdm_remove(struct platform_device *pdev)
{
	struct qcom_pdm *pdm = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(pdm->iface);
	clk_disable_unprepare(pdm->pdm2);
	clk_disable_unprepare(pdm->tcxo);

	return 0;
}

static struct platform_driver qcom_pdm_driver = {
	.probe = qcom_pdm_probe,
	.remove = qcom_pdm_remove,
	.driver  = {
		.name  = "qcom_pdm",
		.of_match_table = qcom_pdm_of_match,
	},
};

module_platform_driver(qcom_pdm_driver)

MODULE_DESCRIPTION("Qualcomm PDM");
MODULE_LICENSE("GPL v2");
