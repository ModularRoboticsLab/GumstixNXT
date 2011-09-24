#! /bin/sh

# shutdown motor 1
echo 1 > /sys/class/gpio/gpio144/value

# shutdown motor 2
echo 1 > /sys/class/gpio/gpio146/value
