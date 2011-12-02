/* TODO:
 * - Remove the knowledge of which port an instance of this touch_data / touch sensor is using:
 *     - Maybe make use of a linked list instead of a static defined array with 4 touch_data objects.
 *     - Create the touch_data objects dynamically and free them again to avoid wasting memory.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/mutex.h>

#include "touch.h"

#include "nxt_sense_core.h"

#define DEVICE_NAME "touch"
#define DEFAULT_THRESHOLD 2048

/* nxt_sense_device_data has to be placed at the top/front of the struct, in order of having polymorphy in C - makes it possible to obtain a pointer to touch_data using only one container_of macro on nxt_sense_device_data in the device open, read and release calls/fileoperations */
struct touch_data {
  struct nxt_sense_device_data nxt_sense_device_data; /* Has to be placed at the beginning, see comment above! */
  struct device_attribute dev_attr_threshold;
  struct device_attribute dev_attr_raw_sample;
  int port;
  int threshold;
  struct mutex mutex;
};

static struct touch_data touch_data[4];

/***********************************************************************
 *
 * File operations for the /dev/touch# files
 *
 ***********************************************************************/
static int touch_open(struct inode *inode, struct file *filp) {
  struct touch_data *td;

  td = (struct touch_data *) container_of(inode->i_cdev, struct nxt_sense_device_data, cdev);

  if (!mutex_trylock(&td->mutex)) {
    return -EBUSY;
  }

  filp->private_data = td;

  return 0;
}

static int touch_release(struct inode *inode, struct file *filp) {
  struct touch_data *td;
  td = (struct touch_data *) container_of(inode->i_cdev, struct nxt_sense_device_data, cdev);

  mutex_unlock(&td->mutex);

  return 0;
}

/* The concurrency aware logic is in touch_open and touch_release */
static ssize_t touch_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
  size_t len;
  ssize_t status = 0;

  char output[3];
  int data = 0;
  int status_sampling;
  struct touch_data *td = filp->private_data;

  status_sampling = td->nxt_sense_device_data.get_sample(&data);

  if (data < td->threshold) {
    data = 1;
  } else {
    data = 0;
  }

  snprintf(output, 3, "%1.d\n", data);

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

static const struct file_operations touch_fops = {
  .owner = THIS_MODULE,
  .read = touch_read,
  .open = touch_open,
  .release = touch_release,
};

/***********************************************************************
 *
 * Sysfs entries for reading and setting the threshold value
 *
 ***********************************************************************/
static ssize_t threshold_show(struct device *dev, struct device_attribute *attr, char *buf) {
  int threshold;
  struct touch_data *td;
  td = container_of(attr, struct touch_data, dev_attr_threshold);

  if (!mutex_trylock(&td->mutex)) {
    return -EBUSY;
  }

  threshold = td->threshold;

  mutex_unlock(&td->mutex);

  return scnprintf(buf, PAGE_SIZE, "%d\n", threshold);
}

static ssize_t threshold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
  int new_threshold = 0;
  int res = 0;
  struct touch_data *td;
  td = container_of(attr, struct touch_data, dev_attr_threshold);

  res = sscanf(buf, "%d", &new_threshold);

  if (res != 1) {
    printk(KERN_WARNING DEVICE_NAME "%d: wrong sysfs input for threshold, only takes a value for the threshold\n", MINOR(td->nxt_sense_device_data.devt));
  } else {
    /* not looking at echo return values by default, so when the return value is not shown to the user, it requires a read of the value to confirm that it was actually set (the device was not busy) -- so just doing a sleep wait
    if (!mutex_trylock(&td->mutex)) {
      return -EBUSY;
      }*/
    mutex_lock(&td->mutex);
    
    td->threshold = new_threshold;
    
    mutex_unlock(&td->mutex);
  }

  return count;
}

/***********************************************************************
 *
 * Sysfs entries for reading the raw sample value
 *
 ***********************************************************************/
/* NOTE: Should it have another error handling return -1 instead of printing -1? */
static ssize_t raw_sample_show(struct device *dev, struct device_attribute *attr, char *buf) {
  int status;
  int sample;
  struct touch_data *td;
  td = container_of(attr, struct touch_data, dev_attr_raw_sample);

  if (mutex_trylock(&td->mutex)) {
    return -EBUSY;
  }

  status = td->nxt_sense_device_data.get_sample(&sample);

  mutex_unlock(&td->mutex);

  if (status != 0) {
    printk(KERN_ERR DEVICE_NAME "%d: error trying to get a sample: %d\n", MINOR(td->nxt_sense_device_data.devt), status);
    sample = -1;
  }

  return scnprintf(buf, PAGE_SIZE, "%d\n", sample);
}

/***********************************************************************
 *
 * Utility functions for actually setting up the sysfs entries
 * and removing them again.
 *
 ***********************************************************************/
static int init_sysfs(struct touch_data *td) {
  int error = 0;

/* Manually doing what the macro DEVICE_ATTR is doing behind the scenes, but working around it for having a structure for each touch_data */
  td->dev_attr_threshold.attr.name = __stringify(threshold);
  td->dev_attr_threshold.attr.mode = (S_IRUGO | S_IWUSR);
  td->dev_attr_threshold.show = threshold_show;
  td->dev_attr_threshold.store = threshold_store;

  td->dev_attr_raw_sample.attr.name = __stringify(raw_sample);
  td->dev_attr_raw_sample.attr.mode = (S_IRUGO);
  td->dev_attr_raw_sample.show = raw_sample_show;
  td->dev_attr_raw_sample.store = NULL;

  error = device_create_file(td->nxt_sense_device_data.device, &td->dev_attr_threshold);
  if (error != 0) {
    printk(KERN_ALERT DEVICE_NAME "%d: device_create_file(threshold) error: %d\n", MINOR(td->nxt_sense_device_data.devt), error);
    return -1;
  }

  error = device_create_file(td->nxt_sense_device_data.device, &td->dev_attr_raw_sample);
  if (error != 0) {
    printk(KERN_ALERT DEVICE_NAME "%d: device_create_file(raw_sample) error: %d\n", MINOR(td->nxt_sense_device_data.devt), error);
    device_remove_file(td->nxt_sense_device_data.device, &td->dev_attr_threshold);
    return -1;
  }

  return 0;
}

static int destroy_sysfs(struct touch_data *td) {
  device_remove_file(td->nxt_sense_device_data.device, &td->dev_attr_threshold);
  device_remove_file(td->nxt_sense_device_data.device, &td->dev_attr_raw_sample);

  return 0;
}

/***********************************************************************
 *
 * Hooks for adding and removing devices for the touch sensor submodule,
 * called from nxt_sense_core.c
 *
 ***********************************************************************/
int add_touch_sensor(int port, dev_t devt) {
  int res;
  int error;
  printk(KERN_DEBUG DEVICE_NAME ": Adding touch sensor on port %d\n", port);

  mutex_init(&touch_data[port].mutex);
  mutex_lock(&touch_data[port].mutex); /* void, so sleeps until the lock is acquired? mutex_lock_interruptible returns an error indicating it was interrupted... */

  touch_data[port].nxt_sense_device_data.devt = devt;

  res = nxt_setup_sensor_chrdev(&touch_fops, &touch_data[port].nxt_sense_device_data, DEVICE_NAME);

  printk(KERN_DEBUG DEVICE_NAME ": return value for nxt_setup_sensor_chrdev: %d\n", res);

  touch_data[port].port = port;
  touch_data[port].threshold = DEFAULT_THRESHOLD;

  error = init_sysfs(&touch_data[port]);
  mutex_unlock(&touch_data[port].mutex);
  if (error != 0) {
    /* error .... handle this if you want */
    return -1;
  }

  return res;
}

static int uninitialise_touch_data(struct touch_data *td) {
  mutex_destroy(&td->mutex);
  td->port = 0;
  td->threshold = 0;

  return 0;
}

int remove_touch_sensor(int port) {
  int res;
  printk(KERN_DEBUG DEVICE_NAME ": Removing touch sensor on port %d\n", port);

  destroy_sysfs(&touch_data[port]);

  res = nxt_teardown_sensor_chrdev(&touch_data[port].nxt_sense_device_data);

  uninitialise_touch_data(&touch_data[port]);

  return res;
}

MODULE_LICENSE("GPL");
