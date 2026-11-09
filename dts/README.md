# 项目 B 设备树

## 为什么不是"改源码树的 dts"

板子实测：

```text
compatible = fsl,imx6ull-14x14-evk
model      = Freescale i.MX6 ULL 14x14 EVK Board
```

但 DT 里有 `alientek,dht11`、`alientek,ds18b20`、`pxp_v4l2`、`sii902x-reset`
这些正点原子加的节点 —— 板子的 DTB 是**厂商改过的 EVK**。

虚拟机里的 `linux-imx-rel_imx_4.1.15_2.1.0_ga` 是 NXP 原版树（当初为了拿
`Module.symvers` 另外下的，还手工改过 `CONFIG_LOCALVERSION` 才和板子对上）。
用它重编 DTB 会丢掉厂商节点和屏参。

所以这里反过来：**以板子自己的 DTB 为基底**，反编译后追加节点再编回去。

## 文件

```text
base/imx6ull-14x14-emmc-4.3-480x272-c.dtb   从板子 /run/media/mmcblk1p1 原样取出
b-dht22-pir.dtsi                            追加的节点和 pinctrl
build.sh                                    反编译 -> 追加 -> 编译 -> 自检
```

板子实际在跑的是 4.3 寸 480x272 屏的 DTB（`/sys/class/graphics/fb0/modes`
= `U:480x272p-56`），boot 分区里另外 6 个 DTB 是别的屏用的。

## 追加了什么

```dts
dht22: data-gpios = <&gpio1 4 GPIO_ACTIVE_HIGH>;   pinctrl: MX6UL_PAD_GPIO1_IO04__GPIO1_IO04
pir:   irq-gpios  = <&gpio1 9 GPIO_ACTIVE_HIGH>;   pinctrl: MX6UL_PAD_GPIO1_IO09__GPIO1_IO09
```

pin 号不是猜的，按下面这条规则核对过：

```text
反编译出来的 fsl,pins 第一项 pin_id = 4 x pinctrl 内部编号
pin 26 = GPIO1_IO03 -> 0x68（DTS 里 gpio-leds 组）
pin 31 = GPIO1_IO08 -> 0x7c（DTS 里 pwm1grp，运行时被 2080000.pwm 占着，是背光）
pin 27 = GPIO1_IO04 -> 0x6c  <- 我们用这个
pin 32 = GPIO1_IO09 -> 0x80  <- 我们用这个
```

运行时确认这两个脚是空的（`/sys/kernel/debug/pinctrl/20e0000.iomuxc/pinmux-pins`）：

```text
pin 27 (MX6UL_PAD_GPIO1_IO04): (MUX UNCLAIMED) (GPIO UNCLAIMED)
pin 32 (MX6UL_PAD_GPIO1_IO09): (MUX UNCLAIMED) (GPIO UNCLAIMED)
```

厂商自带的 `dht11` / `ds18b20` 节点用的是 GPIO1_IO01（pin_id 0x60），
和这两个脚不冲突，而且没有驱动绑定，所以不用管。

## 生成

```bash
cd dts && ./build.sh
```

`build.sh` 带两道自检：

```text
1. 原 dts 编回去再反编译，必须和原反编译结果逐字节一致（dtc 往返无损）
2. 打补丁后再次反编译，和原结果 diff 必须只有新增行，不能有删除行
```

## 刷入与回退

板子没有 `fw_printenv`，u-boot 环境变量改不了，所以只能**同名覆盖**：

```bash
# 板子上
cp -p /run/media/mmcblk1p1/imx6ull-14x14-emmc-4.3-480x272-c.dtb \
      /run/media/mmcblk1p1/imx6ull-14x14-emmc-4.3-480x272-c.dtb.bak
# PC
scp out/imx6ull-14x14-emmc-4.3-480x272-c.dtb root@192.168.137.103:/run/media/mmcblk1p1/
```

起不来时：把 `dts/base/` 里那份原版 DTB 拷回 SD 卡的启动分区即可
（需要读卡器；`base/` 里的文件和板子上的一模一样）。

## 实测记录（2026-11-09）

```text
上传      imx6ull-14x14-emmc-4.3-480x272-c.dtb  md5 f6f13490a451045eacca7f67084f5033
原文件备份 ...dtb.bak                          md5 71a317052fb1b1164f8cfb47500e9814
重启后 /proc/device-tree/dht22 有 compatible/data-gpios/pinctrl-0
重启后 /proc/device-tree/pir   同上
/sys/bus/platform/devices  出现 dht22 和 pir
/sys/class/graphics/fb0/modes 仍是 U:480x272p-56（屏参没被破坏）
pinmux 里 pin 27 / pin 32 仍 UNCLAIMED（没驱动绑定时本来就不该被占）
```

启动日志里的报错和刷之前一样（`mxsfb ... failed to find mxc display driver`、
`ap3216c ... failed`、`fsl-quadspi ... probe failed`），不是这次改动引入的。

