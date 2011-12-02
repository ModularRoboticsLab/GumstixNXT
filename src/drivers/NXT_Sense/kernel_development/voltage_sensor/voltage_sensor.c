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

#define DEVICE_NAME "voltage_sensor"

#define ADC_CHANNEL_VOLTAGE_SENSOR 4

#define NUMBER_OF_DEVICES 1

struct voltage_sensor_dev {
  dev_t devt;
  struct cdev cdev;
  struct class *class;
  struct device *device;
};

static struct voltage_sensor_dev voltage_sensor_dev;

/***********************************************************************
 *
 * File operations for the /dev/voltage_sensor file
 *
 ***********************************************************************/
static ssize_t voltage_sensor_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
  size_t len;
  ssize_t status = 0;

  char output[6]; /* 12bit ADC translates to 4 digits + newline and the null-character */
  int sample_value = 0;
  int adc_sample_status;

  if (!buff) 
    return -EFAULT;

  if (*offp > 0) {
    return 0;
  }
	
  adc_sample_status = adc_sample_channel(ADC_CHANNEL_VOLTAGE_SENSOR, &sample_value);

  if (adc_sample_status != 0) {
    /* Signal I/O error */
    /* Could also use the error code given from adc_sample_channel... */
    printk(KERN_ERR DEVICE_NAME ": error while sampling adc channel %d: %d\n", ADC_CHANNEL_VOLTAGE_SENSOR, adc_sample_status);
    return -EIO;
  }

  sprintf(output, "%4.d\n", sample_value);

  len = strlen(output);
 
  if (len < count) 
    count = len;

  if (copy_to_user(buff, output, count))  {
    printk(KERN_ERR DEVICE_NAME ": copy_to_user in voltage_sensor_read failed\n");
    status = -EFAULT;
  } else {
    *offp += count;
    status = count;
  }

  return status;	
}

static const struct file_operations voltage_sensor_fops = {
  .owner =	THIS_MODULE,
  .read = 	voltage_sensor_read,
};

/***********************************************************************
 *
 * Module initialisation and teardown functions
 *
 ***********************************************************************/
static int __init voltage_sensor_init_cdev(void) {
  int error;

  voltage_sensor_dev.devt = MKDEV(0, 0);

  error = alloc_chrdev_region(&voltage_sensor_dev.devt, 0, NUMBER_OF_DEVICES, DEVICE_NAME);
  if (error < 0) {
    printk(KERN_CRIT DEVICE_NAME ": alloc_chrdev_region() failed: %d\n", 
	   error);
    return -1;
  }

  cdev_init(&voltage_sensor_dev.cdev, &voltage_sensor_fops);
  voltage_sensor_dev.cdev.owner = THIS_MODULE;

  error = cdev_add(&voltage_sensor_dev.cdev, voltage_sensor_dev.devt, 1);
  if (error < 0) {
    printk(KERN_CRIT DEVICE_NAME ": cdev_add() failed: %d\n", error);
    unregister_chrdev_region(voltage_sensor_dev.devt, 1);
    return -1;
  }	

  return 0;
}

static int __init voltage_sensor_init_class(void) {
  voltage_sensor_dev.class = class_create(THIS_MODULE, DEVICE_NAME);

  if (IS_ERR(voltage_sensor_dev.class)) {
    printk(KERN_CRIT DEVICE_NAME ": class_create() failed: %ld\n", PTR_ERR(voltage_sensor_dev.class));
    return -1;
  }

  voltage_sensor_dev.device = device_create(voltage_sensor_dev.class, NULL, voltage_sensor_dev.devt, NULL, DEVICE_NAME);
  if (IS_ERR(voltage_sensor_dev.device)) {
    printk(KERN_CRIT DEVICE_NAME ": device_create() failed: %ld\n", PTR_ERR(voltage_sensor_dev.device));
    class_destroy(voltage_sensor_dev.class);
    return -1;
  }

  return 0;
}

static int __init voltage_sensor_init(void) {
  memset(&voltage_sensor_dev, 0, sizeof(voltage_sensor_dev));

  if (voltage_sensor_init_cdev() < 0) 
    goto fail_1;

  if (voltage_sensor_init_class() < 0)  
    goto fail_2;

  return 0;

 fail_2:
  cdev_del(&voltage_sensor_dev.cdev);
  unregister_chrdev_region(voltage_sensor_dev.devt, 1);

 fail_1:
  return -1;
}
module_init(voltage_sensor_init);

static void __exit voltage_sensor_exit(void) {
  device_destroy(voltage_sensor_dev.class, voltage_sensor_dev.devt);
  class_destroy(voltage_sensor_dev.class);

  cdev_del(&voltage_sensor_dev.cdev);
  unregister_chrdev_region(voltage_sensor_dev.devt, 1);
}
module_exit(voltage_sensor_exit);

MODULE_LICENSE("GPL");
