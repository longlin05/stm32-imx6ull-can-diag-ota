obj-m += dht22_iio.o

KDIR ?= /home/lin/imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga
ARCH ?= arm
CROSS_COMPILE ?= arm-poky-linux-gnueabi-

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

.PHONY: all clean
