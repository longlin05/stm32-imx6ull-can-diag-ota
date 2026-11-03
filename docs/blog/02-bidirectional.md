# STM32 + i.MX6ULL CAN 诊断与 OTA（02）：双向命令/ACK，以及 50 ms 超时为什么是“假丢包”

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[01 旧 CAN 项目基线审计](https://blog.csdn.net/longlin101/article/details/169563492)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.2-bidirectional`

AImate 原来只有 STM32 → Linux 的周期上报：

```text
0x111 PIR
0x112 温湿度
0x113 心跳
```

Linux → STM32 的 `0x221/0x222/0x228` 只有 ID 和帧格式，MCU 端没有实现。

这一篇做三件事：

1. 把单向链路补成双向命令/ACK；
2. 做 1000 次命令压测；
3. 解释一个很容易误判的现象：**把 ACK 超时改短，丢包率反而升高**。

---

## TL;DR

- 双向命令/ACK 已跑通：`0x221/0x222` 命令，`0x228` 分片 ACK。
- 1000 次命令：**1000/1000 成功，0 重试，P99 22.06 ms**。
- 50 ms 超时那次：2006 次尝试 / 1006 次重试，但 **1000/1000 全部成功**。
- 这不是 CAN 丢包，是 **50 ms 超时小于 RTT p99（49.85 ms）**。
- 另一个问题是 STM32 的 `APP_REPORT_TICK_MS = 100`，命令每 100 ms 才处理一次；改成 20 ms 后 RTT 从约 99 ms 降到约 20 ms。
- 重复 `seq` 验证：`dup_cmd 300 → 301`，没有重复执行。

---

## 1. 背景：原来只有单向上报

旧工程已经能：

- 500 kbps 标准帧；
- `0x111/0x112/0x113` 周期/事件上报；
- Linux 端 `candump` 能收到。

但 Linux 发命令时，STM32 端没有解析，也没有 ACK。  
所以“双向 CAN 通信”其实只完成了物理层和单向上报。

这次要补的是应用层：

```text
Linux 发 0x221/0x222
        ↓
STM32 校验 CRC、执行命令
        ↓
STM32 回 0x228 ACK
        ↓
Linux 按 cmd + seq 匹配
```

---

## 2. 协议：命令、ACK、seq

### 2.1 命令帧 `0x221/0x222`

8 字节固定：

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `uint8` | `cmd` | 命令码 |
| 1 | `uint8` | `seq` | 每条**新**命令 +1；重传复用同一 seq |
| 2..5 | `uint8[4]` | `args` | 参数，小端，未用填 0 |
| 6..7 | `uint16 LE` | `crc16` | CRC-16/CCITT-FALSE，覆盖 0..5 |

CRC 参数：

```text
poly   = 0x1021
init   = 0xFFFF
refin  = false
refout = false
xorout = 0
```

例子：

```text
0x222#10 00 00 00 00 00 94 14
```

- `cmd=0x10`：查询版本；
- `seq=0x00`；
- 后两字节是 CRC16。

### 2.2 ACK 帧 `0x228`

ACK 也固定 8 字节，但按 4 字节分片：

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `uint8` | `cmd` | 回显命令码 |
| 1 | `uint8` | `seq` | 回显 seq |
| 2 | `uint8` | `status` | 0=OK，1=未知命令，2=参数错误，3=不支持 |
| 3 | `uint8` | `frag` | 分片号，从 0 开始 |
| 4..7 | `uint8[4]` | `data` | 载荷分片，不足补 0 |

为什么需要分片？

`0x12` 查询统计返回 11 字节，8 字节 ACK 装不下，所以拆成 3 帧：

```text
frag0: cmd seq status 0  data[0..3]
frag1: cmd seq status 1  data[4..7]
frag2: cmd seq status 2  data[8..10] 00
```

接收端按 `cmd + seq` 匹配，再按 `frag` 排序拼接。

### 2.3 seq、重试、去重

- Linux 每条新命令 `seq++`；
- 超时重发时 **seq 不变，帧字节完全一样**；
- STM32 用 `(cmd, seq)` 做缓存，深度 4；
- 命中缓存：`dup_cmd++`，原样回放 ACK，**不重复执行**；
- 未命中：执行命令，缓存应答，再发 ACK。

这是“ACK 丢失后重传不会执行两次”的关键。

---

## 3. 第一次压测：1000 条命令要 98 秒

第一版 STM32 任务周期是：

```c
#define APP_REPORT_TICK_MS 100U
```

`Report_Task` 每 100 ms 醒一次，命令处理 `AppCmd_Task()` 在这个任务里。

压测结果：

```text
压测命令数   : 1000
成功 / 失败  : 1000 / 0
总尝试 / 重试: 1000 / 0
耗时         : 98.66s
平均速率     : 10.1 cmd/s
RTT min/avg/max : 8.81 / 98.38 / 103.03 ms
RTT p50/p99  : 99.72 / 102.86 ms
```

没有丢包，也没有重试，但每条命令都要等约 100 ms。

### 为什么平均是 98 ms，不是 50 ms

因为测试是串行的：

```text
STM32 任务醒来，发 ACK
        ↓
Linux 立刻收到 ACK，马上发下一条
        ↓
新命令刚好错过这一轮
        ↓
只能等下一次 100 ms 任务唤醒
```

每条命令都刚好等满一个 tick，平均接近 100 ms。  
1000 条 × 100 ms ≈ 100 秒，和实测 98.66 秒对得上。

---

## 4. 把超时改短，反而“丢包”

第一版 Linux ACK 超时是 50 ms。

结果：

```text
压测命令数   : 1000
成功 / 失败  : 1000 / 0
总尝试 / 重试: 2006 / 1006
耗时         : 99.67s
RTT min/avg/max : 5.71 / 45.88 / 51.50 ms
RTT p50/p99  : 46.77 / 49.85 ms
```

看起来像“丢包率很高”，但实际上：

- 1000 条命令最终 **1000/1000 成功**；
- 2006 次尝试 = 1000 条命令 + 1006 次重试；
- p99 49.85 ms，max 51.50 ms，而超时是 50 ms。

也就是说：

> **超时窗口刚好卡在 RTT p99 上，大量命令会先超时，再重发；1006 次重试约等于每条命令多等一次 50 ms。**

这不是 CAN 链路丢包，是 **超时设得太小**。

### `ack_rx` 为什么不是命令数

早期日志里出现过：

```text
cmd_tx=3 ack_rx=6
```

3 条命令，6 个 ACK 分片：

```text
版本 1 片 + 配置 2 片 + 统计 3 片 = 6 片
```

所以 `ack_rx` 是分片数，不是命令成功数。  
命令成功数看 `ok`。

---

## 5. 根因：不是 CAN，是调度粒度

两个问题要分开：

| 现象 | 根因 |
|---|---|
| 50 ms 超时导致大量重试 | 超时小于 RTT p99，属于“假丢包” |
| 每条命令约 100 ms | STM32 命令处理每 100 ms 才轮一次 |

CAN 外设发一帧很快，慢的是：

```text
CAN RX FIFO
  → Report_Task 100 ms 醒来
  → AppCmd_Task 解析
  → 执行命令
  → CAN_Send ACK
```

所以优化方向不是“改 CAN 波特率”，而是：

1. 让 Linux 超时大于 RTT；
2. 让 STM32 更频繁地处理命令，或者用中断唤醒。

---

## 6. 修复：先调超时，再调 tick

### 6.1 超时改成 150 ms

50 ms 超时窗口小于 max 51.50 ms，必然有大量重试。  
把 Linux 超时改成 150 ms：

```text
总尝试 / 重试: 1000 / 0
耗时         : 98.66s
RTT p99      : 102.86 ms
```

重试归零，但每条命令仍然约 100 ms，因为 tick 还是 100 ms。

结论：

> 超时必须大于 RTT p99；但只改超时，只能消除“假丢包”，不能降低延迟。

### 6.2 tick 从 100 ms 改成 20 ms

```c
/* Core/CustomDrivers/app_report.h */
#define APP_REPORT_TICK_MS 20U
```

周期帧仍按各自的时间判断，不受影响；只是命令处理更频繁。

结果：

```text
压测命令数   : 1000
成功 / 失败  : 1000 / 0
总尝试 / 重试: 1000 / 0
耗时         : 20.04s
平均速率     : 49.9 cmd/s
RTT min/avg/max : 18.57 / 19.75 / 26.24 ms
RTT p50/p99  : 19.74 / 22.06 ms
```

对比：

| tick | 超时 | 尝试次数 | 重试 | 耗时 | RTT p99 |
|---|---:|---:|---:|---:|---:|
| 100 ms | 50 ms | 2006 | 1006 | 99.67s | 49.85 ms |
| 100 ms | 150 ms | 1000 | 0 | 98.66s | 102.86 ms |
| 20 ms | 150 ms | 1000 | 0 | 20.04s | 22.06 ms |

最终选择：

```text
APP_REPORT_TICK_MS = 20
ACK timeout       = 150 ms
```

P99 22 ms，满足后面 UDS `0x22 < 50 ms` 的目标。

---

## 7. 去重验证

发两次同一个 `seq=0x77` 的查询：

```bash
cansend can0 222#107700000000C66E
cansend can0 222#107700000000C66E
```

再查两次统计：

```bash
cansend can0 222#1278000000007F80
cansend can0 222#1279000000002E2A
```

抓到：

```text
can0 228 [8] 12 78 00 00 3B 06 00 00
can0 228 [8] 12 78 00 01 2C 01 5F 09
can0 228 [8] 12 78 00 02 00 00 78 00

can0 228 [8] 10 77 00 00 10 12 00 00
can0 228 [8] 10 77 00 00 10 12 00 00

can0 228 [8] 12 79 00 00 3E 06 00 00
can0 228 [8] 12 79 00 01 2D 01 64 09
can0 228 [8] 12 79 00 02 00 00 79 00
```

统计解析：

| 查询 | cmd_rx | crc_err | dup_cmd | ack_tx | ack_fail | last_seq |
|---|---:|---:|---:|---:|---:|---:|
| `0x12 seq=0x78` | 1595 | 0 | 300 | 2399 | 0 | 0x78 |
| `0x12 seq=0x79` | 1598 | 0 | 301 | 2404 | 0 | 0x79 |

变化：

```text
cmd_rx  +3   = 两次 0x77 + 最后一次 0x79
dup_cmd +1   = 第二次 0x77 命中缓存
ack_tx  +5   = 两次 0x77 各 1 帧 + 0x79 统计 3 帧
crc_err = 0
ack_fail = 0
```

结论：

> 第二次 `0x77` 没有重复执行，而是命中缓存并回放 ACK。

---

## 8. 踩坑清单

1. **`ack_rx` 是分片数，不是命令数**  
   看命令成功数要用 `ok`，看分片用 `ack_rx`。

2. **超时小于 RTT p99，会制造“假丢包”**  
   50 ms 超时那次 1006 次重试，但全部成功；不是链路丢包。

3. **串行命令/ACK + 轮询 tick 会相位锁定**  
   每次 ACK 后立刻发下一条，新命令刚好错过本轮，等待一个完整 tick。

4. **`0x228` 的源地址位是历史遗留**  
   ID 位域里源地址仍是 Linux，但实际发送方是 STM32；接收按完整 ID 匹配。

5. **去重缓存要存“应答内容”，不是只存一个标志**  
   否则重传时查询结果（uptime、统计值）会变化。

---

## 9. 下一步

通信层到此冻结：

```text
命令/ACK：完成
seq/CRC/重试/去重：完成
丢包/RTT 统计：完成
tag：a-v0.2-bidirectional
```

下一步是 ISO-TP：

```text
SF / FF / CF / FC
BlockSize / STmin
N_Cr 超时
4 KB 分片/重组
```

然后 UDS：

```text
0x10 / 0x3E / 0x22 / 0x19 / 0x14
```

---

## 10. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.2-bidirectional`
- 1000 次压测日志：`test/records/20261103-can-stress-1000.log`
- 50 ms 超时日志：`test/records/20261103-can-timeout-50ms.log`
- 去重日志：`test/records/20261103-can-dedup.log`
- 测试报告：`docs/test-report.md`
- 下一篇：03 ISO-TP 多帧

标签：`STM32` `CAN` `i.MX6ULL` `嵌入式Linux` `ISO-TP`
