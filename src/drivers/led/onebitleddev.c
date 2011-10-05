/*

A GumstixNXT LED driver for a single LED using GPIO, loosely based on irqlat by Scott
Ellis

Sample useage (shell commands):
insmod leddev.ko
echo set > /dev/leddev
echo inv > /dev/leddev
echo clr > /dev/leddev

*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <mach/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>


/* GPIOs for controlling the level shifter behavior */
#define GPIO_OE 80
#define GPIO_DIR 67

/* GPIOs for controlling the individual bits */
#define GPIO_BIT0 68

/* Signals to send to GPIOs to turn LEDs on and off */
#define BIT_ON 0
#define BIT_OFF 1

/* leddev device structure */
struct leddev_dev {
  /* Standard fields */
  dev_t devt; // kernel data structure for device numbers (major,minor)
  struct cdev cdev; // kernel data structure for character device
  struct class *class; // kernel data structure for device driver class /sys/class/leddev [LDD Chapter 14]
  /* Driver-specific fields */
  int value; // the integer value currently shown, 1 if on, 0 if off
};

/* device structure instance */
static struct leddev_dev leddev_dev;

/* Reset hardware settings and driver state */
static void leddev_reset(void)
{
  // Reset level shifter control
  gpio_set_value(GPIO_OE, 1);
  gpio_set_value(GPIO_DIR, 0);
  // Reset the bit to off state
  gpio_set_value(GPIO_BIT0, BIT_OFF);
  // Reset driver state
  leddev_dev.value = 0;
}

/* Respond to a command that has been written to the device special file */
static ssize_t leddev_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
  char cmd[3]; // Buffer for command written by user
  ssize_t status = 0; // Return status, updated depending on input

  /* Copy the data the user wrote from userspace (how much is read depends on return value) */
  if (copy_from_user(cmd, buff, 3)) {
    printk(KERN_ALERT "Error copy_from_user\n");
    status = -EFAULT;
    goto leddev_write_done;
  }
    
  /*
    Process the command stored in the data
    'set' means set the bit
    'clr' means clear the bit
    'inv' means invert
  */
  if (cmd[0] == 's' && cmd[1] == 'e' && cmd[2] == 't') { // Set command?
    gpio_set_value(GPIO_BIT0, BIT_ON); // Turn the hardware bit on 
    leddev_dev.value = 1; // Store the software bit
    status = 3; // Read 3 bytes, update return status correspondingly
  }

  else if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'r') { // Clear command?
    gpio_set_value(GPIO_BIT0, BIT_OFF); // Turn the selected hardware bit off
    leddev_dev.value = 0; // Turn the software bit off
    status = 3; // Read 3 bytes, update return status correspondingly
  }

  else if (cmd[0] == 'i' && cmd[1] == 'n' && cmd[2] == 'v') { // Invert command?
    leddev_dev.value = !leddev_dev.value; // Invert bit pattern
    gpio_set_value(GPIO_BIT0, leddev_dev.value ? BIT_ON : BIT_OFF);
    status = 3; // Read 3 bytes, update return status correspondingly
  } 

  else { // Unrecognized command
    status = 1; // Read one byte, will be called again if there is more input
    if(cmd[0]!=10 && cmd[0]!=' ') // Ignore newline and space
      printk(KERN_ALERT "LED illegal command char %d\n", cmd[0]);
  }

 leddev_write_done:

  return status; // Negative=error, positive=success and indicates #bytes read
}

/* The file operations structure, operations not listed here are illegal */
static const struct file_operations leddev_fops = {
  .owner = THIS_MODULE,
  .write = leddev_write,
};

/* Initialize the character device structure */
static int __init leddev_init_cdev(void)
{
  int error;

  /* Initialize devt structure, major/minor numbers assigned afterwards */
  leddev_dev.devt = MKDEV(0, 0);

  /* Assign major and minor device numbers to devt structure */
  error = alloc_chrdev_region(&leddev_dev.devt, 0, 1, "leddev");
  if (error < 0) {
    printk(KERN_ALERT "alloc_chrdev_region() failed: error = %d \n", error);
    return -1;
  }

  /* Initialize character device using the specific file operations structure */
  cdev_init(&leddev_dev.cdev, &leddev_fops);
  leddev_dev.cdev.owner = THIS_MODULE;

  /* Request kernel to make one device available */
  error = cdev_add(&leddev_dev.cdev, leddev_dev.devt, 1);
  if (error) {
    printk(KERN_ALERT "cdev_add() failed: error = %d\n", error);
    unregister_chrdev_region(leddev_dev.devt, 1);
    return -1;
  }

  return 0;
}

/* Create a class for the device driver /sys/class/leddev [LDD Ch 14] */
static int __init leddev_init_class(void)
{
  /* Create a class named leddev */
  leddev_dev.class = class_create(THIS_MODULE, "leddev");

  if (!leddev_dev.class) {
    printk(KERN_ALERT "class_create(leddev) failed\n");
    return -1;
  }

  /* Create class representation in the file system */
  if (!device_create(leddev_dev.class, NULL, leddev_dev.devt, NULL, "leddev")) {
    class_destroy(leddev_dev.class);
    return -1;
  }

  return 0;
}

/* Reserve and initialize the GPIO pins needed for the driver */
static int __init leddev_init_pins(void)
{
  /* Request and configure GPIOs for the level shifter */
  if (gpio_request(GPIO_OE, "OutputEnable")) {
    printk(KERN_ALERT "gpio_request failed\n");
    goto init_pins_fail_1;
  }

  if (gpio_direction_output(GPIO_OE, 0)) {
    printk(KERN_ALERT "gpio_direction_output GPIO_OE failed\n");
    goto init_pins_fail_2;
  }

  if (gpio_request(GPIO_DIR, "Direction")) {
    printk(KERN_ALERT "gpio_request(2) failed\n");
    goto init_pins_fail_2;
  }

  if (gpio_direction_output(GPIO_DIR, 0)) {
    printk(KERN_ALERT "gpio_direction_output GPIO_DIR failed\n");
    goto init_pins_fail_3;
  }

  if (gpio_request(GPIO_BIT0, "Bit0")) {
      printk(KERN_ALERT "gpio_request(0) failed\n");
      goto init_pins_fail_4;
  }

  if (gpio_direction_output(GPIO_BIT0, 0)) {
      printk(KERN_ALERT "gpio_direction_output GPIO_BIT0 failed\n");
      goto init_pins_fail_4;
  }

  return 0;

  /* Error handling code: free in reverse direction */

 init_pins_fail_4:
  gpio_free(GPIO_BIT0);
  
 init_pins_fail_3: 
  gpio_free(GPIO_DIR);

 init_pins_fail_2: 
  gpio_free(GPIO_OE);

 init_pins_fail_1:

  return -1;
}

/* Kernel module initialization function */
static int __init leddev_init(void)
{
  /* Zero memory for device struct */
  memset(&leddev_dev, 0, sizeof(leddev_dev));

  /* Run initialization functions in-turn */
  if (leddev_init_cdev())
    goto init_fail_1;

  if (leddev_init_class())
    goto init_fail_2;

  if (leddev_init_pins() < 0)
    goto init_fail_3;

  /* Reset driver state */
  leddev_reset();

  return 0;

  /* Failure handling: free resources in reverse order (starting at the point we got to) */

 init_fail_3:
  device_destroy(leddev_dev.class, leddev_dev.devt);
  class_destroy(leddev_dev.class);

 init_fail_2:
  cdev_del(&leddev_dev.cdev);
  unregister_chrdev_region(leddev_dev.devt, 1);

 init_fail_1:

  return -1;
}
module_init(leddev_init);

/* Kernel module release function */
static void __exit leddev_exit(void)
{
  /* Free all GPIOs */
  gpio_free(GPIO_OE);
  gpio_free(GPIO_DIR);
  gpio_free(GPIO_BIT0);

  /* Free class device */
  device_destroy(leddev_dev.class, leddev_dev.devt);
  class_destroy(leddev_dev.class);

  /* Free device itself */
  cdev_del(&leddev_dev.cdev);
  unregister_chrdev_region(leddev_dev.devt, 1);
}
module_exit(leddev_exit);

/* Module meta-information */
MODULE_AUTHOR("Ulrik Pagh Schultz");
MODULE_DESCRIPTION("A module for controlling a single GumstixNXT LED");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.1");
