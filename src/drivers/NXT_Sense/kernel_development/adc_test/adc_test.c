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

#include "../adc/adc.h"

#define USER_BUFF_SIZE 128

#define DEVICE_NAME "adc_test"

#define NUMBER_OF_DEVICES 1

struct adc_test_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	char user_buff[USER_BUFF_SIZE];
};

static struct adc_test_dev adc_test_dev;

static ssize_t adc_test_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	size_t len;
	ssize_t status = 0;

	int sample_value;
	int adc_sample_status;

	if (!buff) 
		return -EFAULT;

	if (*offp > 0){
		printk(KERN_DEBUG "offp: %lld",*offp); 
		return 0;
	}
	
	adc_sample_status = adc_sample_channel(1, &sample_value);

	sprintf(adc_test_dev.user_buff, "ADC status: %d\nValue: %d\n", adc_sample_status, sample_value);

	len = strlen(adc_test_dev.user_buff);
 
	if (len < count) 
		count = len;

	if (copy_to_user(buff, adc_test_dev.user_buff, count))  {
		printk(KERN_DEBUG "adc_test_read(): copy_to_user() failed\n");
		status = -EFAULT;
	} else {
		*offp += count;
		status = count;
	}

	return status;	
}

static const struct file_operations adc_test_fops = {
	.owner =	THIS_MODULE,
	.read = 	adc_test_read,
};

static int __init adc_test_init_cdev(void)
{
	int error;

	adc_test_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&adc_test_dev.devt, 0, NUMBER_OF_DEVICES, DEVICE_NAME);
	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", 
			error);
		return -1;
	}

	cdev_init(&adc_test_dev.cdev, &adc_test_fops);
	adc_test_dev.cdev.owner = THIS_MODULE;

	error = cdev_add(&adc_test_dev.cdev, adc_test_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(adc_test_dev.devt, 1);
		return -1;
	}	

	return 0;
}

static int __init adc_test_init_class(void)
{
	adc_test_dev.class = class_create(THIS_MODULE, DEVICE_NAME);

	if (IS_ERR(adc_test_dev.class)) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(adc_test_dev.class, NULL, adc_test_dev.devt, NULL, DEVICE_NAME)) {
		printk(KERN_ALERT "device_create(..., %s) failed\n", DEVICE_NAME);
		class_destroy(adc_test_dev.class);
		return -1;
	}

	return 0;
}

static int __init adc_test_init(void)
{
	memset(&adc_test_dev, 0, sizeof(adc_test_dev));

	if (adc_test_init_cdev() < 0) 
		goto fail_1;

	if (adc_test_init_class() < 0)  
		goto fail_2;

	return 0;

fail_2:
	cdev_del(&adc_test_dev.cdev);
	unregister_chrdev_region(adc_test_dev.devt, 1);

fail_1:
	return -1;
}
module_init(adc_test_init);

static void __exit adc_test_exit(void)
{
	device_destroy(adc_test_dev.class, adc_test_dev.devt);
	class_destroy(adc_test_dev.class);

	cdev_del(&adc_test_dev.cdev);
	unregister_chrdev_region(adc_test_dev.devt, 1);
}
module_exit(adc_test_exit);
MODULE_AUTHOR("Group1");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

