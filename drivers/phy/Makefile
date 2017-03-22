#
# Makefile for the phy drivers.
#

obj-$(CONFIG_GENERIC_PHY)		+= phy-core.o
obj-$(CONFIG_PHY_LPC18XX_USB_OTG)	+= phy-lpc18xx-usb-otg.o
obj-$(CONFIG_PHY_MT65XX_USB3)		+= phy-mt65xx-usb3.o
obj-$(CONFIG_PHY_XGENE)			+= phy-xgene.o
obj-$(CONFIG_PHY_PISTACHIO_USB)		+= phy-pistachio-usb.o
obj-$(CONFIG_PHY_MESON8B_USB2)		+= phy-meson8b-usb2.o
obj-$(CONFIG_ARCH_TEGRA) 		+= tegra/
obj-$(CONFIG_ARCH_SUNXI)		+= allwinner/
obj-$(CONFIG_ARCH_RENESAS)		+= renesas/
obj-$(CONFIG_ARCH_ROCKCHIP)		+= rockchip/

obj-y					+= broadcom/	\
					   hisilicon/	\
					   marvell/	\
					   qualcomm/	\
					   samsung/	\
					   st/		\
					   ti/
