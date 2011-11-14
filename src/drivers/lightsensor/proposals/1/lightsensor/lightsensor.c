/*
 * lightsensor.c
 *
 * Copyright Peter Madsen, 2011
 *
 * Driver module for the GumstixNXT LEGO lightsensor.
 *
 * Prerequisites:
 * Requires adc128s022 SPI driver:
 * 	: insmod adc128s022.ko
 * Set DIR and OE on level converters:
 *  : sh init_lvlconvs.sh
 *
 * Usage:
 *  : insmod lightsensor port=P <delay=D>
 * P = physical port [1:4]
 * D = LED turn on/off time in ms (differential sampling, for aided ambient
 * light cancelling
 * D = 0, LED always on
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
#include "sensor_gpio.h"

#define USER_BUFF_SIZE 128

int port;
module_param(port, int, S_IRUGO);
MODULE_PARM_DESC(port, "Lightsensor attached to sensorport [1:4]");

int delay;
module_param(delay, int, S_IRUGO);
MODULE_PARM_DESC(delay, "LED turn on/off time [ms]");

const char this_driver_name[] = "lightsensor";

struct lightsensor_control
{
	struct spi_message msg;
	struct spi_transfer transfer;
	u8 *tx_buff;
	u8 *rx_buff;
};

static struct lightsensor_control my_ctl;

struct lightsensor_dev
{
	struct semaphore fop_sem;
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	char *user_buff;
	u8 test_data;
	long jiffies_off;
};

static struct lightsensor_dev my_dev;

static ssize_t lightsensor_read(struct file *filp, char __user *buff,
		size_t count, loff_t *offp)
{
	size_t len;
	ssize_t status = 0;
	long j;
	int raw;
	int light, nolight;

	if (!buff)
		return -EFAULT;

	if (*offp > 0)
		return 0;

	if (down_interruptible(&(my_dev.fop_sem)))
		return -ERESTARTSYS;

	if (delay) {
		if (gpio_get_value(sensorport[port - 1].scl)) {
			gpio_set_value(sensorport[port - 1].scl, 0);
			my_dev.jiffies_off = jiffies;
		}
		while (jiffies < my_dev.jiffies_off + delay)
			/* nop */;

		printk(KERN_ALERT "Reading...");
		raw = adc128s022_read_channel(sensorport[port - 1].adc_channel);

		if (raw < 0) {
			printk(KERN_ALERT "ERR: ADC read returned %d!", raw);
			return (ssize_t) raw;
		}

		nolight = raw;

		gpio_set_value(sensorport[port - 1].scl, 1);

		j = jiffies + delay;
		while (jiffies < j)
			/* nop */;

		raw = adc128s022_read_channel(sensorport[port - 1].adc_channel);

		if (raw < 0) {
			printk(KERN_ALERT "ERR: ADC read returned %d!", raw);
			return (ssize_t) raw;
		}

		gpio_set_value(sensorport[port - 1].scl, 0);
		my_dev.jiffies_off = jiffies;

		light = raw;

		sprintf(my_dev.user_buff, "%d", nolight - light);
	} else {
		raw = adc128s022_read_channel(sensorport[port - 1].adc_channel);

		if (raw < 0) {
			printk(KERN_ALERT "ERR: ADC read returned %d!", raw);
			return (ssize_t) raw;
		}

		sprintf(my_dev.user_buff, "%d", raw);
	}

	len = strlen(my_dev.user_buff);

	if (len < count)
		count = len;

	if (copy_to_user(buff, my_dev.user_buff, count)) {
		printk(KERN_ALERT "lightsensor_read(): copy_to_user() failed\n");
		status = -EFAULT;
	} else {
		*offp += count;
		status = count;
	}

	up(&(my_dev.fop_sem));

	return status;
}

static int lightsensor_open(struct inode *inode, struct file *filp)
{
	int status = 0;

	if (down_interruptible(&my_dev.fop_sem))
		return -ERESTARTSYS;

	if (!my_dev.user_buff) {
		my_dev.user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!my_dev.user_buff)
			status = -ENOMEM;
	}

	up(&my_dev.fop_sem);

	return status;
}
static const struct file_operations lightsensor_fops = {
		.owner = THIS_MODULE,
		.read = lightsensor_read,
		.open = lightsensor_open,
};

static int __init lightsensor_init_cdev(void)
{
	int error;

	my_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&my_dev.devt, 0, 1, this_driver_name);
	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n",
				error);
		return -1;
	}

	cdev_init(&my_dev.cdev, &lightsensor_fops);
	my_dev.cdev.owner = THIS_MODULE;

	error = cdev_add(&my_dev.cdev, my_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(my_dev.devt, 1);
		return -1;
	}

	return 0;
}

static int __init lightsensor_init_class(void)
{
	if (port < 1 || port > 4) {
		printk(KERN_ALERT "Error: please provide sensor \"port=[1:4]\"!\n");
		return -1;
	}

	my_dev.class = class_create(THIS_MODULE, this_driver_name);

	if (!my_dev.class) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(my_dev.class, NULL, my_dev.devt, NULL,
			this_driver_name)) {
		printk(KERN_ALERT "device_create(..., %s) failed\n", this_driver_name);
		class_destroy(my_dev.class);
		return -1;
	}

	return 0;
}

static int __init lightsensor_init(void)
{
	memset(&my_dev, 0, sizeof(my_dev));
	memset(&my_ctl, 0, sizeof(my_ctl));

	sema_init(&my_dev.fop_sem, 1);

	if (port < 0 || port > 4) {
		printk(KERN_ALERT "Please provide port=[1:4]!");
		goto fail_1;
	}

	if (gpio_request(sensorport[port - 1].scl, "LED")) {
		printk(KERN_ALERT "gpio_request failed\n");
		goto fail_1;
	}
	gpio_direction_output(sensorport[port - 1].scl, 0);

	if (delay)
		delay = HZ / (1000 / delay) + 1;

	gpio_set_value(sensorport[port - 1].scl, (delay ? 0 : 1));

	if (lightsensor_init_cdev() < 0)
		goto fail_1;

	if (lightsensor_init_class() < 0)
		goto fail_2;

	return 0;

//fail_3:
//	device_destroy(lightsensor_dev.class, lightsensor_dev.devt);
//	class_destroy(mydata.class);

	fail_2:
	cdev_del(&my_dev.cdev);
	unregister_chrdev_region(my_dev.devt, 1);

	fail_1:
	return -1;
}
module_init(lightsensor_init);

static void __exit lightsensor_exit(void)
{

	device_destroy(my_dev.class, my_dev.devt);
	class_destroy(my_dev.class);

	cdev_del(&my_dev.cdev);
	unregister_chrdev_region(my_dev.devt, 1);

	if (my_ctl.tx_buff)
		kfree(my_ctl.tx_buff);

	if (my_ctl.rx_buff)
		kfree(my_ctl.rx_buff);

	if (my_dev.user_buff)
		kfree(my_dev.user_buff);

	gpio_set_value(sensorport[port - 1].scl, 0);

	gpio_free(sensorport[port - 1].scl);
}
module_exit(lightsensor_exit);

MODULE_AUTHOR("Peter Madsen");
MODULE_DESCRIPTION("Lightsensor module");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
