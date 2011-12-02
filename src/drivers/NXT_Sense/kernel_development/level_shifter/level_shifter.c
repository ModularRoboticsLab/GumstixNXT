/* Notes:
 * - The key point of using kref as the refcount has disappeared after a rewrite to support multiple level shifters, but it remains in the code as it is still used for refcounting, but could easily be replaced by a simple int, given concurrency issues are kept taken care of.
 * - The functionality could be expanded to also include the possibility of enabling and disabling the level shifter (setting OE to low or high respectively).
 * - Currently there is only one mutex that guards the two entry points {register|unregister}_use_of_level_shifter, but could be replaced by one mutex for each level_shifter so two modules wanting different level shifters don't have to wait on each other - but favoring the size in size vs performance tradeoff for now (given that the activity of wanting to register/unregister the use of a level shifter is rare).
 *
 * - The functionality of this module could be greatly reduced, if just wanting a level shifter module that requests the GPIOs upon loading and frees them again when unloading, given that the level shifters should be enabled all the time
 */
#include <linux/module.h>
#include <mach/gpio.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "level_shifter.h"

#define DEVICE_NAME "level_shifter"

/* The current naming scheme only requires 10 bytes for the labels */
#define LS_LABEL_SIZE 10

/* The level shifter is referenced to as U3 on the gumstixnxt schematic */
#define GPIO_U3_1OE 10
#define GPIO_U3_2OE 71
/* There is a dependency between the number of level shifters and the enum defined in level_shifter.h */
#define NUMBER_OF_LEVEL_SHIFTERS 2

DEFINE_MUTEX(ls_mutex);

struct level_shifter {
  int gpio_pin;
  bool activated; /* Bookkeeping of whether or not the gpio_pin is currently requested (using gpio_request()) */
  char label[LS_LABEL_SIZE];
  struct kref refcount;
};

static struct level_shifter level_shifter[NUMBER_OF_LEVEL_SHIFTERS];

/***********************************************************************
 *
 * Utility functions for activating and deactivating the sub level
 * shifters
 *
 ***********************************************************************/
static int activate_sub_level_shifter(struct level_shifter *ls) {
  int status = 0;
  if (gpio_request(ls->gpio_pin, ls->label)) {
    printk(KERN_CRIT DEVICE_NAME ": gpio_request failed for pin %d labelled %s\n", ls->gpio_pin, ls->label);
    status = -1;
  } else {
    ls->activated = true;
    if (gpio_direction_output(ls->gpio_pin, 0)) {
      printk(KERN_CRIT DEVICE_NAME ": gpio_direction_output failed for pin %d labelled %s\n", ls->gpio_pin, ls->label);
      /* Free the gpio pin again if it can't be set to low */
      gpio_free(ls->gpio_pin);
      status = -2;
    }
  }

  /* Initialise kref / refcount: sets the refcount to 1 (this is part of a work around due to the context kref is used within and that kref_get doesn't work when the internal refcount is 0) */
  kref_init(&ls->refcount);

  return status;
}  

static void deactivate_sub_level_shifter(struct kref *kref) {
  struct level_shifter *ls;
  ls = container_of(kref, struct level_shifter, refcount);

  gpio_free(ls->gpio_pin);
  ls->activated = false;
}

/***********************************************************************
 *
 * Hooks for registering and unregistering the use of the GPIO pin from
 * other modules
 *
 ***********************************************************************/
/* Returns 0 on success, else a negative error code */
int register_use_of_level_shifter(const enum level_shifter_tag lst) {
  struct level_shifter *ls;
  mutex_lock(&ls_mutex);

  if (lst < 0 || lst >= NUMBER_OF_LEVEL_SHIFTERS) {
    mutex_unlock(&ls_mutex);
    printk(KERN_WARNING DEVICE_NAME ": Trying to register the use of a level shifter that is not known: %d\n", lst);
    return -1;
  }

  ls = &level_shifter[lst];

  if (!ls->activated) {
    if (activate_sub_level_shifter(ls) != 0) {
      /* Could not get the level_shifter up and running... problems with GPIO, see activate_sub_level_shifter for more info */
      mutex_unlock(&ls_mutex);
      return -2;
    }
  } else {
    kref_get(&ls->refcount);
  }

  mutex_unlock(&ls_mutex);

  return 0;  
}

int unregister_use_of_level_shifter(const enum level_shifter_tag lst) {
  struct level_shifter *ls;
  mutex_lock(&ls_mutex);

  if (lst < 0 || lst >= NUMBER_OF_LEVEL_SHIFTERS) {
    printk(KERN_WARNING DEVICE_NAME ": Trying to unregister the use of a level shifter that is not known: %d\n", lst);
    return -1;
  }

  ls = &level_shifter[lst];

  kref_put(&ls->refcount, deactivate_sub_level_shifter);

  mutex_unlock(&ls_mutex);

  return 0;
}

EXPORT_SYMBOL(register_use_of_level_shifter);
EXPORT_SYMBOL(unregister_use_of_level_shifter);

/***********************************************************************
 *
 * Module initialisation and exit functions to make sure the module
 * is set up correctly and cleans up after itself
 *
 ***********************************************************************/
static int __init level_shifter_init(void) {
  level_shifter[0].gpio_pin = GPIO_U3_1OE;
  level_shifter[0].activated = false;
  strlcpy(level_shifter[0].label, "LS_U3_1OE", LS_LABEL_SIZE);

  level_shifter[1].gpio_pin = GPIO_U3_2OE;
  level_shifter[1].activated = false;
  strlcpy(level_shifter[1].label, "LS_U3_2OE", LS_LABEL_SIZE);

  /* Leaving the kref / refcount objects uninitialised due to a workaround when using kref for bookkeeping and not actual object reference counting */

  return 0;
}

static void __exit level_shifter_exit(void) {
  int i;

  for (i = 0; i < NUMBER_OF_LEVEL_SHIFTERS; ++i) {
    if (level_shifter[i].activated) {
      printk(KERN_DEBUG DEVICE_NAME ": Upon exit level_shifter[%d] was still activated! (gpio pin: %d label: %s and refcount: %d\n", i, level_shifter[i].gpio_pin, level_shifter[i].label, level_shifter[i].refcount.refcount.counter);
      do {
	if (unregister_use_of_level_shifter(i) != 0) {
	  printk(KERN_DEBUG DEVICE_NAME ": Exit function failed to unregister level shifter %d\n", i);
	}
      } while (level_shifter[i].activated);
    }
  }
}

module_init(level_shifter_init);
module_exit(level_shifter_exit);

MODULE_LICENSE("GPL");
