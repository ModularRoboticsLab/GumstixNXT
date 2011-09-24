# cross-compile module makefile

ifneq ($(KERNELRELEASE),)
    obj-m := leddev.o
else
    PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

install:
	cp leddev.ko /mnt/oe/home/export

clean:
	rm -rf *~ *.ko *.o *.mod.c modules.order Module.symvers .leddev* .tmp_versions

endif



