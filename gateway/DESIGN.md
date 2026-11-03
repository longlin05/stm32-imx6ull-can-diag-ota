# Gateway 诊断/升级网关 — 设计文档

> 状态：**ISO-TP 核心已完成（BS=0/STmin=0），UDS/FOTA 未开始**。
> 本文是 `my_ai` 的延伸项目 `gateway` 的设计契约，也是后续 Linux 端实现与 STM32 固件联调的基准。

---

## 1. 目标与范围

一个跑在 **imx6ull** 上的诊断/升级网关：

- **南向**：在 CAN 上实现 **ISO-TP（ISO 15765-2）+ UDS（ISO 14229）**，把诊断、配置、升级、日志回传变成可调用的能力。
- **北向**：第一版只暴露 **CLI + JSON 日志**，不做 MQTT / Web / 常驻服务化接口。

**v1 做**：

| 类别 | 内容 |
|---|---|
| 传输 | 用户态自实现 ISO-TP（SF/FF/CF/FC 状态机），不依赖内核模块 |
| UDS | 读标识、读写配置/标定、读/清 DTC、读冻结帧/扩展数据、固件升级 FOTA、复位 |
| 北向 | CLI 子命令 + 每步 JSON 日志（可机器解析） |

**v1 不做**：

- MQTT / Web / 云端接入（留到后续）
- 内核态 `can-isotp`
- functional / extended addressing
- 新旧协议桥（my_ai 的 legacy 协议原样保留，与本项目并存、ID 隔离）
- FOTA 断点续传

---

## 2. 架构分层

这是「STM32 复用协议逻辑、不复用 socket 代码」的直接落地：ISO-TP 核心与 UDS 层写成**平台无关的纯 C**，只通过「收发/定时」一组回调与平台交互。

```
北向   CLI + JSON 日志       gw read-version / read-dtc / upgrade fw.bin ...
─────────────────────────────────────────────────────────────
UDS   服务层 (ISO 14229)    会话/保活/读标识/读写配置/DTC/FOTA/复位
─────────────────────────────────────────────────────────────
ISO-TP 传输核心 (15765-2)   SF/FF/CF/FC 状态机 —— 纯C、零平台依赖，
                            只通过「收发/定时」回调与平台交互
─────────────────────────────────────────────────────────────
平台适配层                  Linux: socketcan(raw)   STM32: HAL CAN 实现同一组回调
```

- Linux 侧复用 my_ai 里 `can.c` 的 socketcan 封装思路（拉起接口 / open / read / write / poll）。
- STM32 侧用 HAL CAN 实现同一组回调，跑**同一份状态机代码**，只有适配层不同。

---

## 3. CAN 帧层与寻址

| 项 | 决定 |
|---|---|
| 帧类型 | 11 位标准帧（非扩展） |
| 寻址方式 | Normal addressing（PDU 直接从 PCI 字节开始，无地址字节） |
| 寻址类型 | Physical（点对点，单 ECU） |
| 请求 ID | `0x7E0`（tester → ECU） |
| 响应 ID | `0x7E8`（ECU → tester） |
| Functional | 不做（无 `0x7DF` 广播） |
| Extended addressing | 不做 |

- `0x7E0` / `0x7E8` 与 my_ai 的 `0x111/0x112/0x113/0x221/0x222/0x228` **不冲突、可共线**。
- 单 ECU + Physical → 无多源并发应答，状态机简单。
- 数据字段分配（经典 8 字节帧）：
  - **SF** 单帧：`0x0N` + N 字节数据（N ≤ 7）
  - **FF** 首帧：`0x1x xx` + 6 字节数据，12 位总长（≤ 4095）
  - **CF** 续帧：`0x2N` + 7 字节数据（N = 0..15 回绕）
  - **FC** 流控：`0x30 / 0x31 / 0x32` + BS + STmin

---

## 4. ISO-TP 传输参数（保守）

| 参数 | 值 |
|---|---|
| BlockSize (BS) | `0`（不流控块，CF 连续发） |
| STmin | `0` ms |
| N_Bs（发送方等 FC 超时） | `1000` ms |
| N_Cr（接收方等 CF 超时） | `1000` ms |
| padding | `0xCC` |

> 两端（Linux / STM32）必须严格一致。`BS=0` 表示发送方一次性把所有 CF 发完、不等 FC；`N_Bs`/`N_Cr` 用标准 1000ms 是保守取值。

---

## 5. UDS 服务清单

| SID | 服务 | 用途 | v1 |
|---|---|---|---|
| `0x10` | DiagnosticSessionControl | 默认/扩展/编程会话 | ✅ |
| `0x11` | ECUReset | 升级/标定后复位 | ✅ |
| `0x14` | ClearDiagnosticInformation | 清 DTC | ✅ |
| `0x19` | ReadDTCInformation | DTC 列表、冻结帧、扩展数据 | ✅ |
| `0x22` | ReadDataByIdentifier | 读标识（版本/VIN/标定ID/配置） | ✅ |
| `0x2E` | WriteDataByIdentifier | 写配置/标定数据 | ✅ |
| `0x31` | RoutineControl | 升级后校验/提交/切换 A/B | ✅ |
| `0x34` | RequestDownload | FOTA 起始（地址/长度） | ✅ |
| `0x36` | TransferData | FOTA 数据块传输 | ✅ |
| `0x37` | RequestTransferExit | FOTA 结束 | ✅ |
| `0x3E` | TesterPresent | 保活，防会话超时 | ✅ |
| `0x27` | SecurityAccess | 写配置/升级前解锁 | ⚠️ 先留空，后补 |

- 负响应：`0x7F + SID + NRC`。
- DID：VIN 用标准 `0xF190`；固件版本、协议版本、标定 ID、配置项用**项目自定义 DID**（编号表见第 9.1 节，本侧为权威）。

---

## 6. FOTA（走 UDS）

| 项 | 决定 |
|---|---|
| 块大小 | `1024` 字节/块 |
| 校验 | **CRC-32 / ISO-HDLC** |
| 分区 | **A/B 双分区**：写非活动分区 → 校验 → 切换启动分区 |
| 续传 | 第一版**不做**（失败从头传） |

**CRC-32 / ISO-HDLC**：即标准 zlib/Ethernet CRC32 —— 反射多项式 `0xEDB88320`
（正常式 `0x04C11DB7`），init `0xFFFFFFFF`，xorout `0xFFFFFFFF`，
校验串 `"123456789"` → `0xCBF43926`。

**Linux 侧流程**：

```
0x10 进入编程会话
0x34 RequestDownload(地址/长度)         → 地址指向非活动分区
0x36 TransferData(块序号 + 1024 字节)   → 循环，序号 1..N
0x37 RequestTransferExit(整镜像 CRC32)  → 结束传输，带上整镜像 CRC
0x31 RoutineControl(校验/提交/切换 A/B) → ECU 重算 CRC、提交、切启动分区
0x11 ECUReset                           → 复位进新固件
```

> 整镜像 CRC32 由网关算好下发，ECU 收完全部块后重算比对、通过才提交并切换 A/B；
> CRC 下发位置与例程号见第 9.4 / 9.3 节。

---

## 7. 北向接口（CLI + JSON）

单二进制 `gw`，子命令式，每步向 stdout 输出一行 JSON：

| 子命令 | 作用 |
|---|---|
| `gw read-version` | 读固件/协议版本 |
| `gw read-vin` | 读 VIN（`0xF190`） |
| `gw read-calib` | 读标定 ID |
| `gw read-config <did>` | 读配置/标定数据 |
| `gw write-config <did> <value>` | 写配置/标定数据 |
| `gw read-dtc` | 读 DTC 列表 + 冻结帧/扩展数据 |
| `gw clear-dtc` | 清 DTC |
| `gw upgrade <fw.bin>` | FOTA 升级 |
| `gw reset` | 复位 ECU |

> 子命令名与参数为提案，实现阶段可调整；JSON 字段 schema 实现时再定。

---

## 8. 验证顺序

1. **Linux 侧先跑通**：用 `vcan` 虚拟总线 + 一个自测对端（最小 mock ECU）验证 ISO-TP 状态机 —— 单帧、多帧、CF 序号回绕、FC 超时（N_Bs/N_Cr）、padding。
2. **UDS 层自测**：mock ECU 回固定应答，覆盖每个 SID 的正常/负响应。
3. **FOTA 自测**：mock ECU 收块、算 CRC32，验证 1024 字节分块与 A/B 提交流程。
4. **上 STM32 联调**：STM32 参照同一状态机实现（复用协议逻辑，不复用 socket），逐项联调。

---

## 9. 已定义的格式（STM32 参照实现，本侧为权威）

> STM32 固件尚未开始编码，以下格式由 Linux 侧定义，STM32 照此实现。
> 多字节字段一律小端（与 my_ai 既有协议一致）。

### 9.1 DID 表（`0x22` 读 / `0x2E` 写）

| DID | 名称 | 长度 | 类型 | 访问 | 说明 |
|---|---|---|---|---|---|
| `0xF190` | VIN | 17 | ASCII | R | 标准 VIN |
| `0xF180` | 固件版本 | 1 | BCD | R | 高4=主版本 低4=次版本，如 `0x10`=v1.0 |
| `0xF181` | 协议版本 | 1 | BCD | R | 本项目 UDS 协议版本，当前 `0x10`=v1.0 |
| `0xF182` | 标定 ID | 4 | uint32 LE | R | 标定数据版本号 |
| `0xF183` | 序列号 | 8 | ASCII | R | 硬件序列号（可选） |
| `0xF1A0` | PIR 开关 | 1 | uint8 | R/W | `0`=关 `1`=开 |
| `0xF1A1` | 温湿度周期 | 2 | uint16 LE | R/W | ms，500..60000 |
| `0xF1A2` | 心跳周期 | 2 | uint16 LE | R/W | ms，1000..10000 |

> 可扩展：新增配置/标定项按 `0xF1Ax` 顺序追加；网关收到未知 DID 返回 NRC `0x31`(requestOutOfRange)。

### 9.2 会话与保活（`0x10` / `0x3E`）

- 会话：`0x01` 默认 / `0x02` 编程 / `0x03` 扩展。FOTA 须先进入编程会话 `0x02`。
- 会话超时：默认 5s 内无 `0x3E` 自动回默认会话；网关在长操作期间周期性发 `0x3E` 保活。

### 9.3 RoutineControl 例程号（`0x31`）

| RID | 类型 | 说明 |
|---|---|---|
| `0x3100` | `0x01` 启动 | 提交非活动分区并切换 A/B 启动分区；结果 `0x01`=成功 |

> 升级失败自动回滚（A/B 回退）不在 v1 范围。

### 9.4 FOTA 参数与 CRC 位置

- `0x34` RequestDownload：`dataFormatIdentifier=0x00`（未压缩），`addressAndLengthFormatIdentifier=0x44`（地址 4B + 长度 4B），`memoryAddress`=非活动分区起始地址，`memorySize`=镜像字节数。
- `0x36` TransferData：`blockSequenceCounter`(1B，1 起始，0xFF 回绕 0x00) + 1024 字节数据（末块按实际长度）。
- `0x37` RequestTransferExit：`transferRequestParameterRecord` = **整镜像 CRC32/ISO-HDLC，4 字节 LE**。ECU 收完全部块后重算比对，不符返回负响应 NRC `0x72`(generalProgrammingFailure)。
- `0x31` RID `0x3100` 启动：校验通过后提交并切换 A/B。

### 9.5 SecurityAccess（`0x27`）

v1 不启用：网关不发 `0x27`；ECU 收到 `0x27` 返回负响应 NRC `0x11`(serviceNotSupported)。写配置/升级在 v1 无需解锁。

### 9.6 DTC 与冻结帧 / 扩展数据

- **DTC 码**：标准 3 字节（2 字节 DTC 码 + 1 字节 DTCStatusAvailabilityMask），SAE J2012 风格；网关按 3 字节码透传，不做码义解释，DTC 码表由 STM32 侧按故障源枚举。
- **`0x19` 子功能**：`0x01` 读 DTC 数量、`0x02` 读 DTC 列表、`0x04` 读冻结帧、`0x06` 读扩展数据；请求/响应的字节级语义按 ISO 14229-1，项目自定义字段如下。
- **冻结帧记录**（每个 DTC 的固定长度快照，偏移即字节序）：
  | 偏移 | 长度 | 字段 | 类型 |
  |---|---|---|---|
  | 0..1 | 2 | 温度 | int16 LE，0.01 ℃ |
  | 2..3 | 2 | 湿度 | uint16 LE，0.01 %RH |
  | 4..7 | 4 | uptime | uint32 LE，秒 |
  | 8 | 1 | PIR 状态 | uint8，`0`/`1` |
- **扩展数据记录**：
  | 偏移 | 长度 | 字段 | 类型 |
  |---|---|---|---|
  | 0..1 | 2 | 发生次数 | uint16 LE |
  | 2..5 | 4 | 最近发生 uptime | uint32 LE，秒 |

---

## 10. 变更记录

| 版本 | 日期 | 内容 |
|---|---|---|
| v0.1 | 2026-11-03 | 初版：目标/架构/寻址/ISO-TP 参数/UDS 清单/FOTA/北向/验证顺序 |
| v0.2 | 2026-11-03 | 第 9 节由「待定项」改为「已定义格式」：DID 表 / 会话保活 / 例程号 / CRC 位置 / 安全策略 / DTC 格式 |
