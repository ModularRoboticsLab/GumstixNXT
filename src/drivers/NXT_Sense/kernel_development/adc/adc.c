/* The SPI part is heavily inspired by Scott Ellis, see http://jumpnowtek.com/
 */

/* TODO:
 * - In adc_sample_channel(), make use of a macro/function to handle endianess e.g. use {le|be}16_to_cpu() for more portability (Point stolen from the adc128s022 driver created by Peter Madsen)
 */
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

#define USER_BUFF_SIZE 128

/***********************************************************************
 *
 * SPI bus parameters
 *
 ***********************************************************************/
#define SPI_BUS 1
#define SPI_BUS_CS0 0
#define SPI_BUS_SPEED 3000000
#define SPI_BITS_PER_WORD 8
/* SPI definements for our code */
#define SPI_DEVICE_IS_NULL -1
#define SPI_MASTER_IS_NULL -2
#define SPI_BUFF_SIZE 4

/***********************************************************************
 *
 * ADC parameters
 *
 ***********************************************************************/
/* ADC hardware addressing */
#define ADC_CHANNEL0 0x00
#define ADC_CHANNEL1 0x08
#define ADC_CHANNEL2 0x10
#define ADC_CHANNEL3 0x18
/* Voltage divider / voltage measurement */
#define ADC_CHANNEL4 0x20
/* Currently no specified use on channel 5-7 */
#define ADC_CHANNEL5 0x28
#define ADC_CHANNEL6 0x30
#define ADC_CHANNEL7 0x38
/* It supports 8 channels, but only 5 channels are connected to something useful for now */
#define NO_ADC_CHANNELS 5

/* Level shifter gpio */
#define GPIO_1OE 10
#define DEVICE_NAME "adc"

DEFINE_MUTEX(adc_mutex);

struct adc_dev {
  dev_t devt;
  struct cdev cdev;
  struct class *class;
  struct spi_device *spi_device;
  char user_buff[USER_BUFF_SIZE];
  struct device *device[NO_ADC_CHANNELS];
};

struct spi_control {
  struct spi_message msg;
  struct spi_transfer transfer;
  u8 *tx_buff; 
  u8 *rx_buff;
};

struct adc_info {
  int channel;
};

static struct spi_control spi_ctl;
static struct adc_dev adc_dev;
static struct adc_info adc_info[NO_ADC_CHANNELS];

/***********************************************************************
 *
 * SPI utility functions for packaging messages and communicating with
 * the SPI subsystem
 *
 ***********************************************************************/
static void spi_prepare_message(int channel) {
  u8 adc_channel;
  spi_message_init(&spi_ctl.msg);

  switch(channel) {
  case 0:
    adc_channel = ADC_CHANNEL0;
    break;
  case 1:
    adc_channel = ADC_CHANNEL1;
    break;
  case 2:
    adc_channel = ADC_CHANNEL2;
    break;
  case 3:
    adc_channel = ADC_CHANNEL3;
    break;
  case 4:
    adc_channel = ADC_CHANNEL4;
    break;
  case 5:
    adc_channel = ADC_CHANNEL5;
    break;
  case 6:
    adc_channel = ADC_CHANNEL6;
    break;
  case 7:
    adc_channel = ADC_CHANNEL7;
    break;
  default:
    adc_channel = ADC_CHANNEL0;
    /* signal some sort of error? */
  }

  spi_ctl.tx_buff[0] = adc_channel;
  spi_ctl.tx_buff[1] = 0x00;

  /* Not doing message merging for now */
  spi_ctl.tx_buff[2] = 0x00;
  spi_ctl.tx_buff[3] = 0x00;
	
  memset(spi_ctl.rx_buff, 0, SPI_BUFF_SIZE);
  
  spi_ctl.transfer.tx_buf = spi_ctl.tx_buff;
  spi_ctl.transfer.rx_buf = spi_ctl.rx_buff;
  spi_ctl.transfer.len = SPI_BUFF_SIZE;
  
  spi_message_add_tail(&spi_ctl.transfer, &spi_ctl.msg);
}

/* Returns zero on success, else a negative error code */
static int spi_do_message(int channel) {
  int status;

  spi_prepare_message(channel);

  /* sync'ed SPI communication for now */
  status = spi_sync(adc_dev.spi_device, &spi_ctl.msg);

  return status;
}

/***********************************************************************
 *
 * Hook for obtaining an ADC sample from other modules
 *
 ***********************************************************************/
/* Every sampling bounces through this function - taking care of concurrency here to avoid having multiple clients playing around in the same data... */
/* Returns zero on success, else a negative error code */
static int adc_sample_channel(int channel, int *data) {
  int status;
  int sample_value;

  mutex_lock(&adc_mutex);

  if (!adc_dev.spi_device) {
    status = SPI_DEVICE_IS_NULL;
  } else if (!adc_dev.spi_device->master) {
    status = SPI_MASTER_IS_NULL;
  } else {
    status = spi_do_message(channel);

    sample_value = spi_ctl.rx_buff[2];
    sample_value = sample_value << 8;
    sample_value |= spi_ctl.rx_buff[3];

    *data = sample_value;
  }

  mutex_unlock(&adc_mutex);

  return status;
}
EXPORT_SYMBOL(adc_sample_channel);

/***********************************************************************
 *
 * File operations for the /dev/adc# files
 *
 ***********************************************************************/
static ssize_t adc_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
  size_t len;
  ssize_t status = 0;
  struct adc_info *adc_info = filp->private_data;
  int sample_value;
  int adc_sample_status;

  if (!buff) 
    return -EFAULT;

  if (*offp > 0){
    return 0;
  }

  adc_sample_status = adc_sample_channel(adc_info->channel, &sample_value);

  if (adc_sample_status == SPI_DEVICE_IS_NULL)
    strcpy(adc_dev.user_buff, "spi_device is NULL\n");
  else if (adc_sample_status == SPI_MASTER_IS_NULL)
    strcpy(adc_dev.user_buff, "spi_device->master is NULL\n");
  else {
    sprintf(adc_dev.user_buff, "%d\n", sample_value); /* could also be outputted in hexadecimal %X */
  }

  len = strlen(adc_dev.user_buff);
 
  if (len < count) 
    count = len;

  if (copy_to_user(buff, adc_dev.user_buff, count))  {
    printk(KERN_ERR DEVICE_NAME ": copy_to_user() failed\n");
    status = -EFAULT;
  } else {
    *offp += count;
    status = count;
  }

  return status;	
}

static int adc_open(struct inode *inode, struct file *filp) {	
  int chan = MINOR(inode->i_rdev);

  filp->private_data = &adc_info[chan];

  return 0;
}

static const struct file_operations adc_fops = {
  .owner =	THIS_MODULE,
  .read = 	adc_read,
  .open =      	adc_open,	
};

/***********************************************************************
 *
 * SPI initialisation and setup functions
 *
 ***********************************************************************/
static int adc_probe(struct spi_device *spi_device) {
  adc_dev.spi_device = spi_device;

  return 0;
}

static int adc_remove(struct spi_device *spi_device) {
  adc_dev.spi_device = NULL;

  return 0;
}

static struct spi_driver spi_driver = {
  .driver = {
    .name =	DEVICE_NAME,
    .owner = THIS_MODULE,
  },
  .probe = adc_probe,
  .remove = __devexit_p(adc_remove),	
};

static int __init add_adc_device_to_bus(void) {
  struct spi_master *spi_master;
  struct spi_device *spi_device;
  struct device *pdev;
  char buff[64];
  int status = 0;

  /* This call returns a refcounted pointer to the relevant spi_master - the caller must release this pointer(device_put()) */	
  spi_master = spi_busnum_to_master(SPI_BUS);
  if (!spi_master) {
    printk(KERN_CRIT DEVICE_NAME ": spi_busnum_to_master(%d) returned NULL\nMissing modprobe omap2_mcspi?\n", SPI_BUS);
    return -1;
  }

  spi_device = spi_alloc_device(spi_master);
  if (!spi_device) {
    printk(KERN_CRIT DEVICE_NAME ": spi_alloc_device() failed\n");
    return -1;
  }

  spi_device->chip_select = SPI_BUS_CS0;

  /* Check whether this SPI bus.cs is already claimed */
  /* snprintf the c-way of formatting a string */
  snprintf(buff, sizeof(buff), "%s.%u", dev_name(&spi_device->master->dev), spi_device->chip_select);

  pdev = bus_find_device_by_name(spi_device->dev.bus, NULL, buff);
  if (pdev) {
    /* We are not going to use this spi_device, so free it. Since spi_device is not added then decrement the refcount */ 
    spi_dev_put(spi_device);

    /* 
     * There is already a device configured for this bus.cs  
     * It is okay if it ours, otherwise complain and fail.
     */
    if (pdev->driver && pdev->driver->name && strcmp(DEVICE_NAME, pdev->driver->name)) {
      printk(KERN_CRIT DEVICE_NAME ": Driver [%s] already registered for %s\n", pdev->driver->name, buff);
      status = -1;
    } 
  } else {
    spi_device->max_speed_hz = SPI_BUS_SPEED;
    spi_device->mode = SPI_MODE_0;
    spi_device->bits_per_word = SPI_BITS_PER_WORD;
    spi_device->irq = -1;
    spi_device->controller_state = NULL;
    spi_device->controller_data = NULL;
    strlcpy(spi_device->modalias, DEVICE_NAME, SPI_NAME_SIZE);

    status = spi_add_device(spi_device);		
    if (status < 0) {
      /* If spi_device is not added then decrement the refcount */	
      spi_dev_put(spi_device);
      printk(KERN_CRIT DEVICE_NAME ": spi_add_device() failed: %d\n", status);		
    }				
  }
  /* See comment for spi_busnum_to_master */
  put_device(&spi_master->dev);

  return status;
}

static int __init init_spi(void) {
  int error;

  spi_ctl.tx_buff = kzalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
  if (!spi_ctl.tx_buff) {
    error = -ENOMEM;
    goto init_error;
  }

  spi_ctl.rx_buff = kzalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
  if (!spi_ctl.rx_buff) {
    error = -ENOMEM;
    goto init_error;
  }

  error = spi_register_driver(&spi_driver);
  if (error < 0) {
    printk(KERN_CRIT DEVICE_NAME ": spi_register_driver() failed %d\n", error);
    return error;
  }

  error = add_adc_device_to_bus();
  if (error < 0) {
    printk(KERN_CRIT DEVICE_NAME ": add_adc_to_bus() failed\n");
    spi_unregister_driver(&spi_driver);
    return error;
  }

  return 0;

 init_error:

  if (spi_ctl.tx_buff) {
    kfree(spi_ctl.tx_buff);
    spi_ctl.tx_buff = 0;
  }

  if (spi_ctl.rx_buff) {
    kfree(spi_ctl.rx_buff);
    spi_ctl.rx_buff = 0;
  }

  return error;
}

/***********************************************************************
 *
 * Module initialisation and teardown functions
 *
 ***********************************************************************/
static int __init init_cdev(void) {
  int error;

  adc_dev.devt = MKDEV(0, 0);

  error = alloc_chrdev_region(&adc_dev.devt, 0, NO_ADC_CHANNELS, DEVICE_NAME);
  if (error < 0) {
    printk(KERN_CRIT DEVICE_NAME ": alloc_chrdev_region() failed: %d \n", 
	   error);
    return -1;
  }

  cdev_init(&adc_dev.cdev, &adc_fops);
  adc_dev.cdev.owner = THIS_MODULE;

  error = cdev_add(&adc_dev.cdev, adc_dev.devt, NO_ADC_CHANNELS);
  if (error) {
    printk(KERN_CRIT DEVICE_NAME ": cdev_add() failed: %d\n", error);
    unregister_chrdev_region(adc_dev.devt, 1);
    return -1;
  }	

  return 0;
}

static int __init init_class(void) {
  int i;
  int j;

  adc_dev.class = class_create(THIS_MODULE, DEVICE_NAME);

  if (IS_ERR(adc_dev.class)) {
    printk(KERN_CRIT DEVICE_NAME ": class_create() failed: %ld\n", PTR_ERR(adc_dev.class));
    return -1;
  }

  for (i = 0; i < NO_ADC_CHANNELS; ++i) {
    adc_dev.device[i] = device_create(adc_dev.class, NULL, MKDEV(MAJOR(adc_dev.devt), i), NULL, "gumnxtadc%d", i);

    if (IS_ERR(adc_dev.device)) {
      printk(KERN_CRIT DEVICE_NAME ": device_create(..., %s) failed: %ld\n", DEVICE_NAME, PTR_ERR(adc_dev.device));
      goto failed_device_creation;
    }
  }

  return 0;

 failed_device_creation:
  for (j = i-1; j >= 0; --j) {
    device_destroy(adc_dev.class, MKDEV(MAJOR(adc_dev.devt), j));
  }

  class_destroy(adc_dev.class);
  return -1;
}

static int __init init_level_shifters(void) {
  if (register_use_of_level_shifter(LS_U3_1)) {
    printk(KERN_CRIT DEVICE_NAME ": register_use_of_level_shifter failed for LS_U3_1\n");
    goto init_pins_fail_1;
  }

  if (register_use_of_level_shifter(LS_U3_2)) {
    printk(KERN_CRIT DEVICE_NAME ": register_use_of_level_shifter failed for LS_U3_2\n");
    goto init_pins_fail_2;
  }

  return 0;

 init_pins_fail_2:
  unregister_use_of_level_shifter(LS_U3_1);

 init_pins_fail_1:
  return -1;
}

static int __init init(void) {
  int j;

  memset(&adc_dev, 0, sizeof(adc_dev));
  memset(&spi_ctl, 0, sizeof(spi_ctl));

  /* Initialise the adc_info array - minor workaround to keep track of which /dev/file that is opened by the user */
  for (j = 0; j < NO_ADC_CHANNELS; ++j) {
    adc_info[j].channel = j;
  }

  if (init_cdev() < 0) 
    goto fail_1;

  if (init_class() < 0)  
    goto fail_2;

  if (init_spi() < 0) 
    goto fail_3;

  if (init_level_shifters() < 0)
    goto fail_4;


  return 0;

 fail_4:
  /* init_level_shifters() cleans up after itself, if it should fail */

 fail_3:
  for (j = NO_ADC_CHANNELS; j >= 0; --j) {
    device_destroy(adc_dev.class, MKDEV(MAJOR(adc_dev.devt), j));
  }
  class_destroy(adc_dev.class);

 fail_2:
  cdev_del(&adc_dev.cdev);
  unregister_chrdev_region(adc_dev.devt, 1);

 fail_1:
  return -1;
}
module_init(init);

static void __exit adc_exit(void) {
  int j;

  spi_unregister_driver(&spi_driver);

  for (j = NO_ADC_CHANNELS; j >= 0; --j) {
    device_destroy(adc_dev.class, MKDEV(MAJOR(adc_dev.devt), j));
  }
  class_destroy(adc_dev.class);

  cdev_del(&adc_dev.cdev);
  unregister_chrdev_region(adc_dev.devt, 1);

  if (spi_ctl.tx_buff)
    kfree(spi_ctl.tx_buff);

  if (spi_ctl.rx_buff)
    kfree(spi_ctl.rx_buff);

  /* Release spi level shifter pins */
  unregister_use_of_level_shifter(LS_U3_1);
  unregister_use_of_level_shifter(LS_U3_2);
}
module_exit(adc_exit);
MODULE_AUTHOR("Group1");
MODULE_LICENSE("GPL");
