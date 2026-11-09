#!/bin/sh
# 板子上重测：sh /tmp/retest.sh
export PATH=$PATH:/sbin:/usr/sbin

DHT=/home/root/dht22_iio/dht22_iio.ko
PIR=/home/root/pir_gpio/pir_gpio.ko

rmmod pir_gpio 2>/dev/null
rmmod dht22_iio 2>/dev/null
dmesg -c >/dev/null

insmod "$DHT" && insmod "$PIR"
dmesg | grep -E "dht22|pir" | tail -5

echo "== IIO 属性"
ls /sys/bus/iio/devices/iio:device0/ | tr '\n' ' '; echo
echo -n "in_temp_input        = "; cat /sys/bus/iio/devices/iio:device0/in_temp_input
echo -n "in_temp_raw          = "; cat /sys/bus/iio/devices/iio:device0/in_temp_raw
echo -n "in_temp_scale        = "; cat /sys/bus/iio/devices/iio:device0/in_temp_scale
echo -n "in_humidityrelative_input = "; cat /sys/bus/iio/devices/iio:device0/in_humidityrelative_input

echo "== pinctrl"
grep -E "pin (27|32) " /sys/kernel/debug/pinctrl/20e0000.iomuxc/pinmux-pins

echo "== PIR：60 秒内等事件"
timeout 60 cat /dev/pir | head -5
echo "(超时说明这段时间没有 PIR 事件，不一定是驱动问题)"
grep "38:" /proc/interrupts
