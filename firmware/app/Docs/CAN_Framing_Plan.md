# CAN 上报与命令通道 —— 实现说明 / 交接

> **本文与 [CAN_Protocol.md](CAN_Protocol.md) 的分工**：
> - `CAN_Protocol.md` = **对外接口契约**（帧 ID、字节布局、周期、语义），
>   Linux 端只需要看那一份。
> - 本文 = **节点侧怎么实现** + **联调步骤** + **已知偏差**。
>   两份文件里的 ID / 周期 / 字段必须一致，**改一份就要同步改另一份**。
>
> **实施状态**：节点侧代码已按 AImate 的协议 v1.2 移植完成
> （`can_protocol.h` + `app_report.c/h` + `app_cmd.c/h` + `driver_pir.c/h`
> 的 PIR 开关 + `main.c` 接线），**命令行编译链接通过**；尚未上板验证。
> 上板前在 CubeIDE 里刷新工程并 Clean（新增了 `app_cmd.c`）。

## 1. 目标与范围

把已经采到的数据（温湿度、人体感应）+ 节点自身诊断信息打成 CAN 帧发出去，
并接收 Linux 下发的查询/配置命令。

**做**：

- 上行：`0x111` 人体感应（事件 + 5s 保活）、`0x112` 温湿度（2s）、
  `0x113` 心跳（10s）
- 下行：`0x221` 配置写入、`0x222` 查询、`0x228` ACK
- 命令帧 CRC16 校验、`(cmd, seq)` 去重、统计
- 运行时修改 PIR 开关、温湿度周期、心跳周期

**不做**：多节点组网、时间同步、扩展帧、固件升级分片。

## 2. 文件清单

| 文件 | 动作 | 职责 |
|---|---|---|
| `Core/CustomDrivers/can_protocol.h` | 重写 | 协议常量 + 帧编解码 + CRC 辅助（纯头文件，不依赖 HAL） |
| `Core/CustomDrivers/app_report.c` | 重写 | 环境数据快照、三种上行帧组包与调度、配置读写 |
| `Core/CustomDrivers/app_report.h` | 重写 | 上报层对外接口与可配置周期 |
| `Core/CustomDrivers/app_cmd.c` | **新增** | 命令帧校验、去重缓存、执行、ACK 分片 |
| `Core/CustomDrivers/app_cmd.h` | **新增** | 命令通道对外接口与统计结构 |
| `Core/CustomDrivers/driver_pir.c/h` | 修改 | 增加 `PIR_SetEnabled/PIR_IsEnabled`、`DISABLED/RAW` 标志位 |
| `Core/Src/main.c` | 修改 | `AppCmd_Init()`；注释同步新帧号 |
| `Core/Src/can.c` / `can.h` | **不动** | 驱动层保持原样 |
| `Docs/CAN_Protocol.md` | 重写 | 对外协议契约 v1.2 |
| `Docs/CAN_Framing_Plan.md` | 重写 | 本文 |

## 3. 分层与数据流

```
DHT11 ─(2s)─┐                      ┌─→ OLED 屏（已有的 DisplayTask）
            ├─→ 环境数据快照 ───────┤
            │   （app_report.c）    └─→ 0x112 温湿度帧
PIR ─(10ms)─┤                          （ReportTask）
  driver_pir│   PIR 读接口（单字原子）→ 0x111 事件帧 / 保活帧
            └─(跳变时 notify)────────→ 唤醒 ReportTask
                                      0x113 心跳帧
Linux ─(0x221/0x222)─→ CAN FIFO0 ─(CAN1_RX0 中断)→ 软件环形缓冲（can.c）
                                                        │
                             IsoTpPort_Task() 取出 ──────┤
                                                        ├→ AppCmd_ProcessFrame() → 0x228 ACK
                                                        └→ isotp_on_frame()      → 0x7E0 echo
                                                        │
                                                  CAN_Send()  ← 全工程唯一的发送入口
                                                        │
                                                    can.c → CAN1 → 总线
```

## 4. 任务与优先级

| 任务 | 优先级 | 周期 | 干什么 |
|---|---|---|---|
| `StartDefaultTask` | `tskIDLE_PRIORITY + 2` | 10 ms | `PIR_Task()` 采样 + `Report_NotifyPirChange()` |
| `DisplayTask` | `tskIDLE_PRIORITY + 1` | 500 ms / 2 s | 读 DHT11、刷 OLED、`Report_PublishEnv()` |
| `Report_Task` | `tskIDLE_PRIORITY + 1` | 100 ms 唤醒 | 组装并发送全部 CAN 帧 |
| `IsoTpPort_Task` | `tskIDLE_PRIORITY + 2` | 1 ms | 排空接收环形缓冲（命令帧转 `AppCmd_ProcessFrame()`）、`isotp_tick()`、echo |
| （空闲 / 定时器任务） | 0 / 6 | —— | FreeRTOS 自带 |

**为什么 Report_Task 是 100ms 而不是 1s**：100ms 的唤醒粒度足够跟上 Linux
端 200ms 的命令超时预算，而周期帧仍按各自的时间戳判断，不受唤醒粒度影响。

**为什么命令处理不单独建任务**：接收侧只有一条消费链路 —— CAN1_RX0 中断把帧
搬进软件环形缓冲，`IsoTpPort_Task()` 是唯一消费者，取到帧后先喂 ISO-TP、
再转给 `AppCmd_ProcessFrame()`。两个任务各取一半会互相偷帧。
发送侧有 `Report_Task` 和 `IsoTpPort_Task` 两个调用者，互斥量在 `CAN_Send()`
内部（can.c 的 `s_can_tx_lock`）。

## 5. 上报节奏（周期帧不能被事件帧饿死）

```c
for (;;)
{
  (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_REPORT_TICK_MS));  /* 100ms 或事件 */
  now = xTaskGetTickCount();

  AppCmd_Task();          /* 先处理命令，ACK 不被本轮的周期帧拖慢 */

  /* 事件帧：与"上次已发出去的状态"比较，变了才发 */
  if (state != s_last_sent_state) { 发 0x111（ENTER/LEAVE）; }

  /* 周期帧：一律按"距上次发送的时间"判断，不看这次是超时醒还是被通知唤醒 */
  if (now - s_env_last_report >= s_env_period_ms)  发 0x112;
  if (now - s_pir_last_report >= 5000ms)           发 0x111（PERIOD）;
  if (now - s_hb_last_report  >= s_hb_period_ms)   发 0x113;
}
```

- 事件帧和保活帧共用 `0x111` 和同一个 `seq`，靠 `event` 字段区分。
- 上电第一轮 `s_last_sent_state = 0xFF`，不发事件帧；但会把三种周期帧各发一遍，
  Linux 端上电后很快就能看到数据。
- 发送顺序：事件帧 → 温湿度 → PIR 保活 → 心跳（心跳最后，`uptime_s` 取发送时刻）。

## 6. 环境数据快照

DHT11 不可重入、一次读要占 25ms，**只能有一个读者**。由 `DisplayTask` 当生产者，
`Report_Task` 当消费者读快照：

```c
typedef struct {
  DHT11_Data_t data;   /* 最近一次成功读到的值 */
  uint8_t      valid;  /* ≤5s 内读到过，由生产者算好 */
  uint8_t      never;  /* 上电至今一次都没读成功 */
} EnvSnapshot_t;
```

- 有效性（含 5s 过期规则）由生产者算，消费者原样使用 —— 规则只存在一处，
  屏和 CAN 不会出现"一个说过期一个说正常"。
- 跨任务读用 `taskENTER_CRITICAL()` 包住 4 字节拷贝（微秒级）。
- 组帧时把 DHT11 的「整数 + 0.1 单位小数」换算成协议要求的 0.01 单位：
  `temp_int * 100 + temp_dec * 10`，并钳位到协议范围。

## 7. 命令通道

### 7.1 处理流程

每一帧 `0x221` / `0x222`：

1. ID 不是命令帧 → 静默忽略（周期帧不产生 ACK）。
2. `DLC != 8` 或 CRC16 不匹配 → `crc_err++`，不回 ACK。
3. `cmd_rx++`，按 `(cmd, seq)` 查缓存。
4. 命中 → `dup_cmd++`，回放缓存应答，**不执行**。
5. 未命中 → 执行命令，缓存应答，发送 ACK。

缓存深度 4 条，存的是**完整应答**。Linux 因 ACK 丢失重传同一帧时，连查询结果
（uptime、统计值）都沿用第一次的，不会因为重放而变化。

### 7.2 命令与配置

| 命令 | 作用 | 参数范围 |
|---|---|---|
| `0x10` / `0x11` / `0x12` / `0x13` | 查询版本 / uptime / 统计 / 配置 | 无 |
| `0x20` | PIR 开关 | 0 / 1 |
| `0x21` | 温湿度周期 | 500..60000 ms |
| `0x22` | 心跳周期 | 1000..10000 ms |
| `0x23` | 清零统计 | 无 |

- 周期修改立即生效：从命令处理时刻起按新周期重新排队。
- PIR 关闭后 `state` 恒为 0、`flags.bit2 = 1`，保活帧继续发送；重新开启时
  滤波器从干净状态开始。
- 参数越界返回 `ACK_STATUS_BAD_PARAM`，原值不变。

## 8. 发送与失败处理

- 所有发送都是 `(void)CAN_Send(...)`，**不重试**：失败只累加
  `CAN_GetTxDropCount()`，由心跳帧的 `can_tx_drop` 上报给 Linux 端。
- 单帧等待空邮箱超时 20ms（上报路径和 ACK 路径一致）。500kbit/s 下发一帧 8 字节
  约 220µs，20ms 已是约 90 倍余量。
- **绝不能改成 `Error_Handler()`**：那会关中断死循环，TIM6 时基停摆，
  整个采集链路一起失效（`can.h` 里有同样的说明）。

### ⚠ 上板联调前必读：单节点挂在总线上会进总线关闭

CAN 是"发送方要收到 ACK 才算成功"的总线。如果总线上没有第二个节点应答
（比如只接了 STM32，Linux 那边还没接 USB-CAN 适配器），每一帧都会判为 ACK
错误，发送错误计数器 TEC 每帧 +8，三十多帧后 error-passive，再到 bus-off。

对策（任选其一）：

1. **联调时先把 Linux 侧接上**（`ip link set can0 up type can bitrate 500000`）；
2. 只做节点自测时，把 CAN 配成 **Loopback 模式**（`.ioc` 里 `CAN1.Mode`）；
3. `ABOM = ENABLE`（已配）让节点在 bus-off 后自动恢复，但它**不会**让单节点
   发送成功。

## 9. 配置项一览

| 配置 | 位置 | 当前值 |
|---|---|---|
| 节点源地址（决定帧 ID） | `can_protocol.h` 的 `CAN_NODE_STM32` | `0x1`（与 AImate 相同，同总线时需改一个） |
| 固件版本 `CAN_FW_VERSION` | `can_protocol.h` | `0x10`（v1.0） |
| 命令协议版本 `CMD_PROTO_VERSION` | `can_protocol.h` | `0x12`（v1.2） |
| 温湿度周期 | `app_report.h` | 2000 ms（可配 500..60000） |
| 心跳周期 | `app_report.h` | 10000 ms（可配 1000..10000） |
| PIR 保活周期 | `app_report.h` | 5000 ms（固定） |
| 上报任务唤醒粒度 | `app_report.h` | 100 ms |
| 发送超时 | `app_report.c` / `app_cmd.c` | 20 ms |
| 调试打印 | `can.c` 的 `CAN_DEBUG_PRINT` | 0 |

## 10. 验证清单

| 场景 | 期望现象 |
|---|---|
| 正常上电 | 100ms 内出现 `0x113`、`0x112`、`0x111`（PERIOD）各一帧 |
| 有人经过 | 先出一帧 `0x111`（event=ENTER），随后每 5s 一帧 PERIOD |
| 人离开 | 约 2s 保持窗口后出一帧 `0x111`（event=LEAVE） |
| 温湿度正常 | `0x112` 每 2s 一帧，`status=0x00`，数值与 OLED 一致 |
| 拔掉 DHT11 | 先 `status=0x01`（STALE，旧值），若上电就没接则是 `0x04`（IIC_ERR） |
| 查询版本 | `cansend can0 222#100100000000C5BE` → `228#1001000010120000` |
| 修改周期 | `0x21`/`0x22` 的 ACK 回显生效值，随后的周期立刻变化 |
| 命令重传 | 相同 8 字节重发，`dup_cmd` 增加，命令不重复执行 |
| 无 Linux 应答 | `tx_drop` 持续上涨（见第 8 节），恢复接线后停止上涨 |
| PIR 关闭 | `0x111` 的 `state` 恒 0、`flags.bit2=1`，保活帧不断 |

## 11. 已知偏差与风险

1. **人体感应帧不再带累计检出次数**。AImate 的 `0x111` 布局没有这个字段，
   为保持两节点协议一致而舍弃；计数仍显示在 OLED 上。若 Linux 端需要，
   应通过命令通道扩展一个新命令/帧，不要私改 `0x111`。
2. **两个 STM32 节点同总线会撞 ID**。AImate 与 My_AI 都用源地址 `0x1`，
   同时挂一条总线时必须把其中一个改成 `0x3`（ID 变成 `0x13x`）。
3. **DHT11 的失败原因只有一种**：`DHT11_Read()` 只返回成功/失败，
   所以 `driver_code` 只有 0/1；`CRC_ERR`/`NOT_READY` 状态值暂时不会出现。
4. **AppCmd_Task 在 Report_Task 里跑**：命令执行（改周期、关 PIR）
   与上报在同一任务，天然没有竞态；不要把它挪到别的任务里，否则需要重新
   审视 `CAN_Send()` 的单写者约定。
5. **新增 `app_cmd.c` 后要在 CubeIDE 里刷新（F5）+ Clean**，否则 IDE 不会
   把它加入编译；Debug 目录下的 `subdir.mk` / `objects.list` 是自动生成的，
   IDE 重新构建时会覆盖（本文档基于命令行编译验证，已手工补入）。
6. `pir_state` 与 OLED 上的计数可能差一个 10ms 采样周期（跳变瞬间），
   计数单调递增，对统计无影响。
7. `can_tx_drop` 不区分原因（无空邮箱 / HAL 报错 / 无 ACK），只看增量与趋势。

## 12. 后续可扩展

- 多节点：把 `CAN_NODE_STM32` 做成可配置项，每个节点分配不同源地址。
- 更长数据（波形、字符串）：需要先设计分片机制，`0x1Fx` 段已预留。
- 人体感应计数上报：新增独立帧或扩展命令通道，保持 `0x111` 与 AImate 兼容。
