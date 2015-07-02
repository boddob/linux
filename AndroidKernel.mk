#Android makefile to build kernel as a part of Android Build
PERL		= perl

ifeq ($(TARGET_PREBUILT_KERNEL),)

KERNEL_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
KERNEL_CONFIG := $(KERNEL_OUT)/.config
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/arm/boot/zImage
KERNEL_IMG=$(KERNEL_OUT)/arch/arm/boot/Image

TARGET_PREBUILT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL).dtb

$(KERNEL_OUT):
	mkdir -p $(KERNEL_OUT)

$(KERNEL_CONFIG): $(KERNEL_OUT)
	$(MAKE) -C kernel/flo O=../../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-eabi- $(KERNEL_DEFCONFIG)


$(TARGET_PREBUILT_KERNEL): $(KERNEL_OUT) $(KERNEL_CONFIG)
	$(MAKE) -C kernel/flo O=../../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-eabi-
	$(MAKE) -C kernel/flo O=../../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-eabi- qcom-apq8064-ifc6410.dtb
	cat kernel/flo/fixup.bin $(TARGET_PREBUILT_INT_KERNEL) $(KERNEL_OUT)/arch/arm/boot/dts/qcom-apq8064-ifc6410.dtb > $(TARGET_PREBUILT_KERNEL)

endif
