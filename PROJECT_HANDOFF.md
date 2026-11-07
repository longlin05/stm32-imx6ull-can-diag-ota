# 项目交接文档：CAN 诊断/OTA + i.MX6ULL 驱动

> 用途：换新会话时先读本文件。  
> 更新时间：2026-11-07  
> 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota

---

## 1. 当前目标

两个项目：

```text
项目 A：STM32F407 + i.MX6ULL CAN 诊断、Bootloader、OTA、Linux 网关
项目 B：i.MX6ULL Linux 传感器驱动（DHT22 IIO + PIR 字符设备）
```

求职方向：

```text
Linux 驱动 / MCU 开发实习
```

---

## 2. 仓库与代码位置

### Windows 主仓库

```text
D:\嵌入式学习\嵌入式软件开发\stm32-imx6ull-can-diag-ota
```

当前分支：

```text
main
```

最新 commit：

```text
94aa305 test: record 15min soak and add 10 test report
```

tag：

```text
a-v1.0   功能版：3 次 OTA + 15 分钟 soak
b-v0.1   i.MX6ULL 驱动冒烟版
```

### STM32 工程

```text
D:\STM32CubeIDE\My_AI\Bootloader
D:\STM32CubeIDE\My_AI\My_AI_A
D:\STM32CubeIDE\My_AI\My_AI_B
D:\STM32CubeIDE\My_AI\boot_images
```

### i.MX6ULL 网关

```text
Windows: D:\Linux\imx6ull\gateway
虚拟机:  /home/lin/imx6ull/gateway
板子:    /home/root/my_ai
```

### 内核源码

```text
虚拟机:
/home/lin/imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga
```

### 驱动代码

主仓库内：

```text
drivers/hello
drivers/dht22_iio
drivers/pir_gpio
```

---

## 3. 硬件与网络

### 板子

```text
STM32F407VGT6-Disc
i.MX6ULL ATK-IMX6U
```

### 网络

```text
i.MX6ULL eth0: 192.168.137.103
PC broker:     192.168.137.1:1883
```

注意：

```text
connman 已关闭，避免把静态 IP 改回 DHCP
/etc/network/interfaces 里 eth0 是 static
开机自启用 /etc/rc.local
```

### CAN

```text
500 kbps
Linux sample-point 0.750
STM32 sample-point 0.75
```

**关键：Linux 必须设 0.75。**

```bash
sudo ip link set can0 down
sudo ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100
```

已写入 `/etc/rc.local`：

```sh
/sbin/ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100 2>/dev/null
```

否则会出现：

```text
STM32: CAN tx: no free mailbox
ESR LEC=3 ACK error
```

---

## 4. 项目 A 当前状态

已完成：

```text
CAN 500 kbps
ISO-TP SF/FF/CF/FC，4095 字节
UDS 0x10/0x3E/0x22/0x19/0x14
5 类 DTC，sector 4 掉电保存
Bootloader A/B、CRC32、双 Metadata
trial / rollback / confirm
OTA 0x34/0x36/0x37
gatewayd + gwctl + SQLite + MQTT
DID F1B0 当前槽
```

测试数据：

```text
1000 次命令：P99 22.06 ms，0 重试
3 次 A↔B OTA 成功
15 分钟 soak：rows 3014→3182，PID 不变，CPU 0%，RSS 2092 KB
```

当前槽（最后已知）：

```text
App B，F1B0 = 01
```

OTA 镜像：

```text
app_a.img 73660
app_b.img 73660
```

注意：板子上的 `app_a.img/app_b.img` 必须是最新版 73660；旧版 73528 没有 F1B0。

---

## 5. 项目 A 已知问题

### 5.1 CAN 采样点

```text
Linux 0.866 vs STM32 0.75 → ACK error → no free mailbox
```

修复：

```bash
sample-point 0.75
```

### 5.2 UDS 串包

`0x34` 曾收到上一条 DTC 的 `59 02 FF`。

已修：

```text
发送前 drain socket
校验响应 SID：0x7F + 原 SID，或 SID+0x40
```

### 5.3 ISO-TP CF 发送失败

`isotp_tick()` 原来发送失败仍推进序号。

已修：

```text
tx_frame 返回非 0 时回滚 pos/seq/state/block/last_cf
```

### 5.4 OTA 和周期 DTC 抢总线

`poll_ms=5000` 时 OTA 可能偶发失败。

临时方案：

```ini
poll_ms=60000
```

正式方案未做：

```text
OTA 开始暂停周期 DTC，结束恢复
```

### 5.5 MQTT

内置最小 MQTT 3.1.1 QoS0 发布器：

```text
每次 publish 新建 TCP 连接
无 keepalive
无断线补传
```

### 5.6 gatewayd 日志

`gw_log` 跳过 `0x36`，所以 OTA 块错误只能看 candump/串口。

---

## 6. 项目 B 当前状态

已有：

```text
DHT22 IIO 驱动
PIR 字符设备
read / poll
200ms 消抖 + 3s holdoff
Linux 4.1.15 交叉编译
hello.ko 加载验证
```

实测：

```text
DHT22：22.0°C / 76.2%RH
PIR：/dev/pir 可读，poll 正常
```

硬件：

```text
DHT22 DATA → GPIO4（GPIO1_IO04）
DHT22 VCC  → 3.3V
DHT22 GND  → GND
DHT22 NC   → 不接
DHT22 模块自带 4.7k 上拉

PIR OUT → GPIO9（GPIO1_IO09）
PIR VCC → 5V
PIR GND → GND
```

PIR 问题：

```text
无滤波电容
OUT 多数 0.3V，GP1_09 端 3.3/0.3 跳
无 10k 下拉
软件已消抖，硬件仍不稳
```

结论：PIR 驱动可用，硬件受限。

---

## 7. 内核模块环境

板子：

```text
Linux 4.1.15-g3dc0a4b
```

工具链：

```bash
source /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
```

源码：

```text
/home/lin/imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga
```

已经做过：

```text
CONFIG_LOCALVERSION="-g3dc0a4b"
CONFIG_LOCALVERSION_AUTO 关闭
make vmlinux
Module.symvers 包含 module_layout
hello.ko 能 insmod
```

编模块：

```bash
make ARCH=arm CROSS_COMPILE=arm-poky-linux-gnueabi- \
  -C /home/lin/imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga \
  M=$PWD modules
```

---

## 8. 下一步：项目 B 设备树 + platform driver

### 目标

去掉：

```text
insmod dht22_iio.ko gpio_num=4
insmod pir_gpio.ko gpio_num=9
```

改成：

```text
设备树描述 GPIO
platform_driver probe 自动申请
pinctrl 配置复用
```

### 板级 DTS 已确认（2026-11-07，详见 `docs/b-dts-plan.md`）

```text
compatible = fsl,imx6ull-14x14-evk, fsl,imx6ull
model      = Freescale i.MX6 ULL 14x14 EVK Board
root       = /dev/mmcblk1p2（SD），boot 分区 mmcblk1p1 vfat
DT 里已有 alientek,dht11 / alientek,ds18b20 厂商节点
```

结论：板子 DTB 是**厂商改过的 EVK**，VM 里那棵 `rel_imx_4.1.15_2.1.0_ga` 是原版树，
不能直接拿它重编 DTB。

路线定为：

```text
从板子 boot 分区取 imx6ull-14x14-evk.dtb
  → dtc 反编译 → 加节点 → 重编成 imx6ull-14x14-evk-b.dtb
  → fw_setenv fdt_file 切换，回退只改 env
驱动同步转 platform_driver + of_match，module_param 留作回退
```

先确认 fdt_file 与厂商 dht11 占用的 GPIO（可能和 GPIO1_IO04 冲突），再动 DTS。

### 原始确认步骤（留档）

板子：

```bash
cat /proc/device-tree/compatible
ls /proc/device-tree
```

虚拟机：

```bash
ls /home/lin/imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga/arch/arm/boot/dts/ | grep -i imx6ull
```

如果源码里没有 ATK-IMX6U 的 DTS（已确认：VM 树是原版，板子是厂商 DTB）：

```text
A. 从板子 /sys/firmware/fdt 导出 DTB，反编译 DTS，加节点，重新编 DTB
B. 先做 platform driver + module parameter，设备树后补
```

A 更完整但有刷 DTB 风险；B 更安全。

### 设备树节点草案

```dts
dht22: dht22 {
    compatible = "project-a,dht22";
    data-gpios = <&gpio1 4 GPIO_ACTIVE_HIGH>;
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_dht22>;
    status = "okay";
};

pir: pir {
    compatible = "project-a,pir";
    irq-gpios = <&gpio1 9 GPIO_ACTIVE_HIGH>;
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_pir>;
    status = "okay";
};
```

```dts
pinctrl_dht22: dht22grp {
    fsl,pins = <
        MX6UL_PAD_GPIO1_IO04__GPIO1_IO04 0x10B0
    >;
};

pinctrl_pir: pirgrp {
    fsl,pins = <
        MX6UL_PAD_GPIO1_IO09__GPIO1_IO09 0x10B0
    >;
};
```

### 驱动改造

```text
platform_driver
of_match_table
probe/remove
devm_gpiod_get
devm_request_threaded_irq
```

PIR：

```text
threaded IRQ
顶半部记录/唤醒
线程半部读 GPIO、消抖、wake_up
```

已知要修的 bug（`docs/b-dts-plan.md` 第 5 节）：

```text
dht22_exit() 没 unregister/free/gpio_free
→ rmmod 后 GPIO 一直被占，第二次 insmod 失败
→ 改 devm_* 时一并解决
```

验收：

```text
不需要 gpio_num
dmesg 有 probe
DHT22 读数正常
PIR poll/read 正常
rmmod/insmod 不 oops
```

### 可选

```text
IIO scale
IIO buffer
Buildroot 集成
```

---

## 9. 项目 A 可选进阶

优先度低于 B：

```text
0x27 SecurityAccess
0x2E WriteDataByIdentifier
0x31 RoutineControl
```

可靠性：

```text
50 次 OTA
24h/72h
```

不是投递必需，不要写没做过的数字。

---

## 10. 文档和博客

已发布：

```text
00 ~ 08
```

本地稿：

```text
docs/blog/09-imx6ull-drivers.md
docs/blog/10-test-report.md
```

待办：

```text
CSDN 发布 09/10
更新 00 总览目录
更新 README 已发布列表
简历 CSDN 行改成 11 篇
```

简历：

```text
D:\嵌入式学习\嵌入式软件开发\林祖龙_嵌入式实习简历_v2.docx
```

---

## 11. 新会话开场建议

把本文件发给新会话，然后说：

```text
先读 PROJECT_HANDOFF.md，继续项目 B：
先确认板子 DTS，再决定改 DTS 还是先做 platform driver。
不要重复讲背景。
```

---

## 12. 关键原则

```text
没做过的数字不写
没测过的功能不写“通过”
先看原始日志，再改代码
CAN 先查采样点/ACK，再查协议
驱动先确认 probe/pinctrl，再查时序
```
