#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>

#include "../level_shifter/level_shifter.h"
#include "../adc/adc.h"
#include "nxt_sense_core.h"
/* Include the add and remove functions of the submodules that are supported */
#include "touch.h"
#include "light.h"

#define DEVICE_NAME "nxt_sense"

#define NUMBER_OF_DEVICES 5

#define PORT_MIN 0
#define NUMBER_OF_PORTS 4
/* This requires the port_min to start from 0 unless the number_of_ports is incremented accordingly */
#define NXT_SENSE_MINOR 4

/* sensor types */
#define NONWORKING_PORT_CODE -1
#define NONE_CODE 0
#define TOUCH_CODE 1
#define LIGHT_CODE 2
#define MAX_SENSOR_CODE LIGHT_CODE

/* GPIO pins */
#define GPIO_SCL_1 73
#define GPIO_SCL_2 75
#define GPIO_SCL_3 72
#define GPIO_SCL_4 74

DEFINE_MUTEX(nxt_sense_core_mutex);

struct nxt_sense_dev {
  dev_t devt;
  struct cdev cdev;
  struct class *class;
  struct device *device;
  int port_cfg[NUMBER_OF_PORTS];
};

static struct nxt_sense_dev nxt_sense_dev;

/***********************************************************************
 *
 * Macros for generating the functions for sampling the corresponding
 * ADC channels
 *
 ***********************************************************************/
#define SAMPLE_FUNCTION(_port, _adc_channel)				\
  static int get_sample_##_port (int *data) {				\
    int status = 0;							\
    status = adc_sample_channel(_adc_channel, data);			\
									\
    if (status != 0) {							\
      printk(KERN_ERR DEVICE_NAME ": Some error happened while communicating with the ADC: %d\n", status); \
    }									\
									\
    return status;							\
  }

/* applying the macro above - sample_function(port, corresponding adc_channel) */
SAMPLE_FUNCTION(0, 0)
SAMPLE_FUNCTION(1, 1)
SAMPLE_FUNCTION(2, 2)
SAMPLE_FUNCTION(3, 3)

/***********************************************************************
 *
 * Macros for generating the functions for operating the SCL pins
 *
 ***********************************************************************/
#define SCL_FUNCTION(_port, _pin)					\
  static int scl_##_port (enum scl_bit_flags bit_flag) {		\
    int status = 0;							\
    static int current_value = 0;					\
									\
    switch (bit_flag) {							\
    case SCL_LOW:							\
      gpio_set_value(_pin, 0);						\
      current_value = 0;						\
      break;								\
    case SCL_HIGH:							\
      gpio_set_value(_pin, 1);						\
      current_value = 1;						\
      break;								\
    case SCL_TOGGLE:							\
      gpio_set_value(_pin, (current_value == 0 ? 1 : 0));		\
      break;								\
    default:								\
      printk(KERN_WARNING DEVICE_NAME ": The given bit flag is invalid: %d\n", bit_flag); \
    }									\
    									\
    return status;							\
  }

/* applying the macro above - scl_function(port, corresponding SCL pin) */
SCL_FUNCTION(0, GPIO_SCL_1);
SCL_FUNCTION(1, GPIO_SCL_2);
SCL_FUNCTION(2, GPIO_SCL_3);
SCL_FUNCTION(3, GPIO_SCL_4);

/***********************************************************************
 *
 * Functions for managing the loading and unloading of the submodules
 * Comments: Requires knowledge of the available submodules
 *
 ***********************************************************************/
static int load_nxt_sensor(int sensor_code, int port) {
  int status = 0;
  dev_t devt;

  /* If the port is not available then exit */
  if (nxt_sense_dev.port_cfg[port] != NONE_CODE) {
    return -3;
  }

  devt = MKDEV(MAJOR(nxt_sense_dev.devt), port); /* NOTE: Hardcoded port to match minor number here! */

  nxt_sense_dev.port_cfg[port] = sensor_code;

  switch (sensor_code) {
  case NONE_CODE:
    /* Do nothing */
    break;
  case TOUCH_CODE:
    status = add_touch_sensor(port, devt);
    break;
  case LIGHT_CODE:
    status = add_light_sensor(port, devt);
    break;
  case NONWORKING_PORT_CODE:
    printk(KERN_ERR DEVICE_NAME ": internal error: Trying to load a nxt_sensor with NONWORKING_PORT_CODE (%d)\n", sensor_code);
    status = -1;
    break;
  default:
    /* Error... */
    printk(KERN_ERR DEVICE_NAME ": Loading unknown sensor port code!: %d\n", sensor_code);
    status = -1;
  }

  if(status < 0){
    nxt_sense_dev.port_cfg[port] = NONWORKING_PORT_CODE;
  }

  return status;
}

static int unload_nxt_sensor(int port) {
  int status = 0;

  switch (nxt_sense_dev.port_cfg[port]) {
  case NONE_CODE:
    /* Do nothing */
    break;
  case TOUCH_CODE:
    status = remove_touch_sensor(port);
    break;
  case LIGHT_CODE:
    status = remove_light_sensor(port);
    break;
  case NONWORKING_PORT_CODE:
    printk(KERN_ERR DEVICE_NAME ": internal error: Trying to unload a nxt_sensor with NONWORKING_PORT_CODE (%d)\n", nxt_sense_dev.port_cfg[port]);
    return -4;
    break;
  default:
    /* Error... */
    printk(KERN_ERR DEVICE_NAME ": Unloading unknown sensor port code!: %d\n", nxt_sense_dev.port_cfg[port]);
    status = -1;
  }

  if(status < 0){
    nxt_sense_dev.port_cfg[port] = NONWORKING_PORT_CODE;
  } else {
    nxt_sense_dev.port_cfg[port] = NONE_CODE;
  }

  return status;
}

static int update_port_cfg(int cfg[]) {
  int res;
  int i;
  int error_occurred = 0;
  for (i = 0; i < NUMBER_OF_PORTS; ++i) {
    if (cfg[i] < NONE_CODE || cfg[i] > MAX_SENSOR_CODE) {
      return -1;
    }
  }

  for (i = 0; i < NUMBER_OF_PORTS; ++i) {
    if (nxt_sense_dev.port_cfg[i] != cfg[i] && nxt_sense_dev.port_cfg[i] != NONWORKING_PORT_CODE) {
      res = unload_nxt_sensor(i);
      if (res != 0) {
        error_occurred = -2;
	continue;
      }

      res = load_nxt_sensor(cfg[i], i);
      if (res != 0) {
	error_occurred = -3;
	continue;
      }

    }
  }

  return error_occurred;
}


/***********************************************************************
 *
 * Hooks for setting up the sensor char devices,
 * called from the submodules
 *
 ***********************************************************************/

/* Just a utility function, not meant to be used as a hook
 * Comments: nxt_sense implements that there is a correspondence between port number and minor number for now, but keeps the implementation details like this in nxt_sense_core
 */
static bool valid_devt(dev_t *devt) {
  bool res = false;
  if (MAJOR(*devt) == MAJOR(nxt_sense_dev.devt)) {
    if (MINOR(*devt) >= PORT_MIN && MINOR(*devt) < (PORT_MIN + NUMBER_OF_PORTS)) {
      res = true;
    }
  }

  return res;
}

int nxt_setup_sensor_chrdev(const struct file_operations *fops, struct nxt_sense_device_data *nxt_sense_device_data, const char *name) {
  int error;

  if (!valid_devt(&nxt_sense_device_data->devt)) {
    return -1;
  }

  cdev_init(&nxt_sense_device_data->cdev, fops);
  
  error = cdev_add(&nxt_sense_device_data->cdev, nxt_sense_device_data->devt, 1);
  if (error) {
    printk(KERN_ERR DEVICE_NAME ": could not add cdev for %s: %d\n", name, error);
    return -1;
  }

  /* Having the first NULL replaced with nxt_sense_dev.device : what does it exactly do? */
  nxt_sense_device_data->device = device_create(nxt_sense_dev.class, NULL, nxt_sense_device_data->devt, NULL, "%s%d", name, MINOR(nxt_sense_device_data->devt)); /* Hardcoded correspondence between minor number and port number */
  if (IS_ERR(nxt_sense_device_data->device)) {
    printk(KERN_ERR DEVICE_NAME ": device_create() failed for sensor %s%d: %ld", name, MINOR(nxt_sense_device_data->devt), PTR_ERR(nxt_sense_device_data->device));
    cdev_del(&nxt_sense_device_data->cdev);
    return -1;
  }

  switch (MINOR(nxt_sense_device_data->devt)) {
  case 0:
    nxt_sense_device_data->get_sample = get_sample_0;
    break;
  case 1:
    nxt_sense_device_data->get_sample = get_sample_1;
    break;
  case 2:
    nxt_sense_device_data->get_sample = get_sample_2;
    break;
  case 3:
    nxt_sense_device_data->get_sample = get_sample_3;
    break;
  default:
    printk(KERN_WARNING DEVICE_NAME ": The given minor number in devt is valid but the hardcoded values for setting up sampling functions is incorrect - FIX ME NOW!\n");
    return -1;
  }

  switch (MINOR(nxt_sense_device_data->devt)) {
  case 0:
    nxt_sense_device_data->scl = scl_0;
    break;
  case 1:
    nxt_sense_device_data->scl = scl_1;
    break;
  case 2:
    nxt_sense_device_data->scl = scl_2;
    break;
  case 3:
    nxt_sense_device_data->scl = scl_3;
    break;
  default:
    printk(KERN_WARNING DEVICE_NAME ": The given minor number in devt is valid but the hardcoded values for setting up scl functions is incorrect - FIX ME NOW!\n");
    return -1;
  }

  return 0;
}

int nxt_teardown_sensor_chrdev(struct nxt_sense_device_data *nxt_sense_device_data) {
  if (!valid_devt(&nxt_sense_device_data->devt)) {
    return -1;
  }

  device_destroy(nxt_sense_dev.class, nxt_sense_device_data->devt);
  cdev_del(&nxt_sense_device_data->cdev);

  nxt_sense_device_data->devt = MKDEV(0, 0);
  nxt_sense_device_data->device = NULL;
  nxt_sense_device_data->get_sample = NULL;
  nxt_sense_device_data->scl(SCL_LOW); /* reset the SCL pin */
  nxt_sense_device_data->scl = NULL;

  return 0;
}

/***********************************************************************
 *
 * Sysfs show and store functions for the configuration interface
 *
 ***********************************************************************/
static ssize_t nxt_sense_show(struct device *dev, struct device_attribute *attr, char *buf) {
  
  return scnprintf(buf, PAGE_SIZE, "%d %d %d %d\n", nxt_sense_dev.port_cfg[0], nxt_sense_dev.port_cfg[1], nxt_sense_dev.port_cfg[2], nxt_sense_dev.port_cfg[3]);
}

static ssize_t nxt_sense_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
  int p[NUMBER_OF_PORTS];
  int res;
  int status;

  res = sscanf(buf, "%d %d %d %d", &p[0], &p[1], &p[2], &p[3]);

  if (res != NUMBER_OF_PORTS) {
    printk(KERN_WARNING DEVICE_NAME ": sysfs input was not four integers!\n");
  } else {
    printk(KERN_DEBUG DEVICE_NAME ": sysfs input: %d %d %d %d\n", p[0], p[1], p[2], p[3]);

    mutex_lock(&nxt_sense_core_mutex);
    status = update_port_cfg(p);
    mutex_unlock(&nxt_sense_core_mutex);
    if (status != 0) {
      printk(KERN_ERR DEVICE_NAME ": error from update_port_cfg([%d] [%d] [%d] [%d]): %d\n", p[0], p[1], p[2], p[3], status);
    }
  }

  return count;
}

/* See linux/stat.h for more info: S_IRUGO gives read permission for everyone and S_IWUSR gives write permission for the user (in this case root is the owner) */
DEVICE_ATTR(config, (S_IRUGO | S_IWUSR), nxt_sense_show, nxt_sense_store);

/***********************************************************************
 *
 * Module initialisation and uninitialisation
 *
 ***********************************************************************/
static const struct file_operations nxt_sense_fops = {
  .owner =	THIS_MODULE,
};

/* Most of this function could be placed in a macro to only write the code once */
#define GPIO_INIT_MACRO(_port)						\
  if (gpio_request(GPIO_SCL_##_port, "SCL" #_port)) {			\
    printk(KERN_CRIT DEVICE_NAME ": gpio_request for pin %d failed\n", GPIO_SCL_##_port); \
    goto init_gpio_pins_fail_##_port;					\
  }									\
  									\
  if (gpio_direction_output(GPIO_SCL_##_port, 0)) {			\
    printk(KERN_CRIT DEVICE_NAME ": could not set direction output on pin %d\n", GPIO_SCL_##_port); \
    goto init_gpio_pins_fail_##_port;					\
  }

static int __init nxt_sense_level_shifter_init(void) {
  if (register_use_of_level_shifter(LS_U3_1)) {
    printk(KERN_CRIT DEVICE_NAME ": register_use_of_level_shifter failed for LS_U3_1\n");
    return -1;
  }

  return 0;
}


static int __init nxt_sense_init_gpio_pins(void) {
  GPIO_INIT_MACRO(1);
  GPIO_INIT_MACRO(2);
  GPIO_INIT_MACRO(3);
  GPIO_INIT_MACRO(4);

  return 0;

 init_gpio_pins_fail_4:
  gpio_free(GPIO_SCL_3);
 init_gpio_pins_fail_3:
  gpio_free(GPIO_SCL_2);
 init_gpio_pins_fail_2:
  gpio_free(GPIO_SCL_1);
 init_gpio_pins_fail_1:
  return -1;

}

static int __init nxt_sense_init_cdev(void)
{
  int error;

  nxt_sense_dev.devt = MKDEV(0, 0);

  error = alloc_chrdev_region(&nxt_sense_dev.devt, 0, NUMBER_OF_DEVICES, DEVICE_NAME);
  if (error < 0) {
    printk(KERN_CRIT DEVICE_NAME ": alloc_chrdev_region() failed: %d\n", 
	   error);
    return -1;
  }

  cdev_init(&nxt_sense_dev.cdev, &nxt_sense_fops);
  nxt_sense_dev.cdev.owner = THIS_MODULE;

  error = cdev_add(&nxt_sense_dev.cdev, MKDEV(MAJOR(nxt_sense_dev.devt), NXT_SENSE_MINOR), 1);
  if (error) {
    printk(KERN_CRIT DEVICE_NAME ": cdev_add() failed: %d\n", error);
    unregister_chrdev_region(nxt_sense_dev.devt, NUMBER_OF_DEVICES);
    return -1;
  }	

  return 0;
}

static int __init nxt_sense_init_class(void)
{
  nxt_sense_dev.class = class_create(THIS_MODULE, DEVICE_NAME);

  if (IS_ERR(nxt_sense_dev.class)) {
    printk(KERN_CRIT DEVICE_NAME ": class_create() failed: %ld\n", PTR_ERR(nxt_sense_dev.class));
    return -1;
  }

  nxt_sense_dev.device = device_create(nxt_sense_dev.class, NULL, MKDEV(MAJOR(nxt_sense_dev.devt), NXT_SENSE_MINOR), NULL, DEVICE_NAME);

  if (IS_ERR(nxt_sense_dev.device)) {
    printk(KERN_CRIT DEVICE_NAME ": device_create() failed: %ld\n", PTR_ERR(nxt_sense_dev.device));
    class_destroy(nxt_sense_dev.class);
    return -1;
  }

  if (device_create_file(nxt_sense_dev.device, &dev_attr_config)) {
    printk(KERN_CRIT DEVICE_NAME ": device_create_file() failed\n");
    device_destroy(nxt_sense_dev.class, MKDEV(MAJOR(nxt_sense_dev.devt), NXT_SENSE_MINOR));
    class_destroy(nxt_sense_dev.class);
    return -1;
  }

  return 0;
}

static int __init nxt_sense_init(void) {
  printk(KERN_DEBUG DEVICE_NAME ": Initialising nxt_sense...\n");
  memset(&nxt_sense_dev, 0, sizeof(nxt_sense_dev));

  if (nxt_sense_level_shifter_init() < 0)
    goto fail_1;

  if (nxt_sense_init_gpio_pins() < 0)
    goto fail_2;

  if (nxt_sense_init_cdev() < 0) 
    goto fail_3;

  if (nxt_sense_init_class() < 0)  
    goto fail_4;

  return 0;

 fail_4:
  cdev_del(&nxt_sense_dev.cdev);
  unregister_chrdev_region(nxt_sense_dev.devt, NUMBER_OF_DEVICES);

 fail_3:
  gpio_free(GPIO_SCL_4);
  gpio_free(GPIO_SCL_3);
  gpio_free(GPIO_SCL_2);
  gpio_free(GPIO_SCL_1);

 fail_2:
  unregister_use_of_level_shifter(LS_U3_1);

 fail_1:
  return -1;
}
module_init(nxt_sense_init);

static void __exit nxt_sense_exit(void) {
  int p[NUMBER_OF_PORTS] = {0, 0, 0, 0};
  update_port_cfg(p);

  device_remove_file(nxt_sense_dev.device, &dev_attr_config);
  device_destroy(nxt_sense_dev.class, MKDEV(MAJOR(nxt_sense_dev.devt), NXT_SENSE_MINOR));
  class_destroy(nxt_sense_dev.class);

  cdev_del(&nxt_sense_dev.cdev);
  unregister_chrdev_region(nxt_sense_dev.devt, NUMBER_OF_DEVICES);

  gpio_free(GPIO_SCL_1);
  gpio_free(GPIO_SCL_2);
  gpio_free(GPIO_SCL_3);
  gpio_free(GPIO_SCL_4);
  unregister_use_of_level_shifter(LS_U3_1);

  printk(KERN_DEBUG DEVICE_NAME ": Exiting nxt_sense...\n");
}
module_exit(nxt_sense_exit);
MODULE_AUTHOR("Group1");
MODULE_LICENSE("GPL");
