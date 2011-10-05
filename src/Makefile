# Environment variables that must be configured to your specific setup
device := /dev/sdc
img := $(OVEROTOP)/tmp/deploy/glibc/images/overo

# Misc variables, not normally part of configuration
home := $(PWD)

umount:
	sudo umount $(device)1
	sudo umount $(device)2

format:
	read -p "Ready to format $(device)"
	sudo mkfs.vfat -F 32 $(device)1 -n FAT
	sudo mkfs.ext3 $(device)2

build:
	echo "(No build tasks included.)"

archive: build
	(cd $(home); rm -f export.tgz; tar --exclude '*.svn*' -zcvf export.tgz export/)

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
