# 项目 B 测试记录：platform driver + 设备树

> 日期：2026-11-09
> 板子：ATK-IMX6U（i.MX6ULL），Linux 4.1.15-g3dc0a4b
> 内核模块 commit：`c89fcf7`
> 设备树：`dts/` 下从板子 DTB 反编译后追加节点重编，已刷入 SD 启动分区

原则同 `docs/test-report.md`：下面的数字都来自板子上的原始输出，没有推测。

---

## 1. 设备树（刷入并重启后）

```text
/proc/device-tree/dht22   compatible / data-gpios / pinctrl-0 / status 齐全
/proc/device-tree/pir     compatible / irq-gpios  / pinctrl-0 / status 齐全
/sys/bus/platform/devices 出现 dht22、pir
/sys/class/graphics/fb0/modes = U:480x272p-56   ← 屏参没被破坏
```

启动日志里 `mxsfb ... failed to find mxc display driver`、`ap3216c ... failed`、
`fsl-quadspi ... probe failed` 与刷 DTB 之前完全一致，不是这次引入的。

## 2. 模块加载（不带任何参数）

```text
$ insmod /home/root/dht22_iio/dht22_iio.ko
$ insmod /home/root/pir_gpio/pir_gpio.ko
[ 2247.998787] dht22 dht22: probed, from device tree
[ 2248.037562] pir pir: probed, irq=38, initial=1, from device tree
```

`from device tree` 而不是 `from module parameter`，说明 v0.1 的
`insmod xxx.ko gpio_num=N` 已经不需要了。

## 3. pinctrl 认领

```text
pin 27 (MX6UL_PAD_GPIO1_IO04): dht22 (GPIO UNCLAIMED) function imx6ul-evk group zl_dht22grp
pin 32 (MX6UL_PAD_GPIO1_IO09): pir   (GPIO UNCLAIMED) function imx6ul-evk group zl_pirgrp
```

两个脚在没加载驱动前是 `MUX UNCLAIMED`，加载后归属正确 ——
说明 pinctrl-0 从设备树里真的生效了，不是碰巧的默认复用。

## 4. DHT22 IIO

```text
$ cat /sys/bus/iio/devices/iio:device0/in_temp_input
19600
$ cat /sys/bus/iio/devices/iio:device0/in_humidityrelative_input
50500
$ cat .../in_temp_raw      19600
$ cat .../in_temp_scale    1
```

19.6 °C / 50.5 %RH。属性列表：

```text
in_temp_input in_temp_raw in_temp_scale
in_humidityrelative_input in_humidityrelative_raw in_humidityrelative_scale
```

## 5. PIR 字符设备

中断：

```text
$ grep 38: /proc/interrupts
 38:        191  gpio-mxc   9 Edge      pir-out
```

read（60 s 窗口，`timeout 60 cat /dev/pir`）：

```text
1
1
1
1
1
```

同 60 s 内中断计数 178 → 191（双沿，约 6 个脉冲 5 次事件，holdoff 3 s 生效）。

poll（板子上用 python3 `select.select()`，测试脚本 `test/pir_poll_test.py`）：

```text
event 1 at 47.5s data=b'1\n'
event 2 at 51.3s data=b'1\n'
event 3 at 58.0s data=b'1\n'
total events=3
```

前 47 s 房间里没人动，传感器不出事件；挥手之后 select() 立刻返回。
间隔 3.8 s / 6.7 s，与 holdoff=3000 ms 吻合。

## 6. rmmod / insmod 循环

```text
cycle 1 ok
cycle 2 ok
dmesg 中 "probed, from device tree" 共 3 条（1 次初始 + 2 次循环）
dmesg 无 warn / oops / call trace
```

v0.1 的 `dht22_exit()` 只打印一句就返回，`iio_device_unregister()`、
`iio_device_free()`、`gpio_free()` 都没调，rmmod 之后 GPIO 一直被占，
第二次 insmod 必然失败。改成 platform_driver + `devm_*` 之后不再出现。

---

## 7. 结论

| 验收项 | 结果 |
|---|---|
| 不需要 gpio_num | **通过**（probe 日志 `from device tree`） |
| dmesg 有 probe | **通过** |
| DHT22 读数正常 | **通过**（19.6 °C / 50.5 %RH） |
| PIR poll/read 正常 | **通过**（read 5 次事件；select 3 次事件） |
| rmmod/insmod 不 oops | **通过**（2 轮循环无告警） |

## 8. 遗留

```text
poll 只在“人挥手”的时候验证过，没有做自动化的事件注入测试
PIR 硬件依旧没滤波电容、没 10k 下拉，长时间读数仍可能抖
DHT22 的 2 s 最小采样间隔内返回的是缓存值，没有做时间戳统计
设备树回退路径（没有 DT 节点时用 gpio_num）没有在板上实测，
  因为板子现在跑的就是带节点的 DTB
```
