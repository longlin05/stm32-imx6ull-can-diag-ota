# STM32 + i.MX6ULL CAN 诊断与 OTA（09）：i.MX6ULL 驱动——DHT22 IIO + PIR 字符设备，以及 GPIO/内核模块踩坑

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[08 Linux 网关：SocketCAN + gatewayd + SQLite + MQTT](https://blog.csdn.net/longlin101/article/details/170245246)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`b-v0.1`

08 是 i.MX6ULL 上的用户态网关。

09 换到内核态：

```text
DHT22 温湿度 → IIO 驱动
HC-SR501 PIR → 字符设备 + GPIO 中断
```

不是“写个程序读 GPIO”，而是两个 `.ko`。

---

## TL;DR

```text
DHT22
  DATA → GPIO_4
  VCC  → 3.3V
  GND  → GND

PIR
  OUT  → GPIO_9
  VCC  → 5V
  GND  → GND
```

测试：

```bash
insmod dht22_iio.ko gpio_num=4
cat /sys/bus/iio/devices/iio:device0/in_temp_input
cat /sys/bus/iio/devices/iio:device0/in_humidityrelative_input

insmod pir_gpio.ko gpio_num=9 debounce_ms=200 holdoff_ms=3000
cat /dev/pir
```

实测：

```text
22.0°C
76.2%RH
PIR 有事件输出，硬件无电容时仍有抖动
```

---

## 1. 为什么换成 DHT22

原本计划 AHT20，但手头用不了。

DHT22 更合适：

```text
温度 -40 ~ 80°C，0.1°C
湿度 0 ~ 100%RH，0.1%RH
单总线
采样周期 ≥2s
```

DHT11 也能用，但只有整数精度。

---

## 2. 内核源码和工具链

板子：

```bash
uname -a
Linux ATK-IMX6U 4.1.15-g3dc0a4b ...
```

源码：

```text
linux-imx-rel_imx_4.1.15_2.1.0_ga
```

工具链：

```bash
source /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
```

板子配置：

```bash
zcat /proc/config.gz > config-4.1.15
```

关键配置：

```text
CONFIG_LOCALVERSION=""
CONFIG_LOCALVERSION_AUTO=y
CONFIG_MODVERSIONS=y
```

`make kernelrelease` 一开始输出：

```text
4.1.15
```

不是板子的：

```text
4.1.15-g3dc0a4b
```

修正：

```bash
scripts/config --set-str LOCALVERSION "-g3dc0a4b"
scripts/config --disable LOCALVERSION_AUTO
make ARCH=arm CROSS_COMPILE=arm-poky-linux-gnueabi- oldconfig
```

之后：

```text
4.1.15-g3dc0a4b
```

---

## 3. 坑一：Module.symvers 不完整

只跑：

```bash
make modules
```

生成的 `Module.symvers` 没有：

```text
module_layout
```

加载：

```text
hello: no symbol version for module_layout
```

解决：

```bash
make -j$(nproc) vmlinux
```

之后：

```text
0xfa985410  module_layout  vmlinux  EXPORT_SYMBOL
```

再编模块，`hello.ko` 才能加载。

原因：

```text
CONFIG_MODVERSIONS=y
模块需要每个导出符号的 CRC
module_layout 来自 vmlinux
```

---

## 4. 坑二：GPIO_6 不是 GPIO1_IO06

排针丝印：

```text
GPIO_6
```

物理量到 3.3V，但：

```bash
echo 6 > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio6/direction
cat /sys/class/gpio/gpio6/value
```

读到：

```text
0
```

说明丝印和 Linux GPIO 编号不是一一对应，或者该 pad 被复用。

扫描：

```bash
for n in 4 6 7 8 9; do
  echo $n > /sys/class/gpio/export 2>/dev/null
  echo in > /sys/class/gpio/gpio$n/direction
  printf "gpio%s = " "$n"
  cat /sys/class/gpio/gpio$n/value
done
```

最后改用：

```text
DHT22 DATA → GPIO_4
PIR OUT    → GPIO_9
```

验证方法：

```bash
cat /sys/class/gpio/gpio4/value
→ 1
```

DHT22 空闲时 DATA 被上拉为高。

---

## 5. DHT22 协议

DHT22 是单总线，不是 I2C。

时序：

```text
主机拉低 ≥1ms
释放总线
DHT 拉低 80us
DHT 拉高 80us
40 bit：
  50us 低 + 26-28us 高 = 0
  50us 低 + 70us 高 = 1
```

读的时候要保证时序：

```c
local_irq_save(flags);
gpio_direction_output(gpio, 0);
udelay(2000);
gpio_direction_input(gpio);
...
udelay(40);
bit = gpio_get_value(gpio);
local_irq_restore(flags);
```

40us 采样点：

- 26us 高 → 已经变低 → 0
- 70us 高 → 仍为高 → 1

校验：

```c
if ((data[0]+data[1]+data[2]+data[3]) != data[4])
    return -EIO;
```

温度有符号：

```c
int16_t t = (data[2] << 8) | data[3];
if (t & 0x8000)
    t = -(t & 0x7FFF);
```

---

## 6. IIO：RAW 还是 PROCESSED

第一版：

```c
.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
```

生成的是：

```text
in_temp_raw
in_humidityrelative_raw
```

没有：

```text
in_temp_input
in_humidityrelative_input
```

改成：

```c
.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
```

`read_raw`：

```c
if (mask != IIO_CHAN_INFO_PROCESSED)
    return -EINVAL;
```

才出现：

```text
in_temp_input
in_humidityrelative_input
```

返回值直接用：

```text
温度 milli°C
湿度 milli%RH
```

实测：

```bash
cat /sys/bus/iio/devices/iio:device0/in_temp_input
22000

cat /sys/bus/iio/devices/iio:device0/in_humidityrelative_input
76200
```

即：

```text
22.0°C
76.2%RH
```

---

## 7. PIR 字符设备

HC-SR501：

```text
VCC 5V
GND GND
OUT GPIO_9
```

驱动：

```text
misc_register → /dev/pir
gpio_to_irq
request_irq
read / poll
```

第一版双边沿：

```c
IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
```

结果一次动作会输出：

```text
1
0
1
0
```

改成只报上升沿：

```c
IRQF_TRIGGER_RISING
```

---

## 8. 消抖和 holdoff

PIR 输出会抖，尤其没有滤波电容。

驱动里做两层：

### 200ms 稳定

```c
irq:
  mod_timer(&pir_debounce, jiffies + msecs_to_jiffies(200));

timer:
  value = gpio_get_value(gpio_num);
  if (value && !pir_level)
      event = 1;
```

### 3s holdoff

```c
if (time_after(jiffies, last_event + msecs_to_jiffies(3000))) {
    last_event = jiffies;
    event = 1;
    wake_up_interruptible(&wq);
}
```

这样一次动作最多 3 秒报一次。

硬件建议：

```text
VCC-GND 并 100uF + 0.1uF
5V 独立供电
共地
跳线选 L（非重复触发）
延时电位器 3~5 秒
灵敏度调低
预热 30~60 秒
```

当前没有电容，PIR 仍会偶发多次输出；这部分归为硬件限制。

---

## 9. 当前结果

```text
DHT22 IIO         通过
in_temp_input     22000
in_humidity       76200
PIR /dev/pir      基本通过
消抖              200ms + 3s holdoff
无电容             仍有偶发抖动
```

---

## 10. 目录

```text
drivers/
├── hello/
├── dht22_iio/
│   ├── dht22_iio.c
│   └── Makefile
└── pir_gpio/
    ├── pir_gpio.c
    └── Makefile
```

编译：

```bash
cd ~/imx6ull/dht22_iio
make
cd ~/imx6ull/pir_gpio
make
```

---

## 11. 下一步

```text
PIR 补 poll 测试
DHT22 加 IIO buffer
B 项目测试报告
```

---

## 12. 仓库 / 数据

- 仓库（项目 A）：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- 驱动仓库（项目 B，已独立）：https://github.com/longlin05/imx6ull-dht22-pir-drivers
- tag：`b-v0.1`（驱动代码现在在 B 仓库）
- 驱动：B 仓库的 `drivers/dht22_iio`、`drivers/pir_gpio`
- 测试日志：`test/records/20261107-driver-smoke.log`
- 后续（设备树 + platform driver）：B 系列 `i.MX6ULL 驱动笔记`

标签：`Linux` `i.MX6ULL` `IIO` `DHT22` `PIR` `GPIO` `内核模块`
