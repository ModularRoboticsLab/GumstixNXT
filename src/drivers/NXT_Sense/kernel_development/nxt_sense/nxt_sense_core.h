#ifndef __H_nxt_sense_core_h_
#define __H_nxt_sense_core_h_

enum scl_bit_flags {SCL_LOW = 0, SCL_HIGH, SCL_TOGGLE};

struct nxt_sense_device_data {
  dev_t devt;
  struct cdev cdev;
  struct device *device;
  int (*get_sample)(int *);
  int (*scl)(enum scl_bit_flags);
};

extern int nxt_setup_sensor_chrdev(const struct file_operations *, struct nxt_sense_device_data *, const char *);
extern int nxt_teardown_sensor_chrdev(struct nxt_sense_device_data *);

#endif
