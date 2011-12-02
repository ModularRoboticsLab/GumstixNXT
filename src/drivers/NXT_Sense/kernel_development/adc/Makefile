# cross-compile module makefile
NAME := adc

ifneq ($(KERNELRELEASE),)
    obj-m := $(NAME).o
else
    PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

install:
	cp $(NAME).ko $(EMB4ROOT)/export/own_modules

.PHONY: clean
clean:
	-rm $(NAME).o $(NAME).ko $(NAME).mod.c $(NAME).mod.o .$(NAME).mod.o.cmd .$(NAME).ko.cmd modules.order

endif



