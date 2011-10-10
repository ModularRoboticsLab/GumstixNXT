# Cross-compile module makefile for leddev

# Target directory for "install", customize
INSTALLDIR=../../export/
# Kernel module files to copiler
MODULES=leddev.o onebitleddev.o

# Generic kernel module makefile

RESULT=$(MODULES:.o=.ko)

ifneq ($(KERNELRELEASE),)
    obj-m := $(MODULES)
else
    PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined, see kernel-dev-env.txt")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

install:
	mkdir -p $(INSTALLDIR)/drivers
	cp $(RESULT) $(INSTALLDIR)/drivers

clean:
	rm -rf *~ *.ko *.o *.mod.c modules.order Module.symvers .leddev* .tmp_versions

endif



