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

#define USER_BUFF_SIZE	128

#define SPI_BUS 1
#define SPI_BUS_CS0 0
#define SPI_BUS_SPEED 3000000
#define SPI_BITS_PER_WORD 8
#define DEVICE_NAME "nxtts"
//Sensor1 ADC IN0
#define ADC_ADDRESS0 0x00
#define ADC_ADDRESS1 0x08
#define ADC_ADDRESS2 0x10
#define ADC_ADDRESS3 0x18
//Voltage devider
#define ADC_ADDRESS4 0x20

#define SPI_BUFF_SIZE 20
// BUF size org. 2

#define GPIO_1OE 10
#define GPIO_2OE 71

//Only support one device for now!!
static int number_of_devices = 1;

struct nxtts_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct spi_device *spi_device;
	char *user_buff;
};

struct nxtts_control {
	struct spi_message msg;
	struct spi_transfer transfer;
	u8 *tx_buff; 
	u8 *rx_buff;
};

static struct nxtts_control nxtts_ctl;
static struct nxtts_dev nxtts_dev;


static void nxtts_prepare_spi_message(void)
{
	spi_message_init(&nxtts_ctl.msg);

	nxtts_ctl.tx_buff[0] = ADC_ADDRESS0;
	nxtts_ctl.tx_buff[1] = 0x00;
	nxtts_ctl.tx_buff[2] = ADC_ADDRESS0;
	nxtts_ctl.tx_buff[3] = 0x00;
	
	
	nxtts_ctl.tx_buff[4] = ADC_ADDRESS1;
	nxtts_ctl.tx_buff[5] = 0x00;
	nxtts_ctl.tx_buff[6] = ADC_ADDRESS1;
	nxtts_ctl.tx_buff[7] = 0x00;


	nxtts_ctl.tx_buff[8] = ADC_ADDRESS2;
	nxtts_ctl.tx_buff[9] = 0x00;
	nxtts_ctl.tx_buff[10] = ADC_ADDRESS2;
	nxtts_ctl.tx_buff[11] = 0x00;
		

	nxtts_ctl.tx_buff[12] = ADC_ADDRESS3;
	nxtts_ctl.tx_buff[13] = 0x00;
	nxtts_ctl.tx_buff[14] = ADC_ADDRESS3;
	nxtts_ctl.tx_buff[15] = 0x00;
	

	nxtts_ctl.tx_buff[16] = ADC_ADDRESS4;
	nxtts_ctl.tx_buff[17] = 0x00;
	nxtts_ctl.tx_buff[18] = ADC_ADDRESS4;
	nxtts_ctl.tx_buff[19] = 0x00;
	
	memset(nxtts_ctl.rx_buff, 0, SPI_BUFF_SIZE);

	nxtts_ctl.transfer.tx_buf = nxtts_ctl.tx_buff;
	nxtts_ctl.transfer.rx_buf = nxtts_ctl.rx_buff;
	nxtts_ctl.transfer.len = 20;

	spi_message_add_tail(&nxtts_ctl.transfer, &nxtts_ctl.msg);
}

static int nxtts_do_one_message(void)
{
	int status;

	nxtts_prepare_spi_message();

	status = spi_sync(nxtts_dev.spi_device, &nxtts_ctl.msg);

	return status;	
}

static ssize_t nxtts_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	size_t len;
	ssize_t status = 0;

	if (!buff) 
		return -EFAULT;

	if (*offp > 0){
		printk(KERN_DEBUG "offp: %d",*offp); 
		return 0;
	}
	if (!nxtts_dev.spi_device)
		strcpy(nxtts_dev.user_buff, "spi_device is NULL\n");
	else if (!nxtts_dev.spi_device->master)
		strcpy(nxtts_dev.user_buff, "spi_device->master is NULL\n");
	else {
		status = nxtts_do_one_message();
		sprintf(nxtts_dev.user_buff, "Status: %d\nADC0: TX: %.2X %.2X %.2X %.2X   RX: %.2X %.2X %.2X %.2X\nADC1: TX: %.2X %.2X %.2X %.2X   RX: %.2X %.2X %.2X %.2X\nADC2: TX: %.2X %.2X %.2X %.2X   RX: %.2X %.2X %.2X %.2X\nADC3: TX: %.2X %.2X %.2X %.2X   RX: %.2X %.2X %.2X %.2X\nADC4: TX: %.2X %.2X %.2X %.2X   RX: %.2X %.2X %.2X %.2X\n", nxtts_ctl.msg.status, 
nxtts_ctl.tx_buff[0], nxtts_ctl.tx_buff[1],nxtts_ctl.tx_buff[2],nxtts_ctl.tx_buff[3], 
nxtts_ctl.rx_buff[0], nxtts_ctl.rx_buff[1],nxtts_ctl.rx_buff[2],nxtts_ctl.rx_buff[3],
nxtts_ctl.tx_buff[4], nxtts_ctl.tx_buff[5],nxtts_ctl.tx_buff[6],nxtts_ctl.tx_buff[7], 
nxtts_ctl.rx_buff[4], nxtts_ctl.rx_buff[5],nxtts_ctl.rx_buff[6],nxtts_ctl.rx_buff[7],
nxtts_ctl.tx_buff[8], nxtts_ctl.tx_buff[9],nxtts_ctl.tx_buff[10],nxtts_ctl.tx_buff[11], 
nxtts_ctl.rx_buff[8], nxtts_ctl.rx_buff[9],nxtts_ctl.rx_buff[10],nxtts_ctl.rx_buff[11],
nxtts_ctl.tx_buff[12], nxtts_ctl.tx_buff[13],nxtts_ctl.tx_buff[14],nxtts_ctl.tx_buff[15], 
nxtts_ctl.rx_buff[12], nxtts_ctl.rx_buff[13],nxtts_ctl.rx_buff[14],nxtts_ctl.rx_buff[15],
nxtts_ctl.tx_buff[16], nxtts_ctl.tx_buff[17],nxtts_ctl.tx_buff[18],nxtts_ctl.tx_buff[19],
nxtts_ctl.rx_buff[16], nxtts_ctl.rx_buff[17],nxtts_ctl.rx_buff[18],nxtts_ctl.rx_buff[19]);
	}

	len = strlen(nxtts_dev.user_buff);
 
	if (len < count) 
		count = len;

	if (copy_to_user(buff, nxtts_dev.user_buff, count))  {
		printk(KERN_DEBUG "nxtts_read(): copy_to_user() failed\n");
		status = -EFAULT;
	} else {
		*offp += count;
		status = count;
	}

	return status;	
}

static int nxtts_open(struct inode *inode, struct file *filp)
{	
	int status = 0;

	if (!nxtts_dev.user_buff) {
		nxtts_dev.user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!nxtts_dev.user_buff) 
			status = -ENOMEM;
	}	


	return status;
}

static int nxtts_probe(struct spi_device *spi_device)
{
  printk("Inside nxtts_probe()\n");
	nxtts_dev.spi_device = spi_device;

	return 0;
}

static int nxtts_remove(struct spi_device *spi_device)
{
  printk("Inside nxtts_remove()\n");

	nxtts_dev.spi_device = NULL;

	return 0;
}

static int __init add_nxtts_device_to_bus(void)
{
	struct spi_master *spi_master;
	struct spi_device *spi_device;
	struct device *pdev;
	char buff[64];
	int status = 0;

	/* This call returns a refcounted pointer to the relevant spi_master - the caller must release this pointer(device_put()) */	
	spi_master = spi_busnum_to_master(SPI_BUS);
	if (!spi_master) {
		printk(KERN_ALERT "spi_busnum_to_master(%d) returned NULL\n", SPI_BUS);
		printk(KERN_ALERT "Missing modprobe omap2_mcspi?\n");
		return -1;
	}

	spi_device = spi_alloc_device(spi_master);
	if (!spi_device) {
		printk(KERN_ALERT "spi_alloc_device() failed\n");
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
		 * It is okay if it us, otherwise complain and fail.
		 */
		if (pdev->driver && pdev->driver->name && strcmp(DEVICE_NAME, pdev->driver->name)) {
			printk(KERN_ALERT "Driver [%s] already registered for %s\n", pdev->driver->name, buff);
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
			printk(KERN_ALERT "spi_add_device() failed: %d\n", status);		
		}				
	}
	/* See comment for spi_busnum_to_master */
	put_device(&spi_master->dev);

	return status;
}

static struct spi_driver nxtts_driver = {
	.driver = {
		.name =	DEVICE_NAME,
		.owner = THIS_MODULE,
	},
	.probe = nxtts_probe,
	.remove = __devexit_p(nxtts_remove),	
};

static int __init nxtts_init_spi(void)
{
	int error;

	nxtts_ctl.tx_buff = kzalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!nxtts_ctl.tx_buff) {
		error = -ENOMEM;
		goto nxtts_init_error;
	}

	nxtts_ctl.rx_buff = kzalloc(SPI_BUFF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!nxtts_ctl.rx_buff) {
		error = -ENOMEM;
		goto nxtts_init_error;
	}

	error = spi_register_driver(&nxtts_driver);
	if (error < 0) {
		printk(KERN_ALERT "spi_register_driver() failed %d\n", error);
		return error;
	}

	error = add_nxtts_device_to_bus();
	if (error < 0) {
		printk(KERN_ALERT "add_nxtts_to_bus() failed\n");
		spi_unregister_driver(&nxtts_driver);
		return error;
	}

	return 0;

nxtts_init_error:

	if (nxtts_ctl.tx_buff) {
		kfree(nxtts_ctl.tx_buff);
		nxtts_ctl.tx_buff = 0;
	}

	if (nxtts_ctl.rx_buff) {
		kfree(nxtts_ctl.rx_buff);
		nxtts_ctl.rx_buff = 0;
	}

	return error;
}

static const struct file_operations nxtts_fops = {
	.owner =	THIS_MODULE,
	.read = 	nxtts_read,
	.open =		nxtts_open,	
};

static int __init nxtts_init_cdev(void)
{
	int error;

	nxtts_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&nxtts_dev.devt, 0, number_of_devices, DEVICE_NAME);
	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", 
			error);
		return -1;
	}

	cdev_init(&nxtts_dev.cdev, &nxtts_fops);
	nxtts_dev.cdev.owner = THIS_MODULE;

	error = cdev_add(&nxtts_dev.cdev, nxtts_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(nxtts_dev.devt, 1);
		return -1;
	}	

	return 0;
}

static int __init nxtts_init_class(void)
{
	nxtts_dev.class = class_create(THIS_MODULE, DEVICE_NAME);

	if (IS_ERR(nxtts_dev.class)) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(nxtts_dev.class, NULL, nxtts_dev.devt, NULL, DEVICE_NAME)) {
		printk(KERN_ALERT "device_create(..., %s) failed\n", DEVICE_NAME);
		class_destroy(nxtts_dev.class);
		return -1;
	}

	return 0;
}

static int __init nxtts_init_level_shifters(void) {

if (gpio_request(GPIO_1OE, "SPI1OE")) {
    printk(KERN_ALERT "gpio_request failed for SPI1OE\n");
    goto init_pins_fail_1;
  }

  if (gpio_request(GPIO_2OE, "SPI2OE")) {
    printk(KERN_ALERT "gpio_request failed for SPI2OE\n");
    goto init_pins_fail_2;
  }

  if (gpio_direction_output(GPIO_1OE, 0)) {
    printk(KERN_ALERT "gpio_direction_output GPIO_1OE failed\n");
    goto init_pins_fail_3;
  }

  if (gpio_direction_output(GPIO_2OE, 0)) {
    printk(KERN_ALERT "gpio_direction_output GPIO_2OE failed\n");
    goto init_pins_fail_3;
  }

  //  goto init_pins_fail_1;
  return 0;

 init_pins_fail_3:
  gpio_free(GPIO_2OE);

 init_pins_fail_2:
  gpio_free(GPIO_1OE);

 init_pins_fail_1:

  return -1;
}

static int __init nxtts_init(void)
{
  printk("Init the nxtts driver\n");
	memset(&nxtts_dev, 0, sizeof(nxtts_dev));
	memset(&nxtts_ctl, 0, sizeof(nxtts_ctl));

	if (nxtts_init_cdev() < 0) 
		goto fail_1;

	if (nxtts_init_class() < 0)  
		goto fail_2;

	if (nxtts_init_spi() < 0) 
		goto fail_3;

	if (nxtts_init_level_shifters() < 0)
	  goto fail_4;


	return 0;

 fail_4:
	gpio_free(GPIO_2OE);
	gpio_free(GPIO_1OE);

fail_3:
	device_destroy(nxtts_dev.class, nxtts_dev.devt);
	class_destroy(nxtts_dev.class);

fail_2:
	cdev_del(&nxtts_dev.cdev);
	unregister_chrdev_region(nxtts_dev.devt, 1);

fail_1:
	return -1;
}
module_init(nxtts_init);

static void __exit nxtts_exit(void)
{
  printk("Exiting the nxtts driver\n");
	spi_unregister_driver(&nxtts_driver);

	device_destroy(nxtts_dev.class, nxtts_dev.devt);
	class_destroy(nxtts_dev.class);

	cdev_del(&nxtts_dev.cdev);
	unregister_chrdev_region(nxtts_dev.devt, 1);

	if (nxtts_ctl.tx_buff)
		kfree(nxtts_ctl.tx_buff);

	if (nxtts_ctl.rx_buff)
		kfree(nxtts_ctl.rx_buff);

	if (nxtts_dev.user_buff)
		kfree(nxtts_dev.user_buff);

	/* Release spi level shifter pins */
	gpio_free(GPIO_2OE);
	gpio_free(GPIO_1OE);

}
module_exit(nxtts_exit);
MODULE_AUTHOR("Tja");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

