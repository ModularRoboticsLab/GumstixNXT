/* TODO:
 * - Remove the knowledge of which port an instance of this light_data / light sensor is using:
 *     - Maybe make use of a linked list instead of a static defined array with 4 light_data objects.
 *     - Create the light_data objects dynamically and free them again to avoid wasting memory.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

#include "light.h"

#include "nxt_sense_core.h"

#define DEVICE_NAME "light"

#define DEFAULT_LED_VALUE 0
/* nxt_sense_device_data has to be placed at the top/front of the struct, in order of having polymorphy in C - makes it possible to obtain a pointer to light_data using only one container_of macro on nxt_sense_device_data in the device open, read and release calls/fileoperations */
struct light_data {
  struct nxt_sense_device_data nxt_sense_device_data; /* Has to be placed at the beginning, see comment above! */
  struct device_attribute dev_attr_led;
  int port;
  int led;
  struct mutex mutex;
};

static struct light_data light_data[4];

/***********************************************************************
 *
 * File operations for the /dev/light# files
 *
 ***********************************************************************/
static int light_open(struct inode *inode, struct file *filp) {
  struct light_data *ld;

  ld = (struct light_data *) container_of(inode->i_cdev, struct nxt_sense_device_data, cdev);

  if (!mutex_trylock(&ld->mutex)) {
    return -EBUSY;
  }

  filp->private_data = ld;

  return 0;
}

static int light_release(struct inode *inode, struct file *filp) {
  struct light_data *ld;
  ld = (struct light_data *) container_of(inode->i_cdev, struct nxt_sense_device_data, cdev);

  mutex_unlock(&ld->mutex);

  return 0;
}

/* The concurrency aware logic is in light_open and light_release */
static ssize_t light_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
  size_t len;
  ssize_t status = 0;

  char output[6]; /* 12bit ADC translates to 4 digits + newline and the null-character */
  int data = 0;
  int status_sampling;
  struct light_data *ld = filp->private_data;

  status_sampling = ld->nxt_sense_device_data.get_sample(&data);

  snprintf(output, 6, "%4.d\n", data);

  if (!buff)
    return -EFAULT;

  if (*offp > 0) {
    return 0;
  }

  len = strlen(output);

  if (len < count)
    count = len;

  if (copy_to_user(buff, output, count)) {
    return -EFAULT;
  } else {
    *offp += count;
    status = count;
  }

  return status;
}

static const struct file_operations light_fops = {
  .owner = THIS_MODULE,
  .read = light_read,
  .open = light_open,
  .release = light_release,
};

/***********************************************************************
 *
 * Sysfs entries for reading and setting the state of the led on the
 * light sensor.
 *
 ***********************************************************************/
static ssize_t led_show(struct device *dev, struct device_attribute *attr, char *buf) {
  int led;
  struct light_data *ld;
  ld = container_of(attr, struct light_data, dev_attr_led);

  if (!mutex_trylock(&ld->mutex)) {
    return -EBUSY;
  }

  led = ld->led;

  mutex_unlock(&ld->mutex);

  return scnprintf(buf, PAGE_SIZE, "%d\n", led);
}

static ssize_t led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
  int new_led = 0;
  int res = 0;
  struct light_data *ld;
  ld = container_of(attr, struct light_data, dev_attr_led);

  res = sscanf(buf, "%d", &new_led);

  if (res != 1) {
    printk(KERN_WARNING DEVICE_NAME "%d: wrong sysfs input for led, only takes a value for the led\n", MINOR(ld->nxt_sense_device_data.devt));
  } else if (new_led != 0 && new_led != 1) {
    printk(KERN_WARNING DEVICE_NAME "%d: the sysfs input for led is supposed to be 0 or 1, but was: %d\n", MINOR(ld->nxt_sense_device_data.devt), new_led);
  } else {
    /* See comments in touch.c for why to use mutex_lock instead of mutex_trylock and return -EBUSY */
    mutex_lock(&ld->mutex);
    
    ld->led = new_led;

    /* Call the function to activate the led or deactivate it */
    ld->nxt_sense_device_data.scl((new_led == 0 ? SCL_LOW : SCL_HIGH));
    
    mutex_unlock(&ld->mutex);
  }

  return count;
}

/***********************************************************************
 *
 * Utility functions for actually setting up the sysfs entries
 * and removing them again.
 *
 ***********************************************************************/
static int init_sysfs(struct light_data *ld) {
  int error = 0;

/* Manually doing what the macro DEVICE_ATTR is doing behind the scenes, but working around it for having a structure for each light_data */
  ld->dev_attr_led.attr.name = __stringify(led);
  ld->dev_attr_led.attr.mode = (S_IRUGO | S_IWUSR);
  ld->dev_attr_led.show = led_show;
  ld->dev_attr_led.store = led_store;

  error = device_create_file(ld->nxt_sense_device_data.device, &ld->dev_attr_led);
  if (error != 0) {
    printk(KERN_ALERT DEVICE_NAME "%d: device_create_file(led) error: %d\n", MINOR(ld->nxt_sense_device_data.devt), error);
    return -1;
  }

  return 0;
}

static int destroy_sysfs(struct light_data *ld) {
  device_remove_file(ld->nxt_sense_device_data.device, &ld->dev_attr_led);

  return 0;
}

/***********************************************************************
 *
 * Hooks for adding and removing devices for the light sensor submodule,
 * called from nxt_sense_core.c
 *
 ***********************************************************************/
int add_light_sensor(int port, dev_t devt) {
  int res;
  int error;
  printk(KERN_DEBUG DEVICE_NAME ": Adding light sensor on port %d\n", port);

  mutex_init(&light_data[port].mutex);
  mutex_lock(&light_data[port].mutex); /* void, so sleeps until the lock is acquired? mutex_lock_interruptible returns an error indicating it was interrupted... */

  light_data[port].nxt_sense_device_data.devt = devt;

  res = nxt_setup_sensor_chrdev(&light_fops, &light_data[port].nxt_sense_device_data, DEVICE_NAME);

  printk(KERN_DEBUG DEVICE_NAME ": return value for nxt_setup_sensor_chrdev: %d\n", res);

  light_data[port].port = port;
  light_data[port].led = DEFAULT_LED_VALUE;

  error = init_sysfs(&light_data[port]);
  mutex_unlock(&light_data[port].mutex);
  if (error != 0) {
    /* error .... handle this if you want */
    return -1;
  }

  return res;
}

static int uninitialise_light_data(struct light_data *ld) {
  mutex_destroy(&ld->mutex);
  ld->port = 0;
  ld->led = DEFAULT_LED_VALUE;

  return 0;
}

int remove_light_sensor(int port) {
  int res;
  printk(KERN_DEBUG DEVICE_NAME ": Removing light sensor on port %d\n", port);

  destroy_sysfs(&light_data[port]);

  res = nxt_teardown_sensor_chrdev(&light_data[port].nxt_sense_device_data);

  uninitialise_light_data(&light_data[port]);

  return res;
}

MODULE_LICENSE("GPL");
