#! /bin/sh

# OE for the level converter, direction is fixed as output in hw
echo 92 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio92/direction
echo 0 > /sys/class/gpio/gpio92/value

# GPIO144 is set up by another driver by default so just shut down the motor
echo 1 > /sys/class/gpio/gpio144/value

# Enable for motor 2
echo 146 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio146/direction
echo 1 > /sys/class/gpio/gpio146/value

# direction for motor 1
echo 91 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio91/direction
echo 0 > /sys/class/gpio/gpio91/value

# direction for motor 2
echo 83 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio83/direction
echo 0 > /sys/class/gpio/gpio83/value
