obj-m += rk_pcie_drv.o

KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?= arm64
CROSS_COMPILE ?=

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
