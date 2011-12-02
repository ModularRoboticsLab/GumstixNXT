#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>

#include "../level_shifter/level_shifter.h"

#define DEVICE_NAME "level_shifter_test"

#define NUMBER_OF_DEVICES 1

struct level_shifter_test_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
};

static struct level_shifter_test_dev level_shifter_test_dev;

static ssize_t level_shifter_test_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
  register_use_of_level_shifter();

  return 0;
}

static ssize_t level_shifter_test_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp) {
  unregister_use_of_level_shifter();

  return count;
}

static const struct file_operations level_shifter_test_fops = {
	.owner =	THIS_MODULE,
	.read = 	level_shifter_test_read,
	.write = level_shifter_test_write,
};

static int __init level_shifter_test_init_cdev(void)
{
	int error;

	level_shifter_test_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&level_shifter_test_dev.devt, 0, NUMBER_OF_DEVICES, DEVICE_NAME);
	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", 
			error);
		return -1;
	}

	cdev_init(&level_shifter_test_dev.cdev, &level_shifter_test_fops);
	level_shifter_test_dev.cdev.owner = THIS_MODULE;

	error = cdev_add(&level_shifter_test_dev.cdev, level_shifter_test_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(level_shifter_test_dev.devt, 1);
		return -1;
	}	

	return 0;
}

static int __init level_shifter_test_init_class(void)
{
	level_shifter_test_dev.class = class_create(THIS_MODULE, DEVICE_NAME);

	if (IS_ERR(level_shifter_test_dev.class)) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(level_shifter_test_dev.class, NULL, level_shifter_test_dev.devt, NULL, DEVICE_NAME)) {
		printk(KERN_ALERT "device_create(..., %s) failed\n", DEVICE_NAME);
		class_destroy(level_shifter_test_dev.class);
		return -1;
	}

	return 0;
}

static int __init level_shifter_test_init(void)
{
	memset(&level_shifter_test_dev, 0, sizeof(level_shifter_test_dev));

	if (level_shifter_test_init_cdev() < 0) 
		goto fail_1;

	if (level_shifter_test_init_class() < 0)  
		goto fail_2;

	return 0;

fail_2:
	cdev_del(&level_shifter_test_dev.cdev);
	unregister_chrdev_region(level_shifter_test_dev.devt, 1);

fail_1:
	return -1;
}
module_init(level_shifter_test_init);

static void __exit level_shifter_test_exit(void)
{
	device_destroy(level_shifter_test_dev.class, level_shifter_test_dev.devt);
	class_destroy(level_shifter_test_dev.class);

	cdev_del(&level_shifter_test_dev.cdev);
	unregister_chrdev_region(level_shifter_test_dev.devt, 1);
}
module_exit(level_shifter_test_exit);
MODULE_AUTHOR("Group1");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

