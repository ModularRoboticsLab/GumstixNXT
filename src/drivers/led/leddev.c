/*

A GumstixNXT LED driver using GPIO, loosely based on irqlat by Scott
Ellis

Sample useage (shell commands):
insmod leddev.ko
echo set0set5 > /dev/leddev
echo inv > /dev/leddev
echo clr2clr3clr7inv > /dev/leddev

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
#define GPIO_BIT1 76
#define GPIO_BIT2 66
#define GPIO_BIT3 77
#define GPIO_BIT4 79
#define GPIO_BIT5 89
#define GPIO_BIT6 88
#define GPIO_BIT7 87

/* Number of GPIOs (this is a generic driver that manages array of GPIOs) */
#define GPIO_N_BITS 8

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
  int value; // the integer value currently shown (interpreting the LEDs as bits in a number)
};

/* device structure instance */
static struct leddev_dev leddev_dev;

/* All GPIOs managed by the driver, must be GPIO_N_BITS long */
static int gpio_bits[] = {
  GPIO_BIT0, GPIO_BIT1, GPIO_BIT2,
  GPIO_BIT3, GPIO_BIT4, GPIO_BIT5,
  GPIO_BIT6, GPIO_BIT7 };

/* Reset hardware settings and driver state */
static void leddev_reset(void)
{
  int index;
  // Reset level shifter control
  gpio_set_value(GPIO_OE, 1);
  gpio_set_value(GPIO_DIR, 0);
  // Reset each bit to off state
  for(index=0; index<GPIO_N_BITS; index++)
    gpio_set_value(gpio_bits[index], BIT_OFF);
  // Reset driver state
  leddev_dev.value = 0;
}

/* Respond to a command that has been written to the device special file */
static ssize_t leddev_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
  char cmd[4]; // Buffer for command written by user
  ssize_t status = 0; // Return status, updated depending on input
  int bit; // Bit that the operation has an effect on

  /* Copy the data the user wrote from userspace (how much is read depends on return value) */
  if (copy_from_user(cmd, buff, 4)) {
    printk(KERN_ALERT "Error copy_from_user\n");
    status = -EFAULT;
    goto leddev_write_done;
  }
    
  /*
    Process the command stored in the data
    'setN' means set bit #N
    'clrN' means clear bit #N
    'inv' means invert pattern
  */
  bit = cmd[3] - '0'; // Assuming a legal command, this is the bit to manipulate

  if (cmd[0] == 's' && cmd[1] == 'e' && cmd[2] == 't') { // Set command?
    if(bit<0 || bit>=GPIO_N_BITS) { // Check that pin argument is legal
      printk(KERN_ALERT "LED illegal numeric argument to set\n");
      return -1;
    }
    gpio_set_value(gpio_bits[bit], BIT_ON); // Turn the selected hardware bit on 
    leddev_dev.value |= 1<<bit; // Store the selected software bit
    status = 4; // Read 4 bytes, update return status correspondingly
  }

  else if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'r') { // Clear command?
    if(bit<0 || bit>=GPIO_N_BITS) { // Check that pin argument is legal
      printk(KERN_ALERT "LED illegal numeric argument to clr\n");
      return -1;
    }
    gpio_set_value(gpio_bits[bit], BIT_OFF); // Turn the selected hardware bit off
    leddev_dev.value ^= 1<<bit; // Turn the selected software bit off
    status = 4; // Read 4 bytes, update return status correspondingly
  }

  else if (cmd[0] == 'i' && cmd[1] == 'n' && cmd[2] == 'v') { // Invert command?
    int index;
    leddev_dev.value = ~leddev_dev.value; // Invert bit pattern
    for(index=0; index<GPIO_N_BITS; index++) // Set each bit
      gpio_set_value(gpio_bits[index], leddev_dev.value&(1<<index) ? BIT_ON : BIT_OFF);
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

  if (IS_ERR(leddev_dev.class)) {
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
  int gpio_index;
  int direction_output_failure = 0;

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

  /* Request and configure GPIOs for the individual bits */
  for(gpio_index=0; gpio_index<GPIO_N_BITS; gpio_index++) {
    
    if (gpio_request(gpio_bits[gpio_index], "BitN")) {
      printk(KERN_ALERT "gpio_request(bitN) failed\n");
      goto init_pins_fail_4;
    }

    if (gpio_direction_output(gpio_bits[gpio_index], 0)) {
      printk(KERN_ALERT "gpio_direction_output GPIO_BITn failed\n");
      direction_output_failure = 1;
      goto init_pins_fail_4;
    }

  }

  return 0;

  /* Error handling code: free in reverse direction */

 init_pins_fail_4: // Free those bit GPIOs that were allocated
  if(!direction_output_failure) gpio_index--; // Failed trying to allocate, don't include
  for(; gpio_index>=0; gpio_index--)
    gpio_free(gpio_bits[gpio_index]);
  
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
  int index;
  /* Free all GPIOs */
  gpio_free(GPIO_OE);
  gpio_free(GPIO_DIR);
  for(index=0; index<GPIO_N_BITS; index++)
    gpio_free(gpio_bits[index]);

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
MODULE_DESCRIPTION("A module for controlling GumstixNXT LEDs");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.3");
