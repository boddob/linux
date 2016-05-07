/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/irqdomain.h>
#include <linux/irq.h>

#include "msm_drv.h"
#include "mdp5_kms.h"

struct mdp5_mdss {
	struct drm_device *dev;

	void __iomem *mmio, *vbif;

	struct {
		volatile unsigned long enabled_mask;
		struct irq_domain *domain;
	} irqcontroller;
};

static inline void mdss_write(struct mdp5_mdss *mdss, u32 reg, u32 data)
{
	msm_writel(data, mdss->mmio + reg);
}

static inline u32 mdss_read(struct mdp5_mdss *mdss, u32 reg)
{
	return msm_readl(mdss->mmio + reg);
}

static irqreturn_t mdss_irq(int irq, void *arg)
{
	struct mdp5_mdss *mdss = arg;
	u32 intr;

	intr = mdss_read(mdss, REG_MDSS_HW_INTR_STATUS);

	VERB("intr=%08x", intr);

	while (intr) {
		irq_hw_number_t hwirq = fls(intr) - 1;
		generic_handle_irq(irq_find_mapping(
				mdss->irqcontroller.domain, hwirq));
		intr &= ~(1 << hwirq);
	}

	return IRQ_HANDLED;
}

/*
 * interrupt-controller implementation, so sub-blocks (MDP/HDMI/eDP/DSI/etc)
 * can register to get their irq's delivered
 */

#define VALID_IRQS  (MDSS_HW_INTR_STATUS_INTR_MDP | \
		MDSS_HW_INTR_STATUS_INTR_DSI0 | \
		MDSS_HW_INTR_STATUS_INTR_DSI1 | \
		MDSS_HW_INTR_STATUS_INTR_HDMI | \
		MDSS_HW_INTR_STATUS_INTR_EDP)

static void mdss_hw_mask_irq(struct irq_data *irqd)
{
	struct mdp5_mdss *mdss = irq_data_get_irq_chip_data(irqd);
	smp_mb__before_atomic();
	clear_bit(irqd->hwirq, &mdss->irqcontroller.enabled_mask);
	smp_mb__after_atomic();
}

static void mdss_hw_unmask_irq(struct irq_data *irqd)
{
	struct mdp5_mdss *mdss = irq_data_get_irq_chip_data(irqd);
	smp_mb__before_atomic();
	set_bit(irqd->hwirq, &mdss->irqcontroller.enabled_mask);
	smp_mb__after_atomic();
}

static struct irq_chip mdss_hw_irq_chip = {
	.name		= "mdss",
	.irq_mask	= mdss_hw_mask_irq,
	.irq_unmask	= mdss_hw_unmask_irq,
};

static int mdss_hw_irqdomain_map(struct irq_domain *d,
		unsigned int irq, irq_hw_number_t hwirq)
{
	struct mdp5_mdss *mdss = d->host_data;

	if (!(VALID_IRQS & (1 << hwirq)))
		return -EPERM;

	irq_set_chip_and_handler(irq, &mdss_hw_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, mdss);

	return 0;
}

static struct irq_domain_ops mdss_hw_irqdomain_ops = {
	.map = mdss_hw_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};


static int mdss_irq_domain_init(struct mdp5_mdss *mdss)
{
	struct device *dev = mdss->dev->dev;
	struct irq_domain *d;

	d = irq_domain_add_linear(dev->of_node, 32,
			&mdss_hw_irqdomain_ops, mdss);
	if (!d) {
		dev_err(dev, "mdss irq domain add failed\n");
		return -ENXIO;
	}

	mdss->irqcontroller.enabled_mask = 0;
	mdss->irqcontroller.domain = d;

	return 0;
}

void mdp5_mdss_destroy(struct mdp5_mdss *mdss)
{
	if (mdss->irqcontroller.domain) {
		irq_domain_remove(mdss->irqcontroller.domain);
		mdss->irqcontroller.domain = NULL;
	}
}

struct mdp5_mdss *mdp5_mdss_init(struct drm_device *dev)
{
	struct platform_device *pdev = dev->platformdev;
	struct mdp5_mdss *mdss;
	int ret;

	DBG("");

	mdss = devm_kzalloc(dev->dev, sizeof(*mdss), GFP_KERNEL);
	if (!mdss) {
		dev_err(dev->dev, "failed to allocate mdss kms\n");
		return -ENOMEM;
	}

	mdss->dev = dev;

	mdss->mmio = msm_ioremap(pdev, "mdss_phys", "MDSS");
	if (IS_ERR(mdss->mmio)) {
		ret = PTR_ERR(mdss->mmio);
		goto fail;
	}

	mdss->vbif = msm_ioremap(pdev, "vbif_phys", "VBIF");
	if (IS_ERR(mdss->vbif)) {
		ret = PTR_ERR(mdss->vbif);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, platform_get_irq(pdev, 0),
			       mdss_irq, 0, "mdss_isr", mdss);
	if (ret) {
		dev_err(dev->dev, "failed to init irq: %d\n", ret);
		goto fail;
	}

	ret = mdss_irq_domain_init(mdss);
	if (ret) {
		dev_err(dev->dev, "failed to init sub-block irqs: %d\n", ret);
		goto fail;
	}

	return mdss;
fail:
	mdp5_mdss_destroy(mdss);

	return ERR_PTR(ret);
}
