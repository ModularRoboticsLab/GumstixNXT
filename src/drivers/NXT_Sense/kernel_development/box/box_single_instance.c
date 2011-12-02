#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/types.h> // dev_t
#include <linux/fs.h> // file_operations (char driver methods)
#include <linux/kernel.h>
#include <linux/module.h> // THIS_MODULE
#include <linux/device.h> // class_create, device_create (and probably destroy)
#include <asm/uaccess.h> // copy_{from,to}_user
#include <linux/string.h>

#define DEVICE_NAME "box"

struct box_dev {
  dev_t devt; // Device major and minor numbers
  struct cdev cdev; // kernel char device abstraction
  struct class *class; 
  char *data;
  size_t size;
};

struct io_status {
  bool bytes_outputted;
};

static struct box_dev box_dev;
static struct io_status io_status = {
  .bytes_outputted = false,
};

/* File operations */
ssize_t box_read(struct file *file, char *buf, size_t count, loff_t *ppos) {
  ssize_t bytes_read = strnlen(box_dev.data, box_dev.size);

  if (io_status.bytes_outputted) {
    io_status.bytes_outputted = false;
    return 0; // Indicate EOF
  } else {
    io_status.bytes_outputted = true;
    if (copy_to_user(buf, box_dev.data, bytes_read) != 0) {
      return -EIO;
    }

    return bytes_read;
  }
}

ssize_t box_write(struct file *file, const char *buf, size_t count, loff_t *ppos) {
  size_t strsize = strnlen(buf, count);
  if(strsize > box_dev.size){
    box_dev.size = count+1;
    kfree(box_dev.data);
    box_dev.data = kmalloc(box_dev.size, GFP_KERNEL);
  }
  if (copy_from_user(box_dev.data, buf, box_dev.size) != 0) {
    return -EIO;
  }

  return strsize;
}

static const struct file_operations box_fops = {
  .owner = THIS_MODULE,
  .read = box_read,
  .write = box_write,
};

int __init box_init(void) {
  /* Request dynamic allocation of a device major number */
  if (alloc_chrdev_region(&box_dev.devt, 0, 1, DEVICE_NAME) < 0) {
    printk(KERN_DEBUG "Can't register device\n");
    return -1;
  }

  /* Connect the file operations with the cdev */
  cdev_init(&box_dev.cdev, &box_fops);
  box_dev.cdev.owner = THIS_MODULE;

  /* Connect the major/minor number to the cdev */
  if (cdev_add(&box_dev.cdev, box_dev.devt, 1)) {
    printk("Bad cdev add\n");
    return 1;
  }

  /* Populate sysfs entries */
  box_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
  if (IS_ERR(box_dev.class)) {
    printk("Bad class create\n");
  }

  if (!device_create(box_dev.class, NULL, box_dev.devt, NULL, DEVICE_NAME)) {
    printk(KERN_DEBUG "Got an error for device_create()\n");
    return -1;
  }

  box_dev.data = kmalloc(56, GFP_KERNEL);
  box_dev.size = 56;

  strncpy(box_dev.data, "Initial data...!\0", 25);

  printk("Box driver initialized.\n");

  return 0;
}

void __exit box_exit(void) {
  /* Release the major number */
  kfree(box_dev.data);
  unregister_chrdev_region(box_dev.devt, 1);

  device_destroy(box_dev.class, box_dev.devt);

  class_destroy(box_dev.class);
}

module_init(box_init);
module_exit(box_exit);

MODULE_LICENSE("GPL");
