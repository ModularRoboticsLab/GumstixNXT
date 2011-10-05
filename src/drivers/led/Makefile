# Cross-compile module makefile for leddev

# Target directory for "install", customize
INSTALLDIR=/mnt/oe/home/export
# Kernel module files to copiler
MODULES=leddev.o onebitleddev.o

# Generic kernel modoule makefile
ifneq ($(KERNELRELEASE),)
    obj-m := §(MODULES)
else
    PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

install:
	cp $(MODULES) $(INSTALLDIR)

clean:
	rm -rf *~ *.ko *.o *.mod.c modules.order Module.symvers .leddev* .tmp_versions

endif



