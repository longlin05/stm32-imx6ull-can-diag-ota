# 项目 B 面试题库（24 题）

> 覆盖：设备树、platform driver、pinctrl、IIO、input、threaded IRQ、模块自动加载。
> 用法同 A：先自己答，再对照要点；能画图、能举本项目的日志。

---

## 一、设备树（1–7）

### 1. 设备树是干什么的？

```text
用数据结构描述硬件，让一份内核镜像适配不同板子
内核启动时展开 DTB，生成 device tree 节点
驱动通过 compatible 匹配节点，用 of_* 接口取资源
```

要点：**描述硬件，不描述驱动行为**。

### 2. compatible 是什么？怎么匹配？

```text
节点里的字符串，格式 vendor,device
驱动 of_device_id 表里写同样的字符串
内核按 compatible 匹配 -> 创建 platform_device -> 调 probe
```

本项目：

```dts
dht22 { compatible = "zulong,dht22"; ... };
static const struct of_device_id dht22_of_match[] = {
    { .compatible = "zulong,dht22" }, {}
};
```

### 3. 你们板子的 DTB 是怎么来的？

```text
板子 compatible 是 fsl,imx6ull-14x14-evk，但 DT 里有 alientek,dht11 /
ds18b20 / pxp_v4l2 这些厂商节点 —— 是厂商改过的 EVK
手头内核树是 NXP 原版，重编会丢厂商节点
所以从板子启动分区取原 DTB，dtc 反编译 → 加节点 → 重编
```

追问：**怎么保证没改坏？** 两道自检：往返无损、diff 只允许新增（本次 26 行）。

### 4. pin 号怎么确定？

```text
反编译出来的 dts 里 pin 配置是数字（宏已展开）
用 /sys/kernel/debug/pinctrl/.../pinmux-pins 反推：
  pin_id = 4 × pinctrl 内部编号
交叉验证三组：gpio-leds(0x68→pin26)、pwm1grp(0x7c→pin31)、uart1grp(0x84→pin33)
→ GPIO1_IO04 = 0x6c / mux 0x2f8，GPIO1_IO09 = 0x80 / mux 0x30c
```

### 5. pinctrl 是谁应用？什么时候应用？

```text
驱动核心在 probe 之前调 pinctrl_bind_pins()，按 pinctrl-0 应用
驱动里不用写任何 pinctrl 代码
验证：
  加载前 pinmux-pins 显示 MUX UNCLAIMED
  加载后显示 dht22 ... group zl_dht22grp
```

### 6. 只查 MUX UNCLAIMED 够不够？

**不够**，本项目踩过：

```text
DHT22 选 GPIO1_IO04 时只看了 pinmux 显示 UNCLAIMED
但 DTS 里 ov5640 的 pwn-gpios 也是这个脚
手动 insmod 时没冲突（摄像头先跑完），开机自动加载后顺序变了 -> 抢脚
正确做法：运行时 pinmux + 静态反查 DTS 里所有 *-gpios 引用
```

### 7. 设备树里加节点要不要重新刷 DTB？

```text
要，但可以做到很安全：
  同名覆盖 + 原文件留 .dtb.bak + 仓库存一份原版
  没有 fw_printenv 时没法切 fdt_file，只能同名覆盖
本项目后来复用已有 compatible，改成 input 驱动时就不用动 DTB 了
```

---

## 二、platform driver（8–12）

### 8. platform bus 是什么？

```text
虚拟总线，用来挂"不挂在物理总线上"的设备
设备来自设备树（或板级代码），驱动 platform_driver
匹配靠 compatible 或 name
```

### 9. probe 里一般做什么？

```text
取资源（reg/irq/gpio/clk）→ 初始化硬件 → 注册子系统设备
失败要能干净回滚
用 devm_* 让内核自动回收
```

本项目 probe：

```c
devm_gpiod_get(&pdev->dev, "data", GPIOD_IN)
devm_input_allocate_device / devm_iio_device_alloc
devm_request_threaded_irq
```

### 10. devm 解决了什么问题？

```text
把资源绑到 device 生命周期，remove 或 probe 失败时自动释放
避免 "申请了没释放" 的设备泄漏
本项目 dht22 旧版 exit() 没 unregister/free/gpio_free，
rmmod 后 GPIO 一直被占，第二次 insmod 必失败 —— devm 一次性解决
```

### 11. 没有设备树怎么用？

```text
module_init 里 platform_device_alloc + platform_device_add，用 platform_data 传参
本项目保留了这条回退路径：没有 DT 节点时用 gpio_num 模块参数
（没在板上实测，属于兼容保底）
```

### 12. remove 里要注意什么？

```text
先停中断/定时器，再注销子设备，最后释放 gpio
顺序反了可能中断里访问已释放内存
devm 会自动做，但要保证自己开的（workqueue/timer）手工停
```

---

## 三、中断与并发（13–16）

### 13. 为什么用 threaded IRQ？

```text
顶半部要短，不能睡
我们的消抖要 msleep(200)，只能在进程上下文
devm_request_threaded_irq(..., hardirq, thread, IRQF_ONESHOT, ...)
硬中断只 return IRQ_WAKE_THREAD
```

### 14. IRQF_ONESHOT 有什么用？

```text
线程化中断必须带 ONESHOT：等线程处理完再解掩码
否则同一条中断会不断重入，把系统拖死
代价：处理期间会丢边沿（对 PIR 这种慢信号可接受）
```

### 15. 单边沿中断 + 电平状态机有什么坑？

**踩过**：

```text
只申请 RISING，thread 里 if (value && !pressed) 报按下
第一次之后 pressed 一直为 1 —— 因为再也不会收到下降沿去清它
表面现象：事件只报一次，偶尔又能报（信号抖动碰巧清掉了）
修法：RISING | FALLING，下降沿只负责把 level 清零
```

### 16. 用户态读值和中断里的数据怎么同步？

```text
PIR：事件标志 + waitqueue，read/poll 等待，中断线程 wake_up
DHT22：mutex 保护采样状态，sysfs 读的时候加锁
注意：中断上下文不能拿 mutex
```

---

## 四、IIO（17–19）

### 17. IIO 是什么？什么时候用它？

```text
Industrial I/O，专给 ADC/传感器这类"多通道数值"设备
用户态统一接口：/sys/bus/iio/devices/iio:deviceN/in_*_{raw,input,scale}
适合：温度、湿度、加速度、光强
不适合：需要自定义语义的事件（比如 PIR -> input）
```

### 18. `_raw` 和 `_input` 有什么区别？

```text
由 info_mask 决定：
  IIO_CHAN_INFO_RAW        -> in_temp_raw
  IIO_CHAN_INFO_PROCESSED  -> in_temp_input（内核算好的物理量）
  IIO_CHAN_INFO_SCALE      -> in_temp_scale
本项目三个都开，raw 和 input 值相同（单位就是 0.001），scale = 1
```

**这是踩过的坑**：只写 RAW 时 `in_temp_input` 根本不存在，文档和实现对不上。

### 19. 板子能不能做 IIO buffer？

```text
不行，查 config：
  # CONFIG_IIO_BUFFER is not set
  # CONFIG_IIO_TRIGGER is not set
只有 direct mode
要不要重编内核？不值得：手头是原版树，刷坏风险大
→ 先查 config 再定方案，这一步省了半天
```

---

## 五、input 子系统（20–22）

### 20. 为什么把 PIR 从字符设备改成 input？

```text
字符设备：自己定义 /dev/pir，read 返回 "1\n"，用户态要写专门的解析
input：标准事件流 /dev/input/eventX，evtest/udev/libinput 都能直接用
本质：PIR 是"事件"，不是"读数"
```

### 21. EV_KEY 上报要注意什么？

```text
按下/抬起必须成对：input_report_key(1) + input_sync()，之后 (0) + input_sync()
用 pressed 状态机保证：holdoff 压掉的按下不会产生孤儿抬起
Handlers=kbd 是因为上报 EV_KEY，内核按键盘类挂了 kbd handler，不影响 evtest
```

### 22. 实测数据怎么证明驱动是对的？

```text
7 对事件，按下到抬起 1.04 s（和 HC-SR501 输出宽度一致）
按下次间隔 3.12~14.29 s，最小值 ≥ holdoff 3 s
→ 时长说明不是凭空造事件，间隔说明 holdoff 真在起作用
```

---

## 六、模块加载与工程化（23–24）

### 23. 模块怎么开机自动加载？

```text
本板 rootfs 是 Yocto + sysvinit，没有 systemd/包管理器
/etc/init.d/modutils.sh 读 /etc/modules，逐行 modprobe
所以：
  .ko 放 /lib/modules/$(uname -r)/extra/
  depmod -a
  /etc/modules 写模块名（不是路径）
```

追问：**为什么不写 rc.local？** 没有依赖处理、失败静默、参数要拼命令行。

### 24. 怎么交叉编译内核模块？

```text
source 工具链环境
make -C $KDIR M=$PWD ARCH=arm CROSS_COMPILE=... modules
KDIR 必须是板子内核对应的树，vermagic 和 Module.symvers 要匹配
CONFIG_MODVERSIONS=y 时还需要 Module.symvers 里有导出符号的 CRC
```

---

## 七、行为/项目追问

### 25. 这个项目最难的点？

```text
不是写驱动，是"先确认环境"：
  IIO buffer 想当然 -> 查 config 发现没编 -> 及时换方向
  引脚只看运行时状态 -> 自动加载后冲突暴露 -> 补查 DTS
每次都是先摸清事实再动手，省下的时间比写代码多
```
