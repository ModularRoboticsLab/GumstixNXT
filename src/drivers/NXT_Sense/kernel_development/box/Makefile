NAME := box

# If KERNELRELEASE is defined, we've been invoked from the 
# kernel build system and can use its language. 
ifneq ($(KERNELRELEASE),)
	obj-m := ${NAME}.o
# Otherwise we were called directly from the command 
# line; invoke the kernel build system. 
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build 
	PWD := $(shell pwd)
default: 
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

setup :
	sudo rmmod $(NAME); sudo insmod $(NAME).ko

.PHONY: clean distclean
clean:
	rm ${NAME}.mod.c ${NAME}.mod.o ${NAME}.o modules.order Module.symvers

distclean: clean
	rm ${NAME}.ko