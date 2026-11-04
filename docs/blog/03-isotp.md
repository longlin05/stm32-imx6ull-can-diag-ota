# STM32 + i.MX6ULL CAN 诊断与 OTA（03）：ISO-TP 从 0 到 4095 字节，以及 30% 丢帧是怎么修掉的

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[02 双向命令/ACK：1000 次压测](https://blog.csdn.net/longlin101/article/details/169950471)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.4-isotp-stm32`

CAN 一帧只有 8 字节。

诊断读 VIN、读 DTC 列表、OTA 传固件，数据都超过 8 字节。ISO-TP（ISO 15765-2）就是解决这件事的传输层：把长消息拆成多个 8 字节 CAN 帧，接收端再拼回去。

这一篇记录四件事：

1. ISO-TP 的帧格式和参数；
2. Linux / STM32 如何复用同一份协议核心；
3. 4095 字节在真实 CAN 上跑通；
4. DHT11 的 4ms 临界区如何造成约 30% 丢帧，以及怎么修。

---

## TL;DR

- 使用 11 位标准帧、Normal addressing、Physical addressing；
- 请求 `0x7E0`，响应 `0x7E8`；
- 普通 ISO-TP 最大 4095 字节，不是 4GB；
- SF / FF / CF / FC 全部实现；
- BS=8、STmin=5ms；
- 真实 CAN 4095 字节：585 个 CF、74 个 FC，`rx_ovf=0`、`tx drops=0`；
- DHT11 每 2s 读一次时仍能跑通；
- 异常注入：丢 FC、错序 CF、丢最后 CF 都能按预期超时/中止。

---

## 1. 为什么不是直接发 CAN 帧

一帧经典 CAN：

```text
8 字节
```

一条 100 字节的 UDS 响应，需要拆成：

```text
1 个 FF + 14 个 CF
```

接收端必须知道：

- 总长度是多少；
- 当前是第几个连续帧；
- 发快了还是发慢了；
- 丢了哪一帧。

这就是 ISO-TP 的职责。

---

## 2. 四种帧

### SF：单帧

```text
byte0: 0x0N    N = 数据长度 1..7
byte1..N: 数据
```

### FF：首帧

```text
byte0: 0x1X    12 位总长的高 4 位
byte1: 0xLL    12 位总长的低 8 位
byte2..7: 前 6 字节数据
```

12 位长度上限：

```text
0xFFF = 4095 字节
```

### CF：连续帧

```text
byte0: 0x2N    N = 0..15，循环
byte1..7: 7 字节数据
```

### FC：流控帧

```text
byte0: 0x3F    0x30=CTS / 0x31=WAIT / 0x32=OVFLW
byte1: BS
byte2: STmin
```

---

## 3. 参数怎么定

第一版没有一上来就调极限，先用保守值：

| 参数 | 值 |
|---|---|
| BS | 0（一次发完） |
| STmin | 0 ms |
| N_Bs / N_Cr | 1000 ms |
| padding | 0xCC |

跑通后，再测：

```text
BS=8
STmin=5ms
```

`BS=8` 表示每 8 个 CF 等一次 FC。  
4095 字节共 585 个 CF，所以 FC 数：

```text
1 + 584 / 8 = 74
```

实测正好 74 个。

---

## 4. 一份核心，两个平台

ISO-TP 核心写成平台无关的纯 C：

```text
isotp.c / isotp.h
```

只依赖两个回调：

```c
int tx_frame(uint32_t id, const uint8_t *data, uint8_t dlc);
uint32_t now_ms(void);
```

Linux 侧：

```c
tx_frame -> can_send()
now_ms   -> CLOCK_MONOTONIC
```

STM32 侧：

```c
tx_frame -> CAN_Send() / HAL_CAN_AddTxMessage()
now_ms   -> HAL_GetTick()
```

这样协议逻辑只写一遍，Linux 和 STM32 跑同一份状态机。  
**复用的是协议逻辑，不是 socket 代码。**

---

## 5. 真实 CAN 4095 字节

测试条件：

```text
STM32F407 + i.MX6ULL
CAN 500 kbps
BS=8 / STmin=5ms
payload=4095
DHT11 每 2s 读一次
```

STM32 串口：

```text
ISOTP fc: fs=0 bs=8 stmin=5 at 1991762ms pos=4094/4095 ok
ISOTP rx: 4095B in 2688ms | 585 CF, 74 FC, CF gap 576..7234us | 1523B/s
ISOTP tx: echo started 4095B -> 0x7E8
ISOTP tx: 4095B echo in 2633ms | 1555B/s
ISOTP sum: n=6 | rx 648/2334/2720ms 1534B/s | tx 654/2300/2633ms 1557B/s | total 27812ms 1546B/s | drops rx_ovf=0 tx=0
```

解释：

- 585 个 CF：4095 = 6 + 584×7 + 1；
- 74 个 FC：BS=8；
- `rx_ovf=0`：接收环形缓冲没有溢出；
- `tx drops=0`：发送邮箱没有丢帧；
- 约 2.6s，符合 585×5ms 的量级；
- 最小 CF gap 576us 出现在块边界（FC 后首帧），块内间隔由 selftest 校验。

---

## 6. 30% 丢帧是怎么来的

这是这篇最值得写的坑。

STM32 上 DHT11 读 40 bit 时：

```c
taskENTER_CRITICAL();   /* BASEPRI = 5 */
/* 读 40 bit，约 4ms */
taskEXIT_CRITICAL();
```

CAN RX 中断优先级原来是 5。

于是：

```text
4ms 关中断
500kbps 下一帧约 250us
4ms 约 16 帧
CAN 硬件 FIFO 只有 3 帧
```

FIFO 溢出，CF 丢失，接收端看到序号跳变，整条 ISO-TP 消息中止。

4KB 回显约 0.6s，DHT11 每 2s 一次，撞上的概率约 30%。

### 修复

不是把 DHT11 的临界区删掉，而是把 CAN RX 中断放到优先级 4：

```c
HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 4, 0);
```

FreeRTOS 的 `BASEPRI=5` 只屏蔽优先级 5 及更低，优先级 4 仍能进。

CAN ISR 只做：

```text
HAL_CAN_GetRxMessage
  → 写入 32 帧软件环形缓冲
  → 退出
```

**ISR 里不调用任何 FreeRTOS API，不 printf。**

任务侧：

```text
1ms 任务从环形缓冲取帧
  → isotp_on_frame()
```

这样 DHT11 的 4ms 临界区可以保留，CAN 接收不再溢出。

---

## 7. 异常注入

异常路径不能只靠“应该没问题”，要真的注入。

### 丢 FC

```text
gw: tx_timeout tx_state=4
STM32: N_Cr timeout after 1000ms, got 6/100B (0 CF, 1 FC)
```

### 错序 CF

```text
gw: tx_abort tx_state=5
STM32: SN mismatch (expected 2, got 3) at 13/100B, FC OVFLW sent
```

### 丢最后一个 CF

```text
gw: timeout tx_state=3 rx_state=0
STM32: N_Cr timeout after 1049ms, got 97/100B (13 CF, 2 FC)
```

三条都符合预期。

一个测试细节：板上同时跑 `mock_ecu` 和 STM32 时，两个节点都会回 `0x7E8`，日志会混。  
做故障注入前先：

```bash
kill $(pidof mock_ecu)
```

只留 STM32。

---

## 8. 当前边界

第一版 ISO-TP 做到：

```text
SF / FF / CF / FC
4095 字节
BS / STmin
N_Bs / N_Cr
错序中止 + OVFLW
```

还没做：

- 丢 CF 后的自动重传（当前是中止 + 上报）；
- 32 位扩展长度 / 4GB；
- CAN FD；
- UDS 服务层。

对 UDS/OTA 来说，4095 字节已经够用；OTA 按 1024 字节一块传，不需要 4GB。

---

## 9. 下一步：UDS

ISO-TP 是传输层，下一层才是诊断服务：

```text
0x10 会话控制
0x3E TesterPresent
0x22 读 DID
```

UDS 请求/响应仍然走 `0x7E0/0x7E8`，只是 payload 从“任意字节”变成“SID + 参数”。

---

## 10. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.4-isotp-stm32`
- Linux ISO-TP：`gateway/src/isotp.c`
- STM32 适配：`firmware/app/Core/CustomDrivers/isotp_port.c`
- 测试日志：
  - `test/records/20261104-isotp-real-4095-bs8.log`
  - `test/records/20261104-isotp-fault-injection.log`
  - `test/records/20261104-isotp-selftest-final.log`

标签：`STM32` `CAN` `ISO-TP` `i.MX6ULL` `嵌入式Linux`
