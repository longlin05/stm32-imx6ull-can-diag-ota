# 项目 B：设备树确认结果与路线决定

> 记录时间：2026-11-07 23:10 前后
> 状态：**已确认板级 DTS 身份；未改任何 DTS、未改驱动代码**
> 下一轮从本文「待执行」开始，不要重复调查

---

## 1. 已确认（板子实测，SSH 只读）

板子在线时的实测输出：

```bash
uname -a
# Linux ATK-IMX6U 4.1.15-g3dc0a4b #1 SMP PREEMPT Thu Aug 18 09:27:40 CST 2022 armv7l

cat /proc/device-tree/compatible
# fsl,imx6ull-14x14-evk fsl,imx6ull

cat /proc/device-tree/model
# Freescale i.MX6 ULL 14x14 EVK Board

cat /proc/cmdline
# console=ttymxc0,115200 root=/dev/mmcblk1p2 rootwait rw
```

`/proc/device-tree` 顶层节点：

```text
aliases backlight chosen clocks compatible cpus
dht11 ds18b20 gpio_keys@0 interrupt-controller@00a01000 leds
memory model name pxp_v4l2 regulators reserved-memory
sii902x-reset soc sound spi4
```

两个**厂商自定义节点**的属性：

```text
/proc/device-tree/dht11
    compatible   = "alientek,dht11"
    dht11-gpio   = <int cell>
    pinctrl-0    = <phandle>
    pinctrl-names= "default"
    status       = "okay"

/proc/device-tree/ds18b20
    compatible   = "alientek,ds18b20"
    ds18b20-gpio = <int cell>
    pinctrl-0    = <phandle>
    status       = "okay"
```

存储：

```text
mmcblk1 (SD 8G)  p1 vfat 128M = 启动分区（zImage/dtb 在这里）
                 p2 ext3      = rootfs
mmcblk0 (eMMC 32G) p1 vfat / p2 ext3
/proc/mtd 为空 → 不是 NAND 启动，DTB 走 SD/eMMC 的 vfat 分区
/boot 为空
```

## 2. 结论：板子跑的不是原版 EVK DTS

compatible 是 `fsl,imx6ull-14x14-evk`，但 DT 里已经有 `alientek,dht11` / `alientek,ds18b20`、
`pxp_v4l2`、`sii902x-reset`、`gpio_keys@0` 这些正点原子加的节点。

也就是说：**板子的 DTB 是厂商改过的 14x14-evk**，不是 NXP 原版。

虚拟机里的源码树是 `linux-imx-rel_imx_4.1.15_2.1.0_ga`（NXP 原版命名），
而 `docs/blog/09-imx6ull-drivers.md` 记录过：`make kernelrelease` 输出 `4.1.15`，
是手工把 `CONFIG_LOCALVERSION` 改成 `-g3dc0a4b` 才和板子对上的。

→ VM 那棵树是为了拿 `Module.symvers` 另外下的原版树，**不是板子内核的构建树**。
→ 直接用 VM 树的 `imx6ull-14x14-evk.dts` 重编 DTB，会生成**没有 alientek 节点、
没有厂商屏参**的 DTB，刷进去风险很高（显示/启动参数都可能不对）。

## 3. 决定

**走设备树路线，但基底必须是板子自己的 DTB。**

```text
从板子 vfat 启动分区取 imx6ull-14x14-evk.dtb
  → dtc 反编译成 dts
  → 在反编译结果上加 dht22 / pir 节点
  → dtc 重编成 imx6ull-14x14-evk-b.dtb（不改原文件）
  → fw_setenv fdt_file 切换，出错只改回 env 就能回退
```

驱动同时改成 platform driver：

```text
of_match_table + devm_gpiod_get + devm_request_threaded_irq
probe/remove 成对
module_param 保留为回退路径（没有 DT 节点时仍能 insmod 测试）
```

两条线的先后：**先拿 DTB、确认 fdt_file 与 dht11 占用的 GPIO，再改 DTS，最后改驱动**，
因为节点属性名和 pin 冲突要看反编译结果才能定。

## 4. 待执行（下一轮照着做）

### 4.1 板子侧取证

```bash
ssh root@192.168.137.103

ls -l /run/media/mmcblk1p1/            # 确认真实的 dtb 文件名和 zImage
which fw_printenv && fw_printenv fdt_file
xxd /proc/device-tree/dht11/dht11-gpio     # 厂商 dht11 用的哪个 GPIO
xxd /proc/device-tree/ds18b20/ds18b20-gpio
ls /sys/bus/platform/devices | grep -i -E "dht|ds18"   # 厂商驱动是否已在跑（会抢 GPIO）
dmesg | grep -i -E "dht11|ds18b20|alientek"
```

重点：**如果厂商 dht11 驱动已经占了 GPIO1_IO04，我们的 DHT22 申请会失败**，
要么换引脚，要么在 DTS 里把那个节点 disable。

### 4.2 取 DTB 反编译

```bash
# Windows 侧
scp root@192.168.137.103:/run/media/mmcblk1p1/imx6ull-14x14-evk.dtb .
# WSL 里（dtc 已装好在 ~/tools，不需要 root）
export LD_LIBRARY_PATH=$HOME/tools/dtc_root/usr/lib/x86_64-linux-gnu
~/tools/dtc_root/usr/bin/dtc -I dtb -O dts -o board.dts imx6ull-14x14-evk.dtb
```

WSL 里的 dtc 是 1.8.1，从 Ubuntu 的 deb 解包到 `~/tools/dtc_root`，没有装系统包：

```text
~/tools/dtc_root/usr/bin/dtc
~/tools/dtc_root/usr/bin/fdtget / fdtput
需要 export LD_LIBRARY_PATH=$HOME/tools/dtc_root/usr/lib/x86_64-linux-gnu
```

### 4.3 加节点

反编译结果里已经有 `pinctrl` 节点，pin 配置值要和厂商已有 nibble 设置对齐，
不要照抄 handoff 里 0x10B0 的草案就完事。

### 4.4 刷入与回退

```text
1. 备份：cp imx6ull-14x14-evk.dtb imx6ull-14x14-evk.dtb.bak（在 p1 上）
2. 新文件用不同名字：imx6ull-14x14-evk-b.dtb
3. fw_setenv fdt_file imx6ull-14x14-evk-b.dtb && reboot
4. 起不来 → 串口进 U-Boot，setenv fdt_file imx6ull-14x14-evk.dtb; boot
```

## 5. 顺带发现的驱动问题（本轮只记录，没改）

`drivers/dht22_iio/dht22_iio.c` 的 `dht22_exit()`：

```c
static void __exit dht22_exit(void)
{
    struct iio_dev *indio_dev = iio_device_alloc(0);   /* 无意义，还漏了 */
    (void)indio_dev;
    pr_info("dht22: unloaded\n");
}
```

没有 `iio_device_unregister()`、没有 `iio_device_free()`、没有 `gpio_free()`。
所以 rmmod 之后 GPIO 仍被占用，`insmod` 第二次会 `gpio_request` 失败，
和 handoff 里「rmmod/insmod 不 oops」的验收项直接冲突。
改 platform driver 时用 `devm_*` 一起解决。

`drivers/pir_gpio/pir_gpio.c` 的 `pir_exit()` 是成对的（del_timer_sync / free_irq），没问题。

---

## 6. 本轮没做

```text
没有改任何 DTS
没有改任何驱动代码
没有编译
没有刷机
```

原因：板子和 VMware 虚拟机都不可用（板子断电、USB 网卡从 Windows 掉线）。
