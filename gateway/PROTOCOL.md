# Gateway 诊断协议（Linux ↔ STM32 字节级规范）

> 本文是 [DESIGN.md](DESIGN.md) 的字节级落地，是 STM32 固件 HAL 侧实现的**唯一参照**。
> 本侧（Linux）为格式权威，STM32 照此实现，只复用协议逻辑、不复用 socket 代码。
> 多字节字段一律**小端**；与 my_ai 既有协议保持一致。

---

## 1. 总线与寻址

| 项 | 值 |
|---|---|
| 波特率 | 500 kbps |
| 帧类型 | 11 位标准数据帧 |
| 寻址 | Normal addressing（PDU 直接从 PCI 字节开始） |
| 请求 ID | `0x7E0`（tester → ECU） |
| 响应 ID | `0x7E8`（ECU → tester） |
| Functional / Extended addressing | 不做 |

---

## 2. ISO-TP 帧格式（ISO 15765-2，经典 8 字节帧）

PCI 在每帧**第 0 字节**，后面紧跟数据；不足 8 字节一律用 `0xCC` padding。

### 2.1 单帧 Single Frame（SF）

```
字节0  0x0N       N = 数据长度(1..7)
字节1..N         N 字节数据
字节N+1..7       0xCC padding
```

### 2.2 首帧 First Frame（FF）

```
字节0  0x1X       X = 12 位总长的高 4 位
字节1  0xLL       12 位总长的低 8 位（总长 ≤ 4095）
字节2..7         前 6 字节数据
```

### 2.3 续帧 Consecutive Frame（CF）

```
字节0  0x2N       N = 序号 0..15，回绕（0xF 后回 0x0）
字节1..7         7 字节数据（末帧不足补 0xCC）
```

### 2.4 流控 Flow Control（FC）

```
字节0  0x30/0x31/0x32   0x30=CTS 继续 / 0x31=WT 等待 / 0x32=OVFLW 溢出
字节1  BlockSize(BS)
字节2  STmin
字节3..7                0xCC padding
```

---

## 3. ISO-TP 参数与超时

| 参数 | 值 |
|---|---|
| BS | `0`（接收方不回 FC 限块，发送方一次发完所有 CF） |
| STmin | `0` ms |
| N_Bs（发送方等 FC 超时） | `1000` ms |
| N_Cr（接收方等 CF 超时） | `1000` ms |
| padding | `0xCC` |

> 本工程 `BS=0`，所以 FC 实际上只出现一次（首帧后），随后发送方连续发 CF，不发第二批。

---

## 4. UDS 通用约定（ISO 14229-1）

- 请求 = `SID + 参数`；正响应 = `SID|0x40 + 参数回显`；负响应 = `7F + SID + NRC`。
- 请求/响应 PDU 用第 2 节的 SF/FF/CF/FC 承载：**PDU ≤ 7 字节用 SF，> 7 字节用 FF + CF**。
- 一个 PDU 的帧序列：FF →（接收方回一个 FC `30 00 00`）→ 连续 CF。发送方按 `0x7E0`，接收方按 `0x7E8`。

### 4.1 负响应码 NRC（本项目用到）

| NRC | 名称 | 触发 |
|---|---|---|
| `0x11` | serviceNotSupported | 收到 `0x27` 等不支持的服务 |
| `0x12` | subFunctionNotSupported | 不支持的子功能/会话类型 |
| `0x13` | incorrectMessageLengthOrInvalidFormat | 长度/格式不符 |
| `0x22` | conditionsNotCorrect | 未在正确会话（如未进编程会话就发 `0x34`） |
| `0x31` | requestOutOfRange | 未知 DID、参数越界 |
| `0x72` | generalProgrammingFailure | FOTA CRC 不符、写分区失败 |
| `0x78` | requestCorrectlyReceived-ResponsePending | 操作需时，网关应等待后重查 |

### 4.2 UDS 时序（网关侧超时）

| 参数 | 值 | 说明 |
|---|---|---|
| P2 | `1000` ms | 普通请求的响应超时 |
| P2* | `5000` ms | FOTA（`0x34/0x36/0x37/0x31`）响应超时 |

收到 `0x78` 时网关等待后重发 `0x3E` 或重查，最长不超过 P2*。

---

## 5. 服务逐条定义

### 5.1 `0x10` DiagnosticSessionControl 会话控制

| 请求 | 字节 |
|---|---|
| SID | `10` |
| sessionType | `01` 默认 / `02` 编程 / `03` 扩展 |

正响应：`50 <sessionType>`

> FOTA 前必须先进入编程会话 `0x02`。会话 5s 无 `0x3E` 自动回默认。

### 5.2 `0x3E` TesterPresent 保活

| 请求 | 字节 |
|---|---|
| SID | `3E` |
| sub | `00` |

正响应：`7E 00`

### 5.3 `0x11` ECUReset 复位

| 请求 | 字节 |
|---|---|
| SID | `11` |
| resetType | `01` 硬复位 / `03` 软复位 |

正响应：`51 <resetType>`

> FOTA 切换 A/B 后网关发 `11 01` 硬复位进新固件。

### 5.4 `0x22` ReadDataByIdentifier 读标识

| 请求 | 字节 |
|---|---|
| SID | `22` |
| DID | 2 字节 |

正响应：`62 <DID 2B> <dataRecord...>`

DID 表见第 6 节。未知 DID → NRC `0x31`。

### 5.5 `0x2E` WriteDataByIdentifier 写标识

| 请求 | 字节 |
|---|---|
| SID | `2E` |
| DID | 2 字节 |
| dataRecord | 按第 6 节类型 |

正响应：`6E <DID 2B>`

### 5.6 `0x19` ReadDTCInformation 读 DTC

子功能（sub 第 2 字节）：

| sub | 请求 | 正响应 |
|---|---|---|
| `01` | `19 01 <statusMask>` | `59 01 <statusMask> <DTCFormatID=01> <count 2B LE>` |
| `02` | `19 02 <statusMask>` | `59 02 <statusMask> [DTC 3B]×N` |
| `04` | `19 04 <DTC 3B> <recNo=01>` | `59 04 <DTC 3B> <statusOfDTC> <recNo=01> <冻结帧 9B>` |
| `06` | `19 06 <DTC 3B> <recNo=01>` | `59 06 <DTC 3B> <statusOfDTC> <recNo=01> <扩展数据 6B>` |

DTC = 3 字节标准码（2B 码 + 1B status）。冻结帧 / 扩展数据布局见第 7 节。

### 5.7 `0x14` ClearDiagnosticInformation 清 DTC

| 请求 | 字节 |
|---|---|
| SID | `14` |
| groupOfDTC | 3 字节，`FF FF FF` = 全部 |

正响应：`54`

### 5.8 `0x31` RoutineControl 例程控制

| 请求 | 字节 |
|---|---|
| SID | `31` |
| routineControlType | `01` 启动 / `03` 取结果 |
| routineIdentifier | 2 字节 |

| RID | 类型 | 说明 |
|---|---|---|
| `0x3100` | `01` 启动 | 提交非活动分区并切换 A/B |

启动请求：`31 01 31 00` → 正响应 `71 01 31 00 <result 1B>`（`01`=成功 / `00`=失败）。
> 本项目把结果直接随正响应返回，作为简化（与「本侧为权威」一致）。

### 5.9 `0x34` RequestDownload 请求下载（FOTA 起始）

| 请求 | 字节 |
|---|---|
| SID | `34` |
| dataFormatIdentifier | `00`（未压缩） |
| addressAndLengthFormatIdentifier | `44`（地址 4B + 长度 4B） |
| memoryAddress | 4 字节 LE，非活动分区起始地址 |
| memorySize | 4 字节 LE，镜像字节数 |

正响应：`74 20 <maxBlockLen 2B LE>`，本项目恒为 `1024`（`00 04`）。

### 5.10 `0x36` TransferData 传输数据

| 请求 | 字节 |
|---|---|
| SID | `36` |
| blockSequenceCounter | 1 字节，1 起始，0xFF 回绕 0x00 |
| data | 1024 字节（末块按实际长度） |

正响应：`76 <blockSequenceCounter 1B>`

> 单次 `0x36` PDU = 1 + 1 + 1024 = 1026 字节，走 FF + 146 个 CF（见第 8 节示例）。

### 5.11 `0x37` RequestTransferExit 结束传输

| 请求 | 字节 |
|---|---|
| SID | `37` |
| CRC32 | 4 字节 LE，整镜像 CRC-32/ISO-HDLC |

正响应：`77`（校验通过）。CRC 不符 → NRC `0x72`。

### 5.12 `0x27` SecurityAccess 安全访问

v1 不支持：ECU 收到返回 NRC `0x11`。

---

## 6. DID 表

| DID | 名称 | 长度 | 类型 | 访问 | 说明 |
|---|---|---|---|---|---|
| `0xF190` | VIN | 17 | ASCII | R | 标准 VIN |
| `0xF180` | 固件版本 | 1 | BCD | R | 高4=主 低4=次，如 `0x10`=v1.0 |
| `0xF181` | 协议版本 | 1 | BCD | R | 本项目 UDS 协议版本，当前 `0x10`=v1.0 |
| `0xF182` | 标定 ID | 4 | uint32 LE | R | 标定数据版本号 |
| `0xF183` | 序列号 | 8 | ASCII | R | 硬件序列号（可选） |
| `0xF1A0` | PIR 开关 | 1 | uint8 | R/W | `0`=关 `1`=开 |
| `0xF1A1` | 温湿度周期 | 2 | uint16 LE | R/W | ms，500..60000 |
| `0xF1A2` | 心跳周期 | 2 | uint16 LE | R/W | ms，1000..10000 |

> 新增配置项按 `0xF1Ax` 顺序追加。未知 DID → NRC `0x31`。

---

## 7. DTC 冻结帧 / 扩展数据记录布局

**冻结帧（9 字节，每个 DTC 一份快照）**

| 偏移 | 长度 | 字段 | 类型 |
|---|---|---|---|
| 0..1 | 2 | 温度 | int16 LE，0.01 ℃ |
| 2..3 | 2 | 湿度 | uint16 LE，0.01 %RH |
| 4..7 | 4 | uptime | uint32 LE，秒 |
| 8 | 1 | PIR 状态 | uint8，`0`/`1` |

**扩展数据（6 字节）**

| 偏移 | 长度 | 字段 | 类型 |
|---|---|---|---|
| 0..1 | 2 | 发生次数 | uint16 LE |
| 2..5 | 4 | 最近发生 uptime | uint32 LE，秒 |

---

## 8. FOTA 完整时序

```
1. 0x10 进入编程会话(0x02)
2. 0x34 RequestDownload(非活动分区地址, 镜像长度)   → 确认 maxBlockLen=1024
3. 0x36 TransferData(块序号, 1024B) × N              → 每块回显序号
4. 0x37 RequestTransferExit(整镜像 CRC32)            → CRC 不符返回 NRC 0x72
5. 0x31 启动 0x3100(提交并切换 A/B)                  → 返回结果 0x01
6. 0x11 ECUReset(硬复位 0x01)                        → 进新固件
```

> 期间网关周期性发 `0x3E` 保活；收到 `0x78` 则等待后重查。

---

## 9. 联调命令（cansend / candump）

观察总线：

```bash
candump -ta can0                        # 全部帧
candump -ta can0,7E0:7FF                # 只看请求
candump -ta can0,7E8:7FF                # 只看响应
```

示例（字节串为示例，正式以工具实际输出为准）：

```bash
# 1) 读固件版本 DID=0xF180：请求 PDU = 22 F1 80 (3B) → SF
cansend can0 7E0#0322F180CCCCCCCC
# 期望响应 7E8#0462F18010CCCCCC   → 62 F1 80 10 = v1.0

# 2) 进入编程会话 0x02
cansend can0 7E0#021002CCCCCCCCCC
# 期望响应 7E8#025002CCCCCCCCCC   → 50 02

# 3) 保活
cansend can0 7E0#023E00CCCCCCCCCC
# 期望响应 7E8#027E00CCCCCCCCCC   → 7E 00

# 4) 读 DTC 数量 (sub=01, mask=FF)
cansend can0 7E0#031901FFCCCCCCCC
# 期望响应 7E8#065901FF010002CC   → 59 01 FF 01 00 02 = 2 个 DTC
```

### 多帧示例：`0x34`（PDU 11 字节）

请求 PDU = `34 00 44 00 00 04 08 00 00 04 00`（地址 `0x08040000`、长度 `0x00040000`），总长 11：

```
FF:  10 0B 34 00 44 00 00 04      (0x1 0x0B + 前 6 字节数据)
FC:  30 00 00 CC CC CC CC CC      (ECU 回，BS=0 STmin=0)
CF:  21 08 00 00 04 00 CC CC      (序号 0x1 + 剩余 5 字节数据)
```

### 多帧示例：`0x36`（PDU 1026 字节，1024 字节数据块）

总长 1026 = `0x0402`：

```
FF:  14 02 36 01 <data0..3>        (0x1 0x4 0x02 + 前 6 字节)
FC:  30 00 00 CC CC CC CC CC
CF:  21 <data4..9 共7B>            序号 1
CF:  22 <...>                      序号 2
...
CF:  2x ...                        序号 0..15 回绕，共 146 帧
```

---

## 10. CRC-32 / ISO-HDLC 参考

即标准 zlib / Ethernet CRC32：

```
poly   = 0x04C11DB7 (正常式) / 0xEDB88320 (反射式)
init   = 0xFFFFFFFF
refin/refout = true
xorout = 0xFFFFFFFF
校验串 "123456789" → 0xCBF43926
```

网关对**整镜像**（`0x34` 里 memorySize 指明的字节范围）算 CRC，4 字节 LE 放在 `0x37` 载荷；ECU 收完全部块后重算比对。

---

## 11. 实现检查清单

- [ ] 所有帧 DLC=8，PCI 在字节 0，不足补 `0xCC`。
- [ ] 多字节字段小端。
- [ ] 请求按 `0x7E0` 收、`0x7E8` 发（ECU 视角），不做 functional/extended addressing。
- [ ] PDU ≤ 7 用 SF，> 7 用 FF+CF；FF 后回一个 FC `30 00 00`。
- [ ] CF 序号 0..15 回绕；N_Cr=1000ms 收不到下一 CF 判超时。
- [ ] 正响应 = `SID|0x40`；负响应 = `7F SID NRC`。
- [ ] FOTA 前进入编程会话 `0x02`；`0x34` 参数 `00 44` + 地址/长度 4B LE。
- [ ] `0x36` 块序号 1 起始、0xFF 回绕；`0x37` 带整镜像 CRC32 4B LE。
- [ ] 未知 DID/越界 → NRC `0x31`；未进编程会话就下载 → NRC `0x22`；CRC 不符 → NRC `0x72`。
- [ ] 收到 `0x27` → NRC `0x11`。

---

## 12. 变更记录

| 版本 | 日期 | 内容 |
|---|---|---|
| v0.1 | 2026-11-03 | 初版：ISO-TP 帧格式 / UDS 服务逐条 / DID 表 / DTC 记录 / FOTA 时序 / 联调示例 |
