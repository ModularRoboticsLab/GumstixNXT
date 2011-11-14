/*
 * adc128s022.c
 *
 * Copyright Peter Madsen, 2011
 *
 * SPI-interface for ADC128S022.
 * Exports static int adc128s022_read_channel(int channel), useful in other
 * LEGO modules, for reading sensor specific analog data.
 *
 * Prerequisites:
 * Set DIR and OE on level converters:
 *  : sh init_lvlconvs.sh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

#define SPI_BUFF_SIZE	2
#define USER_BUFF_SIZE 128

#define SPI_BUS 1
#define SPI_BUS_CS0 0
#define SPI_BUS_SPEED 1000000
#define DEFAULT_SENSOR_PORT 0
#define DTIME HZ/100

// FIXME: Module fails if renamed to anything by 'spike' - Don't know why
const char this_driver_name[] = "spike";

struct adc128s022_control
{
	struct spi_message msg;
	struct spi_transfer transfer;
	u8 *tx_buff;
	u8 *rx_buff;
};

static struct adc128s022_control adc128s022_ctl;

struct adc128s022_dev
{
	struct semaphore spi_sem;
	struct semaphore fop_sem;
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct spi_device *spi_device;
	char *user_buff;
	u8 test_data;
	int cdev_channel;
};

static struct adc128s022_dev adc128s022_dev;

static int
adc128s022_read_channel(int channel)
{
	int status, rx_buf;

	if (down_interruptible(&adc128s022_dev.spi_sem))
		return -ERESTARTSYS;

	if (!adc128s022_dev.spi_device) {
		up(&adc128s022_dev.spi_sem);
		return -ENODEV;
	}

	spi_message_init(&adc128s022_ctl.msg);

	/* put some changing values in tx_buff for testing */
	adc128s022_ctl.tx_buff[0] = ((channel) << 3);
	adc128s022_ctl.tx_buff[1] = 0x00;

	// memset(adc128s022_ctl.rx_buff, 0, SPI_BUFF_SIZE);

	adc128s022_ctl.transfer.tx_buf = adc128s022_ctl.tx_buff;
	adc128s022_ctl.transfer.rx_buf = adc128s022_ctl.rx_buff;
	adc128s022_ctl.transfer.len = 2;

	spi_message_add_tail(&adc128s022_ctl.transfer, &adc128s022_ctl.msg);

	status = spi_sync(adc128s022_dev.spi_device, &adc128s022_ctl.msg);

	rx_buf = be16_to_cpu((
					adc128s022_ctl.rx_buff[1] << 8) |
					adc128s022_ctl.rx_buff[0]);

	up(&adc128s022_dev.spi_sem);

	if (status < 0)
		printk(KERN_ALERT "ERR: adc128s022_read_channel(%d) returns %d",
				channel, status);

	return (status ? status : rx_buf);
}
EXPORT_SYMBOL(adc128s022_read_channel);

static int
adc128s022_open(struct inode *inode, struct file *filp)
{
	int status = 0;

	if (down_interruptible(&adc128s022_dev.fop_sem))
		return -ERESTARTSYS;

	if (!adc128s022_dev.user_buff) {
		adc128s022_dev.user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!adc128s022_dev.user_buff)
			status = -ENOMEM;
	}

	up(&adc128s022_dev.fop_sem);

	return status;
}

static ssize_t
adc128s022_read(struct file *filp, char __user *buff,
		size_t count, loff_t *offp)
{
	/*
	 * TODO: Make 8 minors with independent reads, rather than reading 8 channels in this one
	 */
	size_t len;
	ssize_t status = 0;
	int raw;
	int readings[8], i;

	if (!buff)
		return -EFAULT;

	if (*offp > 0)
		return 0;

	if (down_interruptible(&adc128s022_dev.fop_sem))
		return -ERESTARTSYS;

	for (i = 0; i < 9; i++) {
		raw = adc128s022_read_channel(i);
		if (raw < 0) {
			sprintf(
					adc128s022_dev.user_buff,
					"adc128s022_do_one_message failed : %d\n",
					raw);
			status = raw;
		} else
			readings[i] = raw;
	}
	if (!status)
		sprintf(adc128s022_dev.user_buff,
				"CH:\t1\t2\t3\t4\t5\t6\t7\t8\nVAL:\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
				readings[0], readings[1], readings[2], readings[3],
				readings[4], readings[5], readings[6], readings[7]);

	len = strlen(adc128s022_dev.user_buff);

	if (copy_to_user(buff, adc128s022_dev.user_buff, len)) {
		printk(KERN_ALERT "adc128s022_read(): copy_to_user() failed\n");
		status = -EFAULT;
	} else {
		*offp += count;
		status = count;
	}

	up(&adc128s022_dev.fop_sem);
	return status;
}

static int
adc128s022_probe(struct spi_device *spi_device)
{
	if (down_interruptible(&adc128s022_dev.spi_sem))
		return -EBUSY;

	adc128s022_dev.spi_device = spi_device;

	up(&adc128s022_dev.spi_sem);

	return 0;
}

static int
adc128s022_remove(struct spi_device *spi_device)
{
	if (down_interruptible(&adc128s022_dev.spi_sem))
		return -EBUSY;

	adc128s022_dev.spi_device = NULL;

	up(&adc128s022_dev.spi_sem);

	return 0;
}

static int __init
add_adc128s022_device_to_bus(void)
{
	struct spi_master *spi_master;
	struct spi_device *spi_device;
	struct device *pdev;
	char buff[64];
	int status = 0;

	spi_master = spi_busnum_to_master(SPI_BUS);
	if (!spi_master) {
		printk(KERN_ALERT "spi_busnum_to_master(%d) returned NULL\n", SPI_BUS);
		printk(KERN_ALERT "Missing modprobe omap2_mcspi?\n");
		return -ENXIO;
	}

	spi_device = spi_alloc_device(spi_master);
	if (!spi_device) {
		put_device(&spi_master->dev);
		printk(KERN_ALERT "spi_alloc_device() failed\n");
		return -ENODEV;
	}

	spi_device->chip_select = SPI_BUS_CS0;

	/* Check whether this SPI bus.cs is already claimed */
	snprintf(buff, sizeof(buff), "%s.%u",
			dev_name(&spi_device->master->dev),
			spi_device->chip_select);

	pdev = bus_find_device_by_name(spi_device->dev.bus, NULL, buff);
	if (pdev) {
		/* We are not going to use this spi_device, so free it */
		spi_dev_put(spi_device);

		/*
		 * There is already a device configured for this bus.cs
		 * It is okay if it us, otherwise complain and fail.
		 */
		if (pdev->driver && pdev->driver->name &&
				strcmp(this_driver_name, pdev->driver->name)) {
			printk(KERN_ALERT "Driver [%s] already registered for %s\n",
					pdev->driver->name, buff);
			status = -1;
		}
	}else{
		spi_device->max_speed_hz = SPI_BUS_SPEED;
		spi_device->mode = SPI_MODE_0;
		spi_device->bits_per_word = 8;
		spi_device->irq = -1;
		spi_device->controller_state = NULL;
		spi_device->controller_data = NULL;
		strlcpy(spi_device->modalias, this_driver_name, SPI_NAME_SIZE);

		status = spi_add_device(spi_device);
		if (status < 0)	{
			spi_dev_put(spi_device);
			printk(KERN_ALERT "spi_add_device() failed: %d\n",	status);
		}
	}

	put_device(&spi_master->dev);

	return status;
}

static struct spi_driver adc128s022_driver = {
	.driver = {
		.name = this_driver_name,
		.owner = THIS_MODULE
	},
	.probe = adc128s022_probe,
	.remove = __devexit_p(adc128s022_remove)
};

static int __init
adc128s022_init_spi(void)
{
	int error;

	adc128s022_ctl.tx_buff = kmalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!adc128s022_ctl.tx_buff) {
		error = -ENOMEM;
		goto adc128s022_mem_tx_error;
	}

	adc128s022_ctl.rx_buff = kmalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!adc128s022_ctl.rx_buff) {
		error = -ENOMEM;
		goto adc128s022_mem_rx_error;
	}

	error = spi_register_driver(&adc128s022_driver);
	if (error < 0) {
		printk(KERN_ALERT "spi_register_driver() failed %d\n", error);
		goto adc128s022_spi_error;
	}

	error = add_adc128s022_device_to_bus();
	if (error < 0) {
		printk(KERN_ALERT "add_adc128s022_to_bus() failed\n");
		goto adc128s022_dev_error;
	}

	return 0;

adc128s022_dev_error:
	spi_unregister_driver(&adc128s022_driver);
adc128s022_spi_error:
	kfree(adc128s022_ctl.rx_buff);
adc128s022_mem_rx_error:
	kfree(adc128s022_ctl.tx_buff);
adc128s022_mem_tx_error:
	return error;
}

static const struct file_operations adc128s022_fops = {
	.owner = THIS_MODULE,
	.read = adc128s022_read,
	.open =	adc128s022_open
};

static int __init
adc128s022_init_cdev(void)
{
	int error;

	adc128s022_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&adc128s022_dev.devt, 0, 1, this_driver_name);
	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", error);
		return -1;
	}

	cdev_init(&adc128s022_dev.cdev, &adc128s022_fops);
	adc128s022_dev.cdev.owner = THIS_MODULE;

	error = cdev_add(&adc128s022_dev.cdev, adc128s022_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(adc128s022_dev.devt, 1);
		return -1;
	}

	return 0;
}

static int __init
adc128s022_init_class(void)
{

	adc128s022_dev.class = class_create(THIS_MODULE, this_driver_name);

	if (!adc128s022_dev.class) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(adc128s022_dev.class,
			NULL, adc128s022_dev.devt, NULL, this_driver_name)) {
		printk(KERN_ALERT "device_create(..., %s) failed\n", this_driver_name);
		class_destroy(adc128s022_dev.class);
		return -1;
	}

	return 0;
}

static int __init
adc128s022_init(void)
{
	memset(&adc128s022_dev, 0, sizeof(adc128s022_dev));
	memset(&adc128s022_ctl, 0, sizeof(adc128s022_ctl));

	sema_init(&adc128s022_dev.spi_sem, 1);
	sema_init(&adc128s022_dev.fop_sem, 1);

	if (adc128s022_init_cdev() < 0)
		goto fail_1;

	if (adc128s022_init_class() < 0)
		goto fail_2;

	if (adc128s022_init_spi() < 0)
		goto fail_3;

	return 0;

	fail_3: device_destroy(adc128s022_dev.class, adc128s022_dev.devt);
	class_destroy(adc128s022_dev.class);

	fail_2: cdev_del(&adc128s022_dev.cdev);
	unregister_chrdev_region(adc128s022_dev.devt, 1);

	fail_1: return -1;
}
module_init(adc128s022_init);

static void __exit
adc128s022_exit(void)
{
	spi_unregister_driver(&adc128s022_driver);

	device_destroy(adc128s022_dev.class, adc128s022_dev.devt);
	class_destroy(adc128s022_dev.class);

	cdev_del(&adc128s022_dev.cdev);
	unregister_chrdev_region(adc128s022_dev.devt, 1);

	if (adc128s022_ctl.tx_buff)
		kfree(adc128s022_ctl.tx_buff);

	if (adc128s022_ctl.rx_buff)
		kfree(adc128s022_ctl.rx_buff);

	if (adc128s022_dev.user_buff)
		kfree(adc128s022_dev.user_buff);
}
module_exit(adc128s022_exit);

MODULE_AUTHOR("Peter Madsen");
MODULE_DESCRIPTION("adc128s022 module");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");

