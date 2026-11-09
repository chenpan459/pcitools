obj-m:=pcie-rkep.o
KERNELDIR:=/usr/src/linux-headers-`uname -r`/
PWD :=$(shell pwd)
modules:
	$(MAKE)  -C  $(KERNELDIR)  M=$(PWD)  modules
clean:
	rm -rf *o *.mod.c *.order *.symvers *.mod
