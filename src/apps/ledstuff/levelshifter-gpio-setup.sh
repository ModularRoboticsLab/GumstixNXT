#! /bin/sh

# Uses the userspace gpio interface to set configure the levelshifter-controlling GPIOs for LED blinking

echo 80 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio80/direction
echo 1 > /sys/class/gpio/gpio80/value
echo 67 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio67/direction
echo 0 > /sys/class/gpio/gpio67/value
