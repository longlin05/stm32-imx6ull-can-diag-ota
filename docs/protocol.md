# CAN / ISO-TP / UDS / OTA 协议说明

> 状态：初稿。字段和 ID 在编码前可以调整，但一旦开始联调，变更要记录版本。
> 约定：多字节字段默认 **小端（little-endian）**；CAN 标准帧 11-bit ID；500 kbps；DLC=8（心跳除外）。

---

## 1. 报文 ID 分配

### 1.1 Baseline 应用帧（沿用 AImate）

| CAN ID | 方向 | 名称 | DLC | 周期/触发 | 说明 |
|---|---|---|---|---|---|
| `0x111` | STM32 → Linux | PIR 人体感应 | 8 | 翻转/5 s 保活 | event/state/raw/flags/timestamp |
| `0x112` | STM32 → Linux | 温湿度 | 8 | 2 s | temp/hum ×100、seq、status |
| `0x113` | STM32 → Linux | 心跳/状态 | 8 | 10 s | 版本、uptime、错误计数 |
| `0x221` | Linux → STM32 | 配置/动作命令 | 8 | 事件 | **已实现**，CRC16 |
| `0x222` | Linux → STM32 | 查询命令 | 8 | 事件 | **已实现**，CRC16 |
| `0x228` | STM32 → Linux | 命令 ACK | 8 | 事件 | **已实现**，按 frag 分片 |

Baseline 帧沿用 AImate，不重新定义；完整字段见 `firmware/reference/AImate/Docs/CAN_Protocol.md` 和 Linux 工程 `linux_quickstart.md`。

> `0x228` 的 ID 位域里源地址仍是 `0x2`（最初预留定义），实际发送方是 STM32；接收按完整 ID `0x228` 匹配，不要用源地址位判断方向。
> DHT11 分辨率 1 °C / 1 %RH；`0x112` 的 ×100 只是统一格式，不代表传感器精度。  
> 光照传感器未做，不进入协议。

### 1.1.1 命令帧 `0x221/0x222`

```text
offset 0   cmd       u8    命令码
offset 1   seq       u8    每条新命令 +1；重传复用同一 seq
offset 2..5 args     u8[4] 小端，未用填 0
offset 6..7 crc16    u16le CRC-16/CCITT-FALSE，覆盖 offset 0..5
```

CRC 参数：`poly=0x1021`、`init=0xFFFF`、不反射、`xorout=0`。

### 1.1.2 ACK 帧 `0x228`

```text
offset 0   cmd       u8    回显命令码
offset 1   seq       u8    回显 seq
offset 2   status    u8    0=OK / 1=未知命令 / 2=参数错误 / 3=不支持
offset 3   frag      u8    分片号，从 0 开始
offset 4..7 data     u8[4] 载荷分片；不足补 0
```

- 载荷按 4 字节分片；接收端按 `cmd + seq + frag` 重组。
- `0x12` 统计响应 11 字节 → 3 帧；`0x13` 配置 5 字节 → 2 帧。
- ACK 不带应用层 CRC，依赖 CAN 链路 CRC；命令帧带 CRC16。

### 1.1.3 命令与超时

| cmd | 帧 ID | 参数 | 应答长度 | 说明 |
|---|---|---|---|---|
| `0x10` | `0x222` | 无 | 2 | 版本：fw/proto |
| `0x11` | `0x222` | 无 | 4 | uptime 秒 |
| `0x12` | `0x222` | 无 | 11 | 节点统计 |
| `0x13` | `0x222` | 无 | 5 | 当前配置 |
| `0x20` | `0x221` | `args[0]` 0/1 | 1 | PIR 开关 |
| `0x21` | `0x221` | 周期 ms | 2 | 温湿度周期 500..60000 |
| `0x22` | `0x221` | 周期 ms | 2 | 心跳周期 1000..10000 |
| `0x23` | `0x221` | 无 | 0 | 清零统计，仍回一帧空 ACK |

- ACK 超时 150 ms；同一命令最多发 3 次（含首次），重传字节完全相同。
- 节点去重缓存深度 4，按 `cmd + seq` 命中后回放缓存 ACK，不重复执行。
- 实测 1000 条命令：1000/1000 成功，0 重试，P99 22.06 ms。

### 1.2 诊断帧（ISO-TP / UDS）

| CAN ID | 方向 | 用途 |
|---|---|---|
| `0x7E0` | Tester → ECU | 物理请求 |
| `0x7E8` | ECU → Tester | 物理响应 |
| `0x7DF` | Tester → ECU | 功能请求（可选） |

### 1.3 OTA 帧（A 方案：自定义 OTA）

| CAN ID | 方向 | 名称 | DLC | 说明 |
|---|---|---|---|---|
| `0x501` | Linux → STM32 | OTA Start | 8 | 大小/版本/块大小 |
| `0x502` | Linux → STM32 | OTA Data | 8 | offset + 5 字节数据 |
| `0x503` | STM32 → Linux | OTA ACK | 8 | next_offset/status |
| `0x504` | Linux → STM32 | OTA End | 8 | CRC32/版本/magic |

> 先做自定义 OTA，跑通后再考虑用 UDS `0x34/0x36/0x37` 包装。

---

## 2. ISO-TP（ISO 15765-2）

### 2.1 帧类型

| 类型 | PCI 高 nibble | 格式 | 用途 |
|---|---|---|---|
| SF | `0x0` | `0x0L` + data | 单帧，L=1..7 |
| FF | `0x1` | `0x1L` + 12-bit length + data | 多帧首帧 |
| CF | `0x2` | `0x2N` + data | 连续帧，N=0..15 循环 |
| FC | `0x3` | `0x3F` + FS + BS + STmin | 流控 |

### 2.2 参数（第一版）

| 参数 | 值 | 说明 |
|---|---|---|
| 物理请求 ID | `0x7E0` | Tester → ECU |
| 物理响应 ID | `0x7E8` | ECU → Tester |
| BlockSize | `8` | 每 8 帧等一次 FC |
| STmin | `10 ms` | 连续帧最小间隔 |
| N_Bs | `1000 ms` | 等 FC 超时 |
| N_Cr | `1000 ms` | 等 CF 超时 |
| 接收缓冲 | `≥ 4 KB` | 支持长响应 |

### 2.3 规则

- 多帧传输的 CF 序号从 `1` 开始，`0x2N` 的 N 循环 `0..15`。
- 接收方收到错序 CF，丢弃整包并计数。
- 发送方在 `N_Bs` 内没收到 FC，重发 FF，最多 3 次。
- 接收方缓冲不足，回 FC `OVFLW`。
- 所有帧不足 8 字节用 `0x00` 填充；填充不参与长度计算。

---

## 3. UDS 子集（A 方案）

### 3.1 通用格式

正响应：`<SID + 0x40> ...`

负响应：

```text
7F <SID> <NRC>
```

### 3.2 服务

#### `0x10` DiagnosticSessionControl

请求：

```text
10 <sessionType>
```

| sessionType | 含义 |
|---|---|
| `0x01` | 默认会话 |
| `0x03` | 扩展会话 |

正响应：

```text
50 <sessionType> <P2 high> <P2 low> <P2* high> <P2* low>
```

第一版可以把 P2/P2* 固定为 `00 32 01 F4`（50 ms / 5000 ms），也可以省略，但要在文档里写清楚。

#### `0x3E` TesterPresent

请求：

```text
3E 00
```

正响应：

```text
7E 00
```

规则：扩展会话 S3 超时 5 s；收到 `0x3E` 重置计时。

#### `0x22` ReadDataByIdentifier

请求：

```text
22 <DID high> <DID low>
```

正响应：

```text
62 <DID high> <DID low> <data...>
```

DID 表：

| DID | 含义 | 长度 | 说明 |
|---|---|---|---|
| `0xF186` | Active Session | 1 | `01/03` |
| `0x0100` | Software Version | 4 | major/minor/patch/build |
| `0x0101` | Uptime | 4 | 秒 |
| `0x0102` | CAN Error Counters | 4 | TX/RX/bus-off/error-passive |
| `0x0103` | Reset Reason | 1 | 上电/看门狗/软件复位 |
| `0x0104` | DTC Count | 1 | confirmed 数量 |

#### `0x27` SecurityAccess

只实现 level 1（seed/key 各 4 字节），且**只在扩展/编程会话里允许**。

请求 seed：

```text
27 01
```

正响应：

```text
67 01 <seed 4 字节，大端>
```

发送 key：

```text
27 02 <key 4 字节，大端>
```

正响应：

```text
67 02
```

key 由 seed 经固定变换得到（`uds_calc_key()`，演示用；量产应换成 HSM/产线密钥）：

```text
k = seed ^ 0x5A5A5A5A
k = rotl(k, 13)
k ^= 0xA5A5A5A5
k = k * 2654435761
key = k ^ 0x55AA55AA
```

规则：

```text
- 一次 seed 只能试一次 key（失败后必须重新 27 01）
- 连续 3 次失败 → 锁定 10 s，期间任何 key 都回 0x37
- 切会话 / S3 超时 → 自动上锁
- 已解锁时再请求 seed → 返回全 0 seed
```

#### `0x2E` WriteDataByIdentifier

请求：

```text
2E <DID high> <DID low> <data...>
```

正响应：

```text
6E <DID high> <DID low>
```

可写 DID（都需要先通过 `0x27`）：

| DID | 含义 | 长度 | 字节序 |
|---|---|---|---|
| `0xF1A0` | PIR 开关 | 1 | — |
| `0xF1A1` | 温湿度上报周期 ms | 2 | 小端（与 `22` 读出的顺序一致） |
| `0xF1A2` | 心跳周期 ms | 2 | 小端 |

其余 DID（VIN、版本、序列号等）只读，写回 `7F 2E 31`。

#### `0x19` ReadDTCInformation

请求（子功能 `0x02`）：

```text
19 02 <statusMask>
```

正响应：

```text
59 02 <statusMask> [DTC1(3) + status1(1)] [DTC2(3) + status2(1)] ...
```

DTC 采用 3 字节编码，例如 `0xC001` 表示为 `C0 01 00`。

#### `0x14` ClearDiagnosticInformation

请求：

```text
14 FF FF FF
```

正响应：

```text
54
```

规则：清除后 `0x19 02 FF` 返回空列表；清除动作写日志。

### 3.3 NRC

| NRC | 含义 |
|---|---|
| `0x11` | Service not supported |
| `0x12` | Sub-function not supported |
| `0x13` | Incorrect message length |
| `0x22` | Conditions not correct |
| `0x24` | Request sequence error（没有先取 seed 就发 key） |
| `0x31` | Request out of range |
| `0x33` | Security access denied（没解锁就写） |
| `0x35` | Invalid key |
| `0x36` | Exceed number of attempts（连续 3 次错 key） |
| `0x37` | Required time delay not expired（锁定 10 s 内） |
| `0x7E` | Sub-function not supported in active session |
| `0x7F` | Service not supported in active session |

---

## 4. DTC

| DTC | 含义 | 触发条件 | 恢复条件 |
|---|---|---|---|
| `0xC001` | DHT11 timeout / checksum | 单总线连续失败 3 次 | 连续成功 5 次 |
| `0xC002` | CAN bus-off | 检测到 bus-off | 恢复且收到有效帧 |
| `0xC003` | OTA CRC failure | CRC32 不匹配 | 下一次升级成功 |
| `0xC004` | Sensor out of range | 温湿度越界 | 回到范围并持续 10 s |
| `0xC005` | Flash error | 擦除/写入/校验失败 | 重新写入成功 |

状态位：

| Bit | 名称 | 含义 |
|---|---|---|
| 0 | testFailed | 当前测试失败 |
| 1 | testFailedThisCycle | 本周期失败过 |
| 2 | pending | 待确认 |
| 3 | confirmed | 已确认 |
| 7 | warningIndicator | 需要告警 |

存储：Config 区双备份，每条记录包含 `magic + seq + dtc[3] + status + crc`；启动时选序号最大且 CRC 正确的一份。

---

## 5. OTA 自定义协议

### 5.1 `0x501` OTA Start

| 字节 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0–3 | fw_size | u32 | 固件字节数 |
| 4–5 | version | u16 | 版本号 |
| 6 | chunk_size | u8 | 每帧数据字节数，第一版 5 |
| 7 | crc8 | u8 | 对字节 0–6 计算 |

### 5.2 `0x502` OTA Data

| 字节 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0–1 | offset | u16 | 相对固件起始的偏移 |
| 2–6 | data[5] | u8[5] | 数据 |
| 7 | crc8 | u8 | 对字节 0–6 计算 |

> 5 字节/帧比较保守，但实现简单。后续可改成 6 字节数据 + 1 字节序号 + 1 字节 CRC。

### 5.3 `0x503` OTA ACK

| 字节 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0–1 | next_offset | u16 | 下一帧期望偏移 |
| 2 | status | u8 | 见下表 |
| 3–6 | reserved | u8[4] | 置 0 |
| 7 | crc8 | u8 | 对字节 0–6 计算 |

状态码：

| 值 | 含义 |
|---|---|
| `0x00` | OK |
| `0x01` | CRC 错 |
| `0x02` | offset 错 |
| `0x03` | Flash 错 |
| `0x04` | 版本不支持 |
| `0x05` | 忙 |

### 5.4 `0x504` OTA End

| 字节 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0–3 | crc32 | u32 | 整包 CRC32 |
| 4–5 | version | u16 | 版本号 |
| 6–7 | magic | u16 | `0x5A5A` |

### 5.5 时序

```text
Linux                          STM32
  | -- 0x501 start -------------> |
  | <-- 0x503 ACK ----------------|
  | -- 0x502 data(offset=0) -----> |
  | <-- 0x503 ACK(next=5) --------|
  | -- 0x502 data(offset=5) -----> |
  | <-- 0x503 ACK(next=10) -------|
  | ...                           |
  | -- 0x504 end ----------------> |
  | <-- 0x503 ACK ----------------|
  |          STM32 校验/写 pending/重启
  |          Bootloader 试运行/确认/回滚
```

超时与重试：

- 每帧等 ACK 100 ms，超时重发 3 次；
- 连续 3 次失败，中止升级并记录 DTC `0xC003`；
- 升级期间心跳可暂停，但网关要显示进度；
- 试运行 5 s 内应用必须 confirm，否则回滚。

---

## 6. 示例帧

### 6.1 读扩展会话

```text
请求：7E0#0210030000000000
响应：7E8#0250030000000000
```

### 6.2 读当前会话 / 软件版本

```text
# 读当前会话（DID F186）
请求：7E0#0322F18600000000
响应：7E8#0462F18603000000

# 读软件版本（DID 0100，示例版本 1.2.3 build 0）
请求：7E0#0322010000000000
响应：7E8#0762010001020300
```

### 6.3 读 DTC

```text
请求：7E0#031902FF00000000
响应：7E8#075902FFC0010009  （示例：1 条 DTC + status=0x09）
```

> 多条 DTC 时响应超过 7 字节，走 ISO-TP 多帧。

### 6.4 清 DTC

```text
请求：7E0#0414FFFFFF000000
响应：7E8#0154000000000000
```

---

## 7. 兼容与版本

- Baseline 帧 `0x111/0x112/0x113` 沿用 AImate，不改字段；`0x221/0x222/0x228` 已实现双向命令/ACK。
- 新协议从 `v0.1` 开始；字段变更必须更新本文件并写 changelog。
- 命令帧 `0x221/0x222` 用 CRC-16/CCITT-FALSE（poly `0x1021`，init `0xFFFF`）；OTA 自定义帧用 CRC8（poly `0x07`，init `0x00`），两者不要混用。
- DHT11 的校验和是前 4 字节相加取低 8 位，**不是 CRC**，不要和 CAN 协议 CRC 混用。
- DHT11 实际采样间隔建议 ≥ 2 s；`0x112` 就是 2 s 一帧。
- 通信层验收：1000 条命令 1000/1000，0 重试，P99 22.06 ms；重复 `seq` 命中缓存，`dup_cmd +1`。
