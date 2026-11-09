#!/bin/sh
# 在板子上跑：sh /tmp/b-test.sh /path/dht22_iio.ko /path/pir_gpio.ko
set -u

DHT=${1:-/tmp/dht22_iio.ko}
PIR=${2:-/tmp/pir_gpio.ko}

say() { echo; echo "===== $*"; }

say "0. 清理旧模块"
rmmod pir_gpio 2>/dev/null
rmmod dht22_iio 2>/dev/null
dmesg -c >/dev/null 2>&1

say "1. insmod 不带任何参数（应当走设备树）"
insmod "$DHT" || { echo "dht22 insmod 失败"; exit 1; }
insmod "$PIR" || { echo "pir insmod 失败"; exit 1; }
dmesg | grep -E "dht22|pir" | tail -10

say "2. platform device 绑定情况"
ls -l /sys/bus/platform/devices/dht22 /sys/bus/platform/devices/pir 2>/dev/null
for d in dht22 pir; do
    drv=$(readlink /sys/bus/platform/devices/$d/driver 2>/dev/null)
    echo "$d driver -> ${drv:-<none>}"
done

say "3. pinctrl 是否真的被认领"
grep -E "pin (27|32) " /sys/kernel/debug/pinctrl/20e0000.iomuxc/pinmux-pins

say "4. IIO 读数（间隔 2s 读两次）"
for i in 1 2; do
    echo -n "temp=$(cat /sys/bus/iio/devices/iio:device*/in_temp_input 2>/dev/null) "
    echo "hum=$(cat /sys/bus/iio/devices/iio:device*/in_humidityrelative_input 2>/dev/null)"
    sleep 2
done

say "5. /dev/pir 与 enable 属性"
ls -l /dev/pir
cat /sys/class/misc/pir/dev
cat /sys/class/misc/pir/device/enable

say "6. rmmod -> insmod 再试一次（旧版这里会 GPIO busy）"
rmmod pir_gpio
rmmod dht22_iio
sleep 1
insmod "$DHT" && echo "dht22 二次 insmod OK" || echo "dht22 二次 insmod 失败"
insmod "$PIR" && echo "pir 二次 insmod OK" || echo "pir 二次 insmod 失败"
dmesg | grep -E "dht22|pir" | tail -6

say "7. 再读一次值"
echo -n "temp=$(cat /sys/bus/iio/devices/iio:device*/in_temp_input 2>/dev/null) "
echo "hum=$(cat /sys/bus/iio/devices/iio:device*/in_humidityrelative_input 2>/dev/null)"
