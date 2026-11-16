#!/bin/bash

echo 15 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio15/direction
while true
do
    echo 1 > /sys/class/gpio/gpio15/value
    sleep 0.2

    echo 0 > /sys/class/gpio/gpio15/value
    sleep 0.2
done
