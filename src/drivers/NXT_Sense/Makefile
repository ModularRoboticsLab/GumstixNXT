#Setup the SD-card reader
device := /dev/sdb
#The location of the folders with the device drivers
home := $(EMB4ROOT)
#The location of the Open Embedded
img := $(OVEROTOP)/tmp/deploy/glibc/images/overo

umount:
	sudo umount $(device)1
	sudo umount $(device)2

format:
	read -p "Ready to format $(device)"
	sudo mkfs.vfat -F 32 $(device)1 -n FAT
	sudo mkfs.ext3 $(device)2

build:
	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/leddev; make; make install)
	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/nxtts; make; make install)
#	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/adc; make; make install)
#	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/adc_test; make; make install)
	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/; make; make install)

archive: build
	(cd $(home); rm export.tgz; cd export; tar --exclude '*.svn*' --exclude '.gitignore' -zcvf ../export.tgz .)

deploy: archive
	echo "Deploying from $(OVEROTOP) to $(device)"
	sudo mkdir -p /media/card
	sudo mount $(device)1 /media/card 
	sudo cp $(img)/MLO-overo /media/card/MLO
	sudo cp $(img)/u-boot-overo.bin /media/card/u-boot.bin 
	sudo cp $(img)/uImage-overo.bin /media/card/uImage 
	sudo umount $(device)1
	sudo mount $(device)2 /media/card
	(cd /media/card; sudo tar xvaf $(OVEROTOP)/tmp/deploy/glibc/images/overo/minimalist-image-overo.tar.bz2)
	(cd /media/card; sudo tar -zxvf $(home)/export.tgz)
	sudo umount $(device)2

xport: archive
	sudo mount $(device)2 /media/card
	(cd /media/card; sudo rm -rf export; sudo tar -zxvf $(home)/export.tgz)
	sudo umount $(device)2

default:
	echo "Valid targets are archive, build, deploy, format, umount, xport"

.PHONY: clean_modules
# clean_modules should contain the same lines as in the make target build
clean_modules:
	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/leddev; make; make clean)
	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/nxtts; make; make clean)
	(source $(home)/env/kernel-dev.txt; cd $(home)/kernel_development/; make; make clean)
