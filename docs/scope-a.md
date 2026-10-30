# 项目 A 范围：CAN 诊断与 OTA 闭环（A 方案）

> 本文件是项目 A 的“目标定义”，优先级高于旧计划文档里的项目 A 描述。
> 基线：双机 CAN、传感器采集、SocketCAN 收发已经能跑；本项目只做增量。

---

## 1. 一句话目标

把现有“CAN 能通”的节点，升级为：

> **可诊断、可升级、可恢复、可度量**的车载风格 CAN 节点系统。

三个闭环：

```text
诊断闭环：ISO-TP + UDS + DTC
升级闭环：Bootloader + A/B + CRC32 + 回滚
可靠性闭环：bus-off + 故障注入 + 24h/72h + 指标
```

---

## 2. 基线 / 增量边界

| 模块 | 基线（已完成，不重写） | 本项目新增 |
|---|---|---|
| 双机 CAN | STM32→Linux 500 kbps 已通；Linux→STM32 预留未实现 | 双向命令/ACK、错误帧统计、bus-off 恢复、总线负载测试 |
| 传感器 | DHT11 / PIR 能读（光照未做） | 健康检查、超时/越界、DTC 触发 |
| 应用协议 | AImate `0x111/0x112/0x113`；`0x221/0x222/0x228` 预留 | ISO-TP、UDS、DTC、OTA |
| SocketCAN | 无（源码缺失） | 网关、诊断 CLI、OTA 服务端、SQLite、MQTT、systemd |
| 升级 | 无 | Bootloader、A/B、CRC32、续传、回滚 |
| 测试 | 基本没有 | 故障注入、24h/72h、P99、50 次 OTA |

> DHT11 是单总线，不是 I2C；采样间隔 ≥ 2 s，baseline 的 `0x112` 就是 2 s 一帧。光照传感器未做，已移出本项目；AHT20 留给 B 项目。

**原则：**

- 能复用的模块先打 `a-baseline` tag，不重写；
- 新功能独立提交，commit 历史能看出增量；
- 旧代码的坑先记录，不阻塞新功能。

---

## 3. UDS 子集（A 方案）

只做 5 个服务，先跑通闭环，不追求全服务。

| 服务 | 名称 | 子功能/参数 | 验收 |
|---|---|---|---|
| `0x10` | DiagnosticSessionControl | `0x01` 默认、`0x03` 扩展 | 会话切换，响应正确 |
| `0x3E` | TesterPresent | `0x00` | 5 s 会话超时前保持 |
| `0x22` | ReadDataByIdentifier | 见 DID 表 | 响应 < 50 ms |
| `0x19` | ReadDTCInformation | `0x02` 按状态掩码 | 能读到注入的 DTC |
| `0x14` | ClearDiagnosticInformation | `0xFFFFFF` | 清后 `0x19` 为空 |

### DID 表

| DID | 含义 | 长度 | 说明 |
|---|---|---|---|
| `0xF186` | Active Diagnostic Session | 1 | `0x01/0x03` |
| `0x0100` | Software Version | 4 | 主/次/补丁/构建 |
| `0x0101` | Uptime | 4 | 秒 |
| `0x0102` | CAN Error Counters | 4 | TX/RX/bus-off/error-passive |
| `0x0103` | Reset Reason | 1 | 上电/看门狗/软件复位 |
| `0x0104` | DTC Count | 1 | 当前 confirmed 数量 |

### 负响应

统一格式：

```text
7F <SID> <NRC>
```

至少支持：

| NRC | 含义 |
|---|---|
| `0x11` | Service not supported |
| `0x12` | Sub-function not supported |
| `0x13` | Incorrect message length |
| `0x22` | Conditions not correct |
| `0x31` | Request out of range |
| `0x7F` | Service not supported in active session |

---

## 4. DTC 设计

| DTC | 含义 | 触发条件 | 恢复条件 |
|---|---|---|---|
| `0xC001` | DHT11 timeout / checksum | 单总线连续失败 3 次 | 连续成功 5 次 |
| `0xC002` | CAN bus-off | 检测到 bus-off | 恢复且收到有效帧 |
| `0xC003` | OTA CRC failure | 升级包 CRC32 不匹配 | 下一次升级成功 |
| `0xC004` | Sensor out of range | 温湿度/光照越界 | 回到范围并持续 10 s |
| `0xC005` | Flash error | 擦除/写入/校验失败 | 重新写入成功 |

### 状态字节

至少实现：

| Bit | 名称 | 含义 |
|---|---|---|
| 0 | testFailed | 当前测试失败 |
| 1 | testFailedThisCycle | 本周期失败过 |
| 2 | pending | 待确认 |
| 3 | confirmed | 已确认 |
| 7 | warningIndicator | 需要告警 |

存储：Config 区双备份（A/B 两份 + magic + CRC + 递增序号），掉电不丢。

---

## 5. ISO-TP

| 帧类型 | PCI | 用途 |
|---|---|---|
| SF | `0x0X` | 单帧，≤ 7 字节 |
| FF | `0x1X` | 多帧首帧，带总长度 |
| CF | `0x2X` | 连续帧，序号 0–15 循环 |
| FC | `0x3X` | 流控：CTS/WAIT/OVFLW |

参数：

- 物理请求 `0x7E0`，物理响应 `0x7E8`；
- 功能请求 `0x7DF`（可选）；
- `BlockSize` 先固定 8；
- `STmin` 先固定 10 ms；
- `N_Cr` 超时 1000 ms；
- 多帧接收缓冲 ≥ 4 KB。

验收：

- 4 KB 数据多帧传输 0 错误；
- 丢一帧能超时重传；
- 序号错、长度错能返回负响应/丢弃并计数。

---

## 6. OTA / Bootloader

### Flash 分区

| 区域 | 地址 | 大小 | 用途 |
|---|---|---|---|
| Bootloader | `0x08000000–0x0801FFFF` | 128 KB | 启动选择、CRC、跳转、回滚 |
| Config | `0x08020000–0x0803FFFF` | 128 KB | 元数据双备份 |
| App A | `0x08040000–0x0809FFFF` | 384 KB | 应用 A |
| App B | `0x080A0000–0x080FFFFF` | 384 KB | 应用 B |

### 状态机

```text
IDLE → RECEIVING → VERIFYING → PENDING → TRIAL → CONFIRMED
                         │                    │
                         └──→ ROLLBACK ←──────┘
```

### 必做

- 只写非活动分区；
- 整包 CRC32 + 向量表校验；
- 断点续传（记录 offset）；
- trial/confirm；
- 看门狗超时回滚；
- 版本号上报。

### 验收

- 50 次 OTA 成功；
- 25%/50%/75% 处断电均可回滚；
- 单节点升级 < 60 s；
- 升级后诊断 `0x22 0x0100` 版本变化。

---

## 7. Linux 网关

| 模块 | 职责 |
|---|---|
| `can_rx` | SocketCAN 收包、过滤、错误帧统计 |
| `isotp` | ISO-TP 客户端（SF/FF/CF/FC） |
| `uds_client` | `0x10/0x3E/0x22/0x19/0x14` |
| `ota` | 读固件、分片、重试、进度、版本确认 |
| `storage` | SQLite 记录诊断/升级/传感器数据 |
| `mqtt` | 上报状态、告警、升级结果 |
| `cli` | `diag session/read/dtc/clear`、`ota upgrade` |

验收：

- 一条命令完成诊断/清码/升级；
- 断网 10 分钟，日志不丢；
- `systemd` 重启自动恢复；
- 故障注入下 3 s 内恢复通信。

---

## 8. 可靠性指标

| 指标 | 目标 | 测试方法 |
|---|---|---|
| 24h 丢帧率 | < 0.01% | 8.64M 帧，Seq 连续性 + candump |
| 端到端 P99 | < 50 ms | 时间戳/GPIO + 逻辑分析仪 |
| bus-off 恢复 | < 3 s | 拔线/短接/错误注入 |
| UDS 响应 | < 50 ms | `0x22` 请求/响应时间戳 |
| OTA 成功率 | 50/50 | 版本号 + 心跳确认 |
| 网关 CPU/RSS | < 30% / < 50 MB | `top`/`/proc` |

---

## 9. Definition of Done

- [ ] `a-baseline` tag，旧代码可运行
- [ ] ISO-TP 4 KB 多帧 0 错误
- [ ] UDS 5 服务全部可用
- [ ] 5 类 DTC 可注入、可读、可清
- [ ] Bootloader A/B 都能启动
- [ ] OTA 50 次 + 3 个掉电点
- [ ] 网关诊断/OTA CLI 可用
- [ ] 24h 测试报告 + demo 视频
- [ ] README 能让别人按步骤复现
- [ ] 简历 bullet 只写完成项 + 实测值

---

## 10. 不做

- 完整 AUTOSAR；
- 功能安全认证；
- 完整 UDS 全服务；
- 把简化 SecurityAccess 说成“安全方案”；
- 为了用满树莓派而堆三节点架构。
