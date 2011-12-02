ifneq ($(KERNELRELEASE),)
	obj-m := adc/ adc_test/ nxt_sense/ level_shifter/ voltage_sensor/
else
	PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif
endif

install:
	(cd adc; make install)
	(cd adc_test; make install)
	(cd nxt_sense; make install)
	(cd level_shifter; make install)
	(cd voltage_sensor; make install)

.PHONY: clean
clean:
	(cd adc; make clean)
	(cd adc_test; make clean)
	(cd nxt_sense; make clean)
	(cd level_shifter; make clean)
	(cd voltage_sensor; make clean)
