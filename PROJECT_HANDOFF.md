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
b-v0.1   i.MX6ULL 驱动冒烟版（module parameter 传 GPIO）
b-v0.2   device tree + platform driver（2026-11-09 板上实测通过）
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

**2026-11-09 起项目 B 已独立成仓库：**

```text
https://github.com/longlin05/imx6ull-dht22-pir-drivers
本地：D:\嵌入式学习\嵌入式软件开发\imx6ull-dht22-pir-drivers
```

里面包含：

```text
drivers/dht22_iio     IIO 驱动（platform driver + DTS）
drivers/pir_gpio      字符设备（platform driver + threaded IRQ）
dts/                  从板子 DTB 反编译重建的节点与构建脚本
test/  docs/          测试脚本、测试记录、设备树调查记录
```

本仓库（A）的 `b-v0.1` / `b-v0.2` tag 里仍保留当时的驱动代码，
但 HEAD 已经移除了 `drivers/` 和 `dts/`，避免两边分叉。

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

## 6. 项目 B 当前状态：已完成（v0.4，2026-11-13）

**状态：完成。** tag `b-v0.4`，四篇文章已发布，代码/设备树/测试记录齐全。

```text
v0.1  misc 字符设备 + module parameter（09 那篇的状态）
v0.2  platform driver + 设备树（DHT22 IIO / PIR misc）
v0.3  PIR 改成 input 子系统（EV_KEY + evdev）
v0.4  开机自动加载（/lib/modules/extra + depmod + /etc/modules）
```

已知问题（未解决，已记录）：

```text
GPIO1_IO04 与 ov5640 的 pwn-gpios 共用 → 摄像头起不来
GPIO1_IO09 与触摸屏 IRQ 共用 → 触摸若恢复会抢 PIR
恢复方案在 B 仓库 README 第 9 节（DHT22 挪到 GPIO1_IO01）
```

**2026-11-09 完成 platform driver + 设备树改造，实测通过**

```text
DHT22 IIO 驱动     platform_driver + of_match("zulong,dht22") + devm_gpiod_get
PIR 字符设备       platform_driver + of_match("zulong,pir") + threaded IRQ（双沿）
设备树             从板子 DTB 反编译后追加节点重编，已刷入 SD 启动分区
回退路径           没有 DT 节点时仍可用 gpio_num 模块参数
```

实测（原始输出见 B 仓库的 `docs/test-report-b-drivers.md`）：

```text
dht22 dht22: probed, from device tree
pir   pir:   probed, irq=38, initial=1, from device tree
in_temp_input = 19600（19.6°C）  in_humidityrelative_input = 50500（50.5%RH）
pin 27/32 被 dht22/pir 认领（zl_dht22grp / zl_pirgrp）
PIR：read 60s 出 5 次事件；python select() 挥手后 3 次事件
rmmod/insmod 2 轮，dmesg 无 warn/oops
```

v0.1 遗留的两个 bug 已修：

```text
dht22_exit() 不 unregister/free/gpio_free → rmmod 后 GPIO 被占，二次 insmod 必失败
PIR 只申请上升沿 → level 停在 1，后续事件靠抖动偶然复位
```

设备树相关文件在 B 仓库的 `dts/`（`base/` 是板子原版 DTB，`build.sh` 带往返自检）。
刷入方式：同名覆盖 SD 启动分区里的 `imx6ull-14x14-emmc-4.3-480x272-c.dtb`，
备份为同目录 `.dtb.bak`。

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

## 8. 项目 B 设备树 + platform driver（2026-11-09 完成，后续见 §6）

### 目标（已达成）

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

### 板级 DTS 已确认（2026-11-07，详见 B 仓库 `docs/dts-investigation.md`）

```text
compatible = fsl,imx6ull-14x14-evk, fsl,imx6ull
model      = Freescale i.MX6 ULL 14x14 EVK Board
root       = /dev/mmcblk1p2（SD），boot 分区 mmcblk1p1 vfat
DT 里已有 alientek,dht11 / alientek,ds18b20 厂商节点
```

结论：板子 DTB 是**厂商改过的 EVK**，VM 里那棵 `rel_imx_4.1.15_2.1.0_ga` 是原版树，
不能直接拿它重编 DTB。

实际做法（2026-11-09 已完成，和当初草案有出入）：

```text
板子实际跑的是 imx6ull-14x14-emmc-4.3-480x272-c.dtb（fb0 modes = U:480x272p-56）
板子没有 fw_printenv，u-boot env 改不了
→ 只能同名覆盖，原文件在板子上留 .dtb.bak，B 仓库 dts/base/ 也存了一份
反编译后追加节点再重编，diff 只有新增 26 行、没有删除
驱动同步转 platform_driver + of_match，module_param 留作回退
```

已确认 GPIO1_IO04 / GPIO1_IO09 运行时都没被占（pinmux 里 UNCLAIMED），
厂商 dht11/ds18b20 节点用的是 GPIO1_IO01，PWM 背光在 GPIO1_IO08，都不冲突。

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

已知要修的 bug（B 仓库 `docs/dts-investigation.md` 第 5 节）：

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

面试准备（2026-11-13 新增）：

```text
docs/interview/project-a-question-bank.md   48 题（新增 0x27/0x2E 六题）
docs/interview/project-b-question-bank.md   25 题（设备树 / platform / IIO / input / 自动加载）
docs/interview/embedded-fundamentals.md     C / OS / Linux 驱动 / 总线 / 手写代码
docs/interview/project-a-mastery.md         掌握标准 L1–L4（沿用）
```


已发布：

```text
A 系列 00 ~ 10   共 11 篇（10 是测试报告，也是 A 的收尾）
B 系列 00 / 01 / 02 / 03   设备树 + platform driver、PIR 改 input、开机自动加载
合计 15 篇
```

本地稿：

```text
docs/blog/09-imx6ull-drivers.md
docs/blog/10-test-report.md
```

B 系列「i.MX6ULL 驱动笔记」已发布（2026-11-11）：

```text
B00 这个系列写什么
    https://blog.csdn.net/longlin101/article/details/170550740
B01 把 insmod 的 gpio_num 换成设备树
    https://blog.csdn.net/longlin101/article/details/170550878
B02 PIR 为什么不该做成字符设备——换成 input 子系统
    https://blog.csdn.net/longlin101/article/details/170741777
B03 从手动 insmod 到开机自动加载——以及它暴露的引脚冲突
    https://blog.csdn.net/longlin101/article/details/170742009
草稿在 B 仓库 docs/blog/，配图 docs/blog/img/b01-dt-path.png
```

待办：

```text
简历 PDF 需要重新导出（docx 已改到 13 篇，机器上没有 LibreOffice）
CSDN 上 09/10 的头尾链接需要手动改（见本文件下方“CSDN 手动修改项”）
```

CSDN 手动修改项（文章已发布，只能在线编辑）：

```text
09  头部“仓库/tag”两行 → 换成带 B 仓库和“已发布”的版本
09  结尾补“下一篇 → 10”
10  头部“上一篇：09（本地稿，待发布）” → 换成 09 的真实链接
```

已取消：

```text
"A 系列 00 总览目录加 B00/B01" —— B 系列独立，A 的总览只列 A；
两个系列靠文末单向链接衔接（A 的 09 → B00，B 的 b00 → 09），不做双向目录
```

已完成（2026-11-13，项目 B 收尾）：

```text
B 仓库打 tag b-v0.4（platform driver + input + 自动加载）
简历 docx：CSDN 行改成 15 篇（A 00–10 + B 00–03）；驱动 bullet 补 input 子系统和自动加载
简历备份：林祖龙_嵌入式实习简历_v2_bak-20261111.docx
A 仓库 README 补 B02/B03 链接
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
