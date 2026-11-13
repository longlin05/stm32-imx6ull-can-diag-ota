# 项目掌握清单（逐条自检）

> 用法：每条能不看资料讲 60 秒，就算过。讲不出来的标记出来，面试前一天只看标记项。
> 格式：【会答】【知识点】【项目证据】【追问】。

---

# 一、项目 A：CAN 诊断与 OTA

## 1. CAN 位定时怎么配？

【会答】CLK 42 MHz / 预分频 6 = 7 MHz 的 tq 时钟；一个位 14 个 tq，所以 7M/14 = 500 kbps；采样点 = (1+BS1)/总 tq。

【知识点】
```text
tq = 1 / (f_can / prescaler)
位时间 = SYNC(1) + BS1 + BS2
sample point = (1 + BS1) / (1 + BS1 + BS2)
STM32F407 的 CAN 挂在 APB1 = HCLK/4 = 42 MHz（HCLK 168 MHz）
```

【项目证据】`Src/can.c`：`Prescaler = 6, TimeSeg1 = CAN_BS1_11TQ, TimeSeg2 = CAN_BS2_2TQ, SJW = 1TQ`
→ 14 tq，`(1+11)/14 ≈ 85.7%`。

【已实测核实 2026-11-13】

```text
历史固件（11 月初测试那版）  采样点按当时配置是 0.75
当前源码（Src/can.c + .ioc） PSC 6 / BS1 11 / BS2 2 → (1+11)/14 = 85.7%
两边刷成当前固件后实测：
    can0 sample-point 0.866（实际 0.850）  10 次 read-did → 8 ok / 2 timeout
    can0 sample-point 0.750                 10 次 read-did → 9 ok / 1 timeout
结论：这块板子 + 这条短线，两种采样点都能通，容差足够
```

面试口径（推荐）：

```text
"两侧采样点要匹配；我实测过 0.75 和 0.85 在这条总线上都能通，
 当时那次恢复里真正起作用的是 down/up 重置，把 bus-off 和错误计数清了。"
```

不要再说“必须设 0.75 才通”——现在的数据不支持这个因果。

【追问】采样点为什么重要？→ 收发双方要在同一位内、同一时刻采样；偏差过大时接收方采到边沿，误码 → ACK error。

## 2. CAN 为什么能仲裁？

【会答】显性 0 覆盖隐性 1，节点边发边听，发 1 读到 0 就退出，ID 小的赢。

【知识点】仲裁只发生在仲裁段；仲裁失败不影响数据；有优先级反转风险（低优先级报文可能饿死）。

【项目证据】0x111/0x112/0x113 是传感器帧，0x7E0/0x7E8 是诊断帧，诊断优先级更高（ID 小）。

【追问】CAN 有几种帧？→ 数据帧、远程帧、错误帧、过载帧。

## 3. CAN 错误状态和 bus-off 怎么恢复？

【会答】Error Active（TEC/REC < 128）→ Error Passive（> 127）→ Bus Off（TEC > 255）；bus-off 后要恢复流程。

【知识点】
```text
TEC 发送错误计数、REC 接收错误计数
bus-off 期间节点不再参与总线
恢复：硬件自动（STM32 AutoBusOff）或软件重新初始化
Linux：ip link set can0 down/up，或 restart-ms 自动重启
```

【项目证据】`AutoBusOff = ENABLE`；板上 `restart-ms 100`；测试过 bus-off 恢复后 `can0 ERROR-ACTIVE`、`gwctl status online:true`。

## 4. 报文 ID 是怎么分配的？

【会答】按“优先级+源+类型”编码，传感器帧和外设命令帧分开，诊断用标准 UDS 的 0x7E0/0x7E8。

【知识点】`CAN_ID_MAKE(prio, src, type)`；ID 越小优先级越高。

【项目证据】
```text
0x111/0x112/0x113  STM32 → Linux（PIR/环境/心跳）
0x221/0x222        Linux → STM32（配置/查询）
0x228              STM32 → Linux（ACK）
0x7E0/0x7E8        诊断请求/响应
```

【追问】为什么不用 29 位扩展帧？→ 11 位够用，且 F407 的过滤器和代码更简单。

## 5. ISO-TP 四种帧怎么区分？

【会答】看首字节高 4 位（PCI）：0=单帧，1=首帧，2=连续帧，3=流控。

【知识点】
```text
SF: 0x0L + L 字节数据（L ≤ 7）
FF: 0x1L + 长度高 8 位（12 位总长，最大 4095）
CF: 0x2N + 数据（N 是序号，0-15 循环，首帧后从 1 开始）
FC: 0x3S + BS + STmin（S=0 继续，1 等待，2 溢出）
```

【项目证据】`isotp.c` 里按 `frame[0] >> 4` 分派。

【追问】为什么连续帧序号要循环？→ 8 字节帧里只有 4 位放序号，最大 15，超过就回绕。

## 6. 流控参数 BS 和 STmin 是什么？

【会答】BS = 发送方每发多少个 CF 就要等一次 FC；STmin = 两个 CF 之间的最小间隔。

【知识点】BS=0 表示不需要再等 FC；STmin 0x00-0x7F 是毫秒，0xF1-0xF9 是 100-900 微秒。

【项目证据】默认 BS=8、STmin=5 ms（`isotp_port.h`）；注释里写了 STmin 必须大于任务周期（1 ms），否则会被强制成 0。

【追问】STmin 设太小会怎样？→ 接收方来不及处理，溢出丢帧；设太大则传输慢。

## 7. 4095 字节多帧你踩过什么坑？

【会答】发送失败还推进序号，导致后面全错位。

【知识点】发送 `tx_frame` 返回非 0 时，要回滚 pos/seq/state/block/last_cf 四个状态。

【项目证据】交接文档 5.3 记录了这个 bug 和修复。

【追问】为什么最大是 4095？→ FF 里只有 12 位表示长度。

## 8. UDS 会话和 S3 超时怎么处理？

【会答】默认 01 / 扩展 03 / 编程 02；扩展会话 5 秒没请求就回默认。

【知识点】S3 = 5 s；P2 = 50 ms（收到请求到开始响应），P2* = 5000 ms（0x78 之后）。

【项目证据】`UDS_S3_TIMEOUT_MS 5000`，`UdsServer_Tick()` 在 1 ms 任务里查；超时还会锁安全访问。

【追问】TesterPresent 的作用？→ 保活，重置 S3；子功能 0x80 表示不需要响应。

## 9. 0x22 读 DID 怎么实现的？

【会答】按 DID 查表返回数据，长度不够回 0x13，未知 DID 回 0x31。

【知识点】响应格式 `62 + DID(2) + data`；VIN 17 字节会走多帧，正好验证 FF/CF。

【项目证据】DID：F190 VIN、F180 版本、F1B0 当前槽、F1A0 PIR 开关、F1A1/A2 周期。

## 10. DTC 怎么设计？

【会答】5 类 DTC，各有触发和恢复条件，状态机 + 掉电保存。

【知识点】状态位（testFailed / confirmed / pending）；掉电保存要写 Flash 扇区。

【项目证据】`0xC001` DHT11 超时/校验错，连续失败 3 次置位，连续成功 5 次恢复；写 sector 4。

【追问】Flash 磨损怎么办？→ 分扇区轮换 + 计数，或加 EEPROM 模拟层。

## 11. 0x19 / 0x14 怎么用？

【会答】`19 02 FF` 读全部 DTC，`19 0A` 读支持的 DTC，`14 FF FF FF` 清除。

【知识点】响应 `59 02 + mask + [DTC(3)+status(1)]*`；DTC 用 3 字节编码。

【项目证据】`uds_cli can0 read-dtc` 实测返回 `59 02 FF C0 01 00 0D`。

## 12. 0x27 安全访问完整流程？（重点，新增）

【会答】扩展会话 → `27 01` 拿 seed → 算 key → `27 02` 解锁 → 之后才能 0x2E。

【知识点】
```text
seed 全 0 = 已解锁
一次 seed 只能试一次 key（失败清 seed_valid，必须重新 27 01）
错 3 次锁 10 s（NRC 0x36），锁定期内任何 key 都回 0x37
切会话 / S3 超时 → 自动上锁
默认会话里请求 0x27 → 0x7F 0x27 0x7F
```

【项目证据】实测：
```text
27 01           → 67 01 5D C2 FC C4
27 02 DEADBEEF  → 7F 27 35
27 01 / 27 02 <key> → 67 02
未解锁 2E       → 7F 2E 33
三次错 key      → 35 / 35 / 36，之后 37
```

【追问】seed 怎么生成才安全？→ 本项目用 tick+槽位混算，只是演示；量产要 HSM + 产线一车一密。

## 13. 0x2E 写 DID 要注意什么？

【会答】必须先解锁，只允许写配置类 DID，长度和取值范围都要校验。

【知识点】响应 `6E + DID(2)`；长度错 0x13，DID 不可写 0x31，未解锁 0x33。

【项目证据】可写 F1A0（1 字节）、F1A1/F1A2（2 字节，**小端**，和 0x22 读出来一致）；写 F190 返回 0x31。

【追问】写进去掉电会丢吗？→ 当前只存 RAM，量产能要写 NVM 并加 CRC。

## 14. NRC 有哪些高频的？

【会答】0x11 服务不支持、0x12 子功能不支持、0x13 长度错、0x22 条件不满足、0x24 顺序错、0x31 超范围、0x33 未解锁、0x35 密钥错、0x36 超次数、0x37 延迟未到、0x7F 会话不支持。

【追问】0x78 是什么？→ responsePending，告诉测试端别超时。

## 15. Bootloader 分区怎么划？

【会答】Bootloader + App A(0x08020000) + App B(0x08060000)，每槽 256 KB，另有 Metadata 区。

【知识点】App Header 512 字节（magic/version/length/crc32/entry）；向量表要在 App 起始地址。

【项目证据】`VTOR = 0x08020200`（App A）；`app_a.img` 75048 字节 = 512 头 + 74536。

## 16. 跳转前要做什么？

【会答】关中断、关 SysTick、检查栈顶和复位向量合法、设 VTOR、设 MSP、跳到复位向量。

【知识点】App 的向量表第一个字是初始 SP，第二个字是 Reset_Handler。

【项目证据】`boot_vector_validate()` 检查 SP 在 RAM 范围、Reset 在槽内。

## 17. trial / rollback / confirm 怎么工作？

【会答】OTA 写完写元数据标记 trial；启动时试新槽，App 起来后自己 confirm；没 confirm 就回滚旧槽。

【知识点】双 Metadata 冗余，一份坏了用另一份；confirm 要写"已确认"标记。

【项目证据】`Boot_StartTrial()` 在 0x37 成功后调用；App 启动时 `Boot_AppConfirm(0/1)`。

【追问】怎么防"砖"？→ Bootloader 自己不能被覆盖，元数据双份，App 校验失败就不跳。

## 18. OTA 0x34/0x36/0x37 流程？

【会答】0x34 请求下载（地址+长度）→ 0x36 传数据（块序号）→ 0x37 退出并校验。

【知识点】0x34 响应 `74 + maxBlockLength(2)`；0x36 请求带 blockSequenceCounter；0x37 后要看 CRC 和向量表。

【项目证据】块大小 1024；75048 字节 ≈ 74 个块；实测 3 次 A↔B 成功。

## 19. OTA 和周期 DTC 抢总线怎么定位的？

【会答】抓包看到 gatewayd 每 5 s 的 `19 02 FF` 插在 0x36 中间；换成 gatewayd 内部 OTA（单线程）后消失。

【知识点】同一总线上的两个进程各自发诊断请求，会互相打断 ISO-TP 会话。

【项目证据】
```text
uds_cli ota（独立进程）→ 0x36 failed at offset 1024
gwctl ota（走 gatewayd）→ 一次通过，没停 gatewayd
原因：gatewayd 主循环 poll → serve_client（OTA 同步跑）→ periodic_poll 串行
```

【追问】那为什么最初没发现？→ 手动 insmod 式的操作习惯掩盖了并发问题。

## 20. gatewayd 的架构？

【会答】单进程单线程：SocketCAN 收发 + Unix socket 控制通道 + SQLite 落库 + MQTT 发布。

【知识点】poll 等待 socket 事件；周期任务靠时间戳判断，不靠定时器线程。

【项目证据】`poll(&pfd,1,500)`；`serve_client()` 同步处理请求；`next_poll` 控制轮询。

## 21. 性能数据怎么来的？

【会答】1000 次命令压测：P99 22.06 ms、0 重试；15 分钟 soak：行数 3014→3182、CPU 0%、RSS 2092 KB、PID 不变。

【追问】P99 怎么算的？→ 每次命令记时延，排序取第 99 百分位。

---

# 二、项目 B：i.MX6ULL 驱动

## 22. 设备树是什么？驱动怎么匹配？

【会答】描述硬件的数据结构；驱动用 `compatible` 匹配节点，probe 时用 of_* / devm_* 取资源。

【知识点】DTB 由 dtc 编译；内核启动展开；`of_device_id` 表 + `MODULE_DEVICE_TABLE`。

【项目证据】`compatible = "zulong,dht22"` ↔ `dht22_of_match[]`。

## 23. 为什么不能直接改源码树的 dts？

【会答】板子 DTB 是厂商改过的 EVK（有 alientek 节点），手头源码树是 NXP 原版，重编会丢厂商节点和屏参。

【知识点】判断依据：`compatible` 是 EVK，但 `/proc/device-tree` 里有 `dht11/ds18b20/pxp_v4l2`。

【追问】那怎么改？→ 从板子取原 DTB → dtc 反编译 → 加节点 → 重编，并做两道自检。

## 24. 怎么保证改 DTB 不出事？

【会答】两道自检：原 dts 编回去再反编译必须逐字一致；打补丁后 diff 只允许新增。

【项目证据】本次新增 26 行、删除 0 行，39459 → 39854 字节；板上留 `.dtb.bak`，仓库存原版。

## 25. pin 号怎么确定？

【会答】反编译出来的 dts 里 pin 是数字，用 `pinmux-pins` 反推：`pin_id = 4 × pinctrl 编号`。

【知识点】用三组交叉验证：gpio-leds(0x68→pin26)、pwm1grp(0x7c→pin31)、uart1grp(0x84→pin33)。

【项目证据】GPIO1_IO04 = `0x6c / 0x2f8`，GPIO1_IO09 = `0x80 / 0x30c`，pad 配置 `0x17059`。

## 26. pinctrl 是谁应用的？

【会答】驱动核心在 probe 前调 `pinctrl_bind_pins()`，按 `pinctrl-0` 应用，驱动代码不用管。

【项目证据】加载前 `MUX UNCLAIMED`，加载后 `dht22 ... group zl_dht22grp`。

## 27. platform driver 和 devm 解决什么？

【会答】platform bus 挂不挂物理总线的设备；devm 把资源绑到 device 生命周期，自动释放。

【知识点】probe/remove 成对；`devm_gpiod_get`、`devm_input_allocate_device`、`devm_request_threaded_irq`。

【项目证据】旧版 `dht22_exit()` 没注销，rmmod 后 GPIO 被占、二次 insmod 必失败；devm 一次解决。

## 28. 没有设备树怎么办？

【会答】`platform_device_alloc` + `platform_device_add_data` 传 platform_data，probe 里用 `dev_get_platdata`。

【项目证据】保留 `gpio_num` 模块参数作为回退路径（未在板上实测）。

## 29. IIO 的 raw / input / scale 怎么来的？

【会答】由 `info_mask` 决定：RAW→`_raw`，PROCESSED→`_input`，SCALE→`_scale`。

【项目证据】一开始只写 RAW，`in_temp_input` 根本不存在；改成三路都开。

【追问】为什么值不用换算？→ 值本身是 0.001 单位，scale = 1。

## 30. DHT22 时序怎么读？

【会答】拉低 ≥1 ms 起始 → 释放 → 80 us 低 + 80 us 高应答 → 40 bit，每位 50 us 低 + 26-28 us（0）/70 us（1）高，40 us 处采样。

【知识点】时序要求严，读的时候 `local_irq_save`；40 位后校验和。

【项目证据】`dht22_wait_level` + `udelay(40)`；实测 19.6 °C / 50.5 %RH。

## 31. 为什么没做 IIO buffer？

【会答】板子内核没编 `CONFIG_IIO_BUFFER/TRIGGER`，只有 direct mode；重编厂商内核风险太大。

【项目证据】`zcat /proc/config.gz | grep IIO` → 两行 `not set`。

【追问】那怎么体现“会用 IIO”？→ 说清 direct mode 的限制，以及要 buffer 需要什么配置。

## 32. 为什么 PIR 改成 input？

【会答】PIR 是事件不是读数；input 有标准事件流，evtest/udev/libinput 都能直接用。

【知识点】EV_KEY 按下/抬起成对；`input_sync()` 提交；Handlers=kbd 是因为上报 EV_KEY。

【项目证据】`Name="pir"`，`Handlers=kbd event2`，KEY_PROG1 = 148。

## 33. threaded IRQ 为什么需要？

【会答】硬中断不能睡，消抖要 `msleep`，所以硬中断只 `IRQ_WAKE_THREAD`，处理放线程。

【知识点】必须带 `IRQF_ONESHOT`，否则中断会重入。

## 34. 单边沿 + 状态机有什么坑？

【会答】只申请 RISING 时 `pressed` 永远清不掉，事件只报一次；要双沿，下降沿只清 level。

【项目证据】改之前 60 s 里靠抖动偶然报几次；改之后 90 s 稳定 7 对。

## 35. 消抖和 holdoff 分别管什么？

【会答】消抖 = 边沿后等 200 ms 再采电平；holdoff = 两次事件至少间隔 3 s。

【项目证据】实测按下时长 1.04 s（与传感器输出宽度一致）、最小间隔 3.12 s（> 3 s）。

## 36. 模块怎么开机自动加载？

【会答】本板是 Yocto + sysvinit：`.ko` 放 `/lib/modules/$(uname -r)/extra/`，`depmod -a`，模块名写 `/etc/modules`，`S05modutils.sh` 开机 `modprobe`。

【追问】为什么不用 rc.local？→ 没依赖处理、失败静默、参数要拼命令行。

## 37. 交叉编译模块要匹配什么？

【会答】内核树 + 工具链 + vermagic + Module.symvers 的 CRC。

【知识点】`make -C $KDIR M=$PWD ARCH=arm CROSS_COMPILE=... modules`；`CONFIG_MODVERSIONS=y` 需要符号 CRC。

【项目证据】`vermagic = 4.1.15-g3dc0a4b SMP preempt mod_unload modversions ARMv7`。

## 38. 引脚冲突怎么发现的？

【会答】开机自动加载后 probe 顺序变了，DHT22 先占了 GPIO1_IO04，ov5640 的 `csi_pwn_rstgrp` 申请失败。

【知识点】`MUX UNCLAIMED` 只表示此刻没人持有，要反查 DTS 里所有 `*-gpios` 引用。

【项目证据】`could not request pin 27 ... csi_pwn_rstgrp`；GPIO1 全组归属表。

## 39. 设备树和驱动的关系，用一句话说清？

【会答】设备树描述“硬件是什么、接在哪”，驱动描述“怎么操作”，两者靠 compatible 关联；换驱动实现时设备树可以不动。

【项目证据】PIR 从 misc 改成 input，复用 `zulong,pir` 这个 compatible，没重刷 DTB。

---

# 三、讲述与行为

## 40. 两个项目各用一句话？

```text
A：STM32 + i.MX6ULL 双板，实现车载风格的 CAN 诊断（ISO-TP/UDS/安全访问）、
   A/B Bootloader 与 OTA 升级，以及 Linux 网关的落库与上报。
B：给 i.MX6ULL 写传感器内核驱动，从"模块参数传 GPIO"改造成设备树 + platform driver，
   PIR 接入 input 子系统，并做成开机自动加载。
```

## 41. 最难的点是什么？（每题都要能举日志）

```text
采样点不一致      → ACK error / no free mailbox
ISO-TP 序号推进   → 多帧错位
OTA 与轮询抢总线  → 0x36 在 1024 字节处失败
引脚冲突          → 自动加载后 ov5640 probe 失败
IIO buffer 不存在 → 查 config 及时换方案
```

## 42. 哪些没做？（诚实清单）

```text
24h / 72h 长期稳定性、50 次 OTA
签名验签、差分升级、断点续传
配置项写 NVM（当前只存 RAM）
PIR pins 与触摸屏共用的潜在冲突未解决
```

## 43. 现场可能要画的三张图

```text
1. CAN 标准帧结构（SOF/仲裁/控制/数据/CRC/ACK/EOF）
2. ISO-TP 多帧时序（FF → FC → CF*N → 完成）
3. A/B 切换流程（OTA 写 B → trial 启动 → confirm / rollback）
```

---

# 自检记录

| # | 能讲 60 秒 | 备注 |
|---|---|---|
| 1–5 | ☐ | |
| 6–10 | ☐ | |
| 11–15 | ☐ | |
| 16–21 | ☐ | |
| 22–28 | ☐ | |
| 29–35 | ☐ | |
| 36–39 | ☐ | |
| 40–43 | ☐ | |
