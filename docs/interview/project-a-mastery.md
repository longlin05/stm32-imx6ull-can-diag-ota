# 项目 A 面试掌握手册

> 目标：把“我做过”变成“我能讲清楚、能现场推导、能回答三层追问”。
> 配套：`project-a-question-bank.md`、`PROJECT_HANDOFF.md`。

---

## 0. 掌握标准

### L1 能讲流程

```text
CAN → ISO-TP → UDS → DTC
Bootloader → OTA
Linux gateway
```

知道每个模块“做什么”。

### L2 能讲实现

能指出代码位置、关键结构、状态机、失败路径。

### L3 能讲为什么

能回答：

```text
为什么这样设计
不用这个会怎样
边界在哪里
```

### L4 能现场推导

能画时序、算位定时、解释 NRC、分析日志、写出关键代码。

面试目标：**核心模块 L3，CAN/ISO-TP/UDS L4，边缘模块 L2。**

---

## 1. 项目介绍

### 30 秒版

> 这个项目基于 STM32F407 和 i.MX6ULL，做了一套车载风格的 CAN 诊断与 OTA。STM32 侧实现 CAN、ISO-TP、UDS、DTC、A/B Bootloader 和 OTA；i.MX6ULL 侧实现 SocketCAN 网关，把诊断结果写 SQLite 并 MQTT 上报。实测 3 次 A↔B OTA、15 分钟稳定运行，定位过 CAN 采样点不一致导致的 ACK error。

### 3 分钟版

按四段讲：

```text
1. 背景：从 CAN 能通，做到可诊断、可升级
2. MCU：ISO-TP/UDS/DTC，A/B Bootloader，OTA
3. Linux：SocketCAN/gatewayd/SQLite/MQTT
4. 数据与故障：3 次 OTA、15min、0.75 采样点
```

### 10 分钟版

在 3 分钟版上补：

```text
Flash 分区
Metadata 双记录
trial/rollback/confirm
0x34/0x36/0x37
UDS 串包
ISO-TP CF 重试
```

---

## 2. 架构

```text
PC / Linux tester
  │  SocketCAN
  ▼
i.MX6ULL gatewayd
  ├── SQLite
  ├── MQTT
  └── Unix socket ← gwctl
  │  CAN 500 kbps
  ▼
STM32F407
  ├── App A / App B
  ├── ISO-TP
  ├── UDS
  ├── DTC
  └── Bootloader
```

启动链：

```text
上电
  → Bootloader
  → 读 Metadata
  → 校验 active / trial
  → 跳 App
  → App 自检
  → Boot_AppConfirm
```

升级链：

```text
gwctl ota
  → gatewayd
  → 0x34 擦非活动槽
  → 0x36 分块写
  → 0x37 校验 + 写 trial
  → 复位
  → Bootloader 试启动
  → 新 App confirm
```

---

## 3. 代码地图

### Bootloader

| 文件 | 作用 |
|---|---|
| `firmware/bootloader/boot_layout.h` | Flash 分区 |
| `boot_loader.c` | 启动策略 |
| `boot_jump.c` | VTOR/MSP/SysTick 跳转 |
| `boot_meta.c` | Metadata 状态机 |
| `boot_meta_store.c` | 双 sector 记录 |
| `boot_image.c` | Header/CRC/向量表校验 |
| `boot_crc32.c` | CRC32 |
| `boot_app.c` | confirm / start trial |
| `ota_server.c` | 0x34/0x36/0x37 |

### STM32 App

| 文件 | 作用 |
|---|---|
| `firmware/app/Core/CustomDrivers/isotp.c` | ISO-TP 核心 |
| `isotp_port.c` | CAN 适配 + UDS 分发 |
| `uds_server.c` | UDS 服务 |
| `dtc.c` | DTC 检测/存储 |
| `can.c` | CAN 收发/环形缓冲 |

### Linux 网关

| 文件 | 作用 |
|---|---|
| `gateway/src/can.c` | SocketCAN |
| `gateway/src/isotp.c` | 平台无关 ISO-TP |
| `gateway/src/uds_client.c` | UDS 请求/响应 |
| `gateway/src/gatewayd.c` | 常驻守护、Unix socket、OTA |
| `gateway/src/gwctl.c` | CLI |
| `gateway/src/gw_log.c` | SQLite |
| `gateway/src/gw_mqtt.c` | MQTT |

---

## 4. 模块必须掌握

## 4.1 CAN

必须能答：

```text
标准帧/扩展帧
仲裁
ACK 位
错误帧
错误主动/被动/bus-off
位时间：SYNC + PROP + PHASE1 + PHASE2
采样点
SJW
```

必须会算：

```text
bitrate = fCAN / (prescaler × (1 + BS1 + BS2))
sample point = (1 + BS1) / (1 + BS1 + BS2)
```

要能解释项目中的：

```text
STM32 0.75
Linux 0.866
→ ACK error
→ 改为 0.75
```

## 4.2 ISO-TP

必须能画：

```text
SF
FF
CF
FC
```

必须能解释：

```text
PCI 高 4 位
SF 长度
FF 12 位长度
CF SN 0..15
FC FS/BS/STmin
N_Bs / N_Cr
```

必须能讲项目：

```text
4095 字节
BS=8
STmin=5ms
CF 发送失败回滚重试
```

## 4.3 UDS

必须能答：

```text
0x10 会话
0x3E TesterPresent / S3
0x22 ReadDataByIdentifier
0x19 ReadDTCInformation
0x14 ClearDiagnosticInformation
```

必须知道：

```text
正响应 SID + 0x40
负响应 0x7F + SID + NRC
NRC 0x11/0x12/0x13/0x22/0x31/0x72/0x73
```

#### DTC

必须能答：

```text
5 类 DTC
状态位 testFailed / confirmed / pending
掉电保存
sector 4
0x14 清除
```

## 4.4 Bootloader

必须能答：

```text
A/B 分区
512B Header
CRC32
双 Metadata
seq
trial/rollback/confirm
```

必须能解释跳转：

```c
__disable_irq();
SysTick->CTRL = 0;
SysTick->LOAD = 0;
SysTick->VAL = 0;
SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
SCB->VTOR = app_base;
__DSB();
__ISB();
__set_MSP(sp);
__enable_irq();
entry();
```

必须能讲两个坑：

```text
App 没设 VTOR
Bootloader 没关 SysTick
```

## 4.5 OTA

必须能讲：

```text
0x34 00 44 addr size
0x36 counter data
0x37 exit
max block 1024
只写非活动槽
Header/CRC/vector 校验
trial Metadata
```

必须能讲失败：

```text
目标槽 active → NRC 31
写失败 → NRC 72
block counter 错 → NRC 73
传输中断 → Metadata 不变，旧槽仍启动
```

## 4.6 Linux 网关

必须能讲：

```text
SocketCAN
ISO-TP client
Unix socket
SQLite
MQTT
rc.local
```

必须能讲：

```text
stale DTC response `59 02 FF` 被误当 0x34 响应
→ drain + SID 校验
```

必须知道限制：

```text
gatewayd 单线程
poll 不是 epoll
MQTT 每 5 秒重连
无断线补传
```

---

## 5. 故障故事（STAR）

### 5.1 SysTick 没关，App 卡在 BSS

```text
S：App 跳转后卡 LoopFillZerobss
T：看 PC/LR/栈
A：Bootloader HAL_Init 开了 SysTick，跳转前没关
R：关 SysTick、清 pending、再开中断
```

### 5.2 VTOR 没设

```text
S：App 能进但 UDS timeout
T：PC 在 App，中断不进
A：VECT_TAB_OFFSET 加了，USER_VECT_TAB_ADDRESS 没加
R：VTOR 指向 0x08020200/0x08060200
```

### 5.3 CAN 采样点

```text
S：STM32 ACK error，no free mailbox
T：Linux 0.866，STM32 0.75
A：采样点不一致
R：Linux 改 0.75
```

### 5.4 0x34 收到 59 02 FF

```text
S：0x34 第一块失败
T：SQLite 记录 req=34 resp=5902FF
A：上一条 DTC 响应当成 0x34
R：发送前 drain，响应校验 SID
```

### 5.5 ISO-TP CF 丢失

```text
S：FF/FC 有，CF 没有，N_Cr timeout
T：can 队列满，tx_frame 返回失败
A：isotp_tick 发送失败仍推进序号
R：失败回滚 pos/seq/state
```

---

## 6. 7 天计划

### Day 1：架构 + CAN

```text
画架构图
背 Flash 分区
算 500k 位定时
讲清采样点/ACK/bus-off
```

产出：1 页架构 + 1 页 CAN 笔记。

### Day 2：ISO-TP

```text
手画 SF/FF/CF/FC
写 4095 字节时序
解释 BS/STmin/N_Cr
```

产出：1 张 ISO-TP 时序图。

### Day 3：UDS + DTC

```text
0x10/3E/22/19/14
正/负响应
DTC 状态位
sector 4
```

产出：UDS 请求/响应表。

### Day 4：Bootloader

```text
A/B 分区
Header
CRC
Metadata 状态机
跳转
```

产出：trial/rollback/confirm 状态图。

### Day 5：OTA

```text
0x34/36/37
非活动槽
失败路径
3 次 OTA 数据
```

产出：OTA 流程图 + 失败矩阵。

### Day 6：Linux 网关

```text
SocketCAN
gatewayd/gwctl
SQLite/MQTT
stale response
```

产出：网关数据流图。

### Day 7：模拟面试

```text
30 秒/3 分钟/10 分钟
40 题随机抽
每个故障讲 STAR
```

---

## 7. 动手验证

```text
candump 抓 0x34/0x36/0x37
cansend 手动 FF/CF
读 Metadata 十六进制
看 PC/VTOR
gdb 断点 boot_jump_to
Python 复算 CRC32
注入错误 DID
can0 采样点改回 0.866 复现 ACK error
```

能复现，才算掌握。

---

## 8. 自检

```text
[ ] 能画架构
[ ] 能背 Flash 分区
[ ] 能算位定时
[ ] 能画 ISO-TP
[ ] 能解释 NRC
[ ] 能讲 DTC 状态位
[ ] 能讲 trial/rollback/confirm
[ ] 能讲 VTOR/MSP/SysTick
[ ] 能讲 OTA 失败路径
[ ] 能讲 stale response
[ ] 能讲 3 个故障 STAR
[ ] 能诚实说未做 24h/50 次
```

---

## 9. 不能说的话

```text
50/50 OTA
24h/72h
AUTOSAR
功能安全
量产
epoll 多线程网关
断点续传
安全访问
```

没做就是没做。
