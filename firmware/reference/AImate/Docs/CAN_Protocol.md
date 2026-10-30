# AImate 节点 CAN 通信协议

> 本文档面向 Linux 端的开发者，是 STM32（AImate）节点与 Linux 主机之间 CAN 通信的完整规范。
> 协议在 MCU 端的规范性定义位于 `Core/CustomDrivers/can_protocol.h`，两者必须保持一致。

---

## 1. 概述

STM32F407（AImate 节点）采集人体感应与温湿度数据，经 CAN 总线周期/事件驱动地上报给 Linux 主机。

当前已实现 **STM32 → Linux 单向**的数据上报：

| 数据 | 帧 ID | 时机 |
|---|---|---|
| 人体感应 | `0x111` | 状态翻转时立即上报，另外每 5 s 保活一帧 |
| 温湿度 | `0x112` | 每 2 s 一帧 |
| 心跳 / 状态 | `0x113` | 每 10 s 一帧 |

Linux → STM32 方向（`0x221` / `0x222` / `0x228`）**已在 ID 分配中预留、帧格式已定义，但 MCU 端尚未实现配置生效逻辑**，见第 10 节。

### 1.1 总线参数

| 项目 | 取值 |
|---|---|
| 波特率 | 500 kbps |
| 帧类型 | 标准帧（11 位 ID，非扩展帧） |
| 帧格式 | 数据帧（RTR = 0） |
| DLC | 固定 8 |
| 位定时 | 预分频 6，BS1 = 11 TQ，BS2 = 2 TQ，SJW = 1 TQ（共 14 TQ） |
| 终端电阻 | 总线两端各 120 Ω |

总线负载：约 4 帧/秒，每帧最长 135 位，合计约 **0.11 % @ 500 kbps**，余量充足，后续提高上报频率不需要改协议。

### 1.2 节点地址

| 地址 | 节点 |
|---|---|
| `0x0` | 广播 |
| `0x1` | STM32 / AImate 节点 |
| `0x2` | Linux 主机 |

---

## 2. CAN ID 分配

11 位标准 ID 按下述位域划分，**数值越小优先级越高**（CAN 仲裁规则：显性 0 胜出），因此优先级放在高位。

```
 bit  10  9  8 |  7  6  5  4 |  3  2  1  0
     ┌─────────┬─────────────┬─────────────┐
     │  优先级  │   源地址     │  消息类型    │
     │  3 bit  │   4 bit     │   4 bit     │
     └─────────┴─────────────┴─────────────┘
```

**优先级（bit10..8）**

| 值 | 含义 |
|---|---|
| 0 | 紧急（预留，未使用） |
| 1 | 传感器数据 |
| 2 | 配置 / 命令 |

**源地址（bit7..4）**：见 1.2 节。

**消息类型（bit3..0）**：见下表。

### 2.1 已分配的 ID

| ID | 优先级 | 源 | 类型 | 方向 | 消息 |
|---|---|---|---|---|---|
| `0x111` | 1 | 0x1 | 0x1 | STM32 → Linux | 人体感应 |
| `0x112` | 1 | 0x1 | 0x2 | STM32 → Linux | 温湿度 |
| `0x113` | 1 | 0x1 | 0x3 | STM32 → Linux | 心跳 / 状态 |
| `0x221` | 2 | 0x2 | 0x1 | Linux → STM32 | 配置写入（预留） |
| `0x222` | 2 | 0x2 | 0x2 | Linux → STM32 | 查询请求（预留） |
| `0x228` | 2 | 0x2 | 0x8 | Linux → STM32 | 应答 / ACK（预留） |
| `0x1Fx` | 1 | 0x1 | 0xF | STM32 → Linux | 多帧分片传输（预留） |

用掩码筛选某一类帧很方便：`candump can0,0x112:0x7FF` 精确匹配温湿度帧。

---

## 3. 通用约定

### 3.1 字节序

**所有多字节字段一律小端（Little-Endian）**，即低字节在低偏移处。
这与 ARM 和 x86 的原生字节序一致，`struct.unpack_from('<h', ...)` / `memcpy` 到 `int16_t` 都不需要额外转换。

> 注意：`candump -x` 或 `-L` 打印的字节顺序就是**线上顺序**，直接按偏移取即可。
> 不要用 C 的结构体指针直接强转 CAN 数据缓冲区——不同编译器的结构体填充规则可能引入偏差，请按偏移逐字段解析（第 8 节给出了现成代码）。

### 3.2 DLC

**所有上行帧的 DLC 固定为 8**，不足的字节补 0。
接收端应校验 `can_dlc == 8`，不符的帧计入错误计数并丢弃，不要按残缺数据解析。

### 3.3 数值缩放

温度、湿度都以**定点整数**传输，避免浮点带来的跨平台差异：

| 字段 | 类型 | 缩放 | 实际值 = 原始值 ÷ |
|---|---|---|---|
| 温度 | `int16` | 0.01 °C | 100 |
| 湿度 | `uint16` | 0.01 %RH | 100 |

例：温度字段读到 `9809`（小端字节 `98 09`，即十进制 2456）→ 2456 ÷ 100 = **24.56 °C**。

---

## 4. 人体感应帧 `0x111`

STM32 → Linux，DLC = 8。

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `uint8` | `event` | 事件类型，见下表 |
| 1 | `uint8` | `state` | 当前状态：`1` = 有人，`0` = 无人 |
| 2 | `uint8` | `raw` | 采样到的**原始电平** 0/1（未经滤波，用于排查接线） |
| 3 | `uint8` | `flags` | 标志位，见下表 |
| 4..7 | `uint32` LE | `timestamp_ms` | MCU 上电以来的毫秒数 |

**`event` 取值**

| 值 | 名称 | 含义 |
|---|---|---|
| `0x00` | LEAVE | 有人 → 无人 |
| `0x01` | ENTER | 无人 → 有人 |
| `0x02` | PERIOD | 周期保活上报，状态未发生变化 |

**`flags` 位**

| 位 | 名称 | 含义 |
|---|---|---|
| bit0 | WARM | 预热已完成，读数可信 |
| bit1 | HOLD | 当前处于"检出保持"窗口内 |

**关于 `timestamp_ms`**：这是**节点本地**的上电毫秒数，未与任何时钟同步，仅适合用来计算**同一节点内两个事件之间的时间差**（例如"两次触发相隔 15 秒"）。绝对时间请用 Linux 端的接收时间戳（`candump -ta`，或 SocketCAN 的 `SO_TIMESTAMPING`）。

**示例帧**：进入事件、状态有人、原始电平高、预热完成且处于保持窗口、上电后 123456 ms

```
1122334455: can0  111#0101010340E20100
                  │││││└──────┴─ timestamp_ms = 0x0001E240 = 123456
                  ││││└─ flags        = 0x03  (WARM | HOLD)
                  │││└─ raw          = 0x01
                  ││└─ state         = 0x01  (有人)
                  │└─ event          = 0x01  (ENTER)
                  └─ ID = 0x111
```

**语义说明（重要）**：STM32 端对检出结果施加了 **2 秒保持窗口**（可重触发），目的是抑制人体在传感器前停留时输出的一串抖动状态。因此：

- `event = ENTER` 上报随即出现，响应及时；
- `event = LEAVE` 会在模块自身输出转低 **且** 保持窗口过期之后才上报，最长可能滞后约 2 秒。
- 若 Linux 端需要"最后一帧时间"以外的语义，请以 `state` 字段为准。

---

## 5. 温湿度帧 `0x112`

STM32 → Linux，DLC = 8。

| 偏移 | 类型 | 字段 | 单位 | 范围 |
|---|---|---|---|---|
| 0..1 | `int16` LE | `temperature` | 0.01 °C | −4000 .. 8500（即 −40.00 .. 85.00 °C） |
| 2..3 | `uint16` LE | `humidity` | 0.01 %RH | 0 .. 10000（即 0.00 .. 100.00 %RH） |
| 4 | `uint8` | `status` | — | 见下表 |
| 5 | `uint8` | `driver_code` | — | AHT20 驱动原始返回码，见下表 |
| 6..7 | `uint16` LE | `seq` | — | 每帧 +1，回绕后从 0 继续 |

**`status` 取值**

| 值 | 名称 | 含义 | `temperature` / `humidity` 是否有效 |
|---|---|---|---|
| `0x00` | OK | 本次读取成功 | ✅ 是 |
| `0x01` | STALE | 读取失败，数值为**上次有效值** | ❌ 否（是旧值） |
| `0x02` | CRC_ERR | 传感器数据 CRC 校验失败 | ❌ 否（是旧值） |
| `0x03` | NOT_READY | 传感器数据未就绪 | ❌ 否（是旧值） |
| `0x04` | IIC_ERR | I2C 通信失败（常见原因：传感器未接/接触不良） | ❌ 否（是旧值） |
| `0x05` | NOT_INIT | 传感器未初始化成功 | ❌ 否（是旧值） |

**`driver_code`**（AHT20 驱动原始返回码，用于现场定位）

| 值 | 含义 |
|---|---|
| `0` | 成功 |
| `1` | I2C 读写失败 |
| `2` | 句柄为空 |
| `3` | 未初始化 |
| `4` | 数据未就绪 |
| `5` | CRC 错误 |

### 5.1 陈旧数据的处理约定（务必注意）

**读取失败时，STM32 不会把数值填 0，而是继续发送上一次读到的有效值**，同时把 `status` 置成对应的错误码。这样做的考虑是：

- 一组 0 对上层来说是**非法数据**，可能触发误判（比如湿度 0 % 触发告警）；
- 冻结的旧值 + 明确的 `status` 错误码，信息量严格更大。

因此 Linux 端**必须检查 `status` 字段**才能确定数值是否是本次实时采样值。建议同时用接收时间做二次判据（见第 9 节）。

**示例帧**：24.56 °C、55.30 %RH、读取正常、序号 27

```
1122334455: can0  112#98099A1500001B00
                  ││││││││││││└─┴─ seq          = 0x001B = 27
                  ││││││││││└─ driver_code = 0x00（成功）
                  ││││││││└─ status      = 0x00（OK）
                  │││││└─┴─ humidity    = 0x159A = 5530 → 55.30 %RH
                  └─┴─┴─ temperature = 0x0998 = 2456 → 24.56 °C
```

**示例帧（读取失败沿用旧值）**：数值同上但本次读取失败，序号 28

```
1122334455: can0  112#98099A1501011C00
                          └─┴─ status = 0x01（STALE），driver_code = 0x01（I2C 失败）
```

---

## 6. 心跳帧 `0x113`

STM32 → Linux，DLC = 8。

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0..3 | `uint32` LE | `uptime_s` | MCU 上电以来秒数 |
| 4..5 | `uint16` LE | `can_tx_drop` | CAN 发送失败累计帧数（含无空邮箱超时） |
| 6 | `uint8` | `fw_version` | 高 4 位主版本，低 4 位次版本（`0x10` = v1.0） |
| 7 | `uint8` | `health` | 健康标志位，见下表 |

**`health` 位**

| 位 | 名称 | 含义 |
|---|---|---|
| bit0 | PIR_READY | PIR 已过预热期，读数可信 |
| bit1 | ENV_READY | AHT20 初始化成功 |
| bit2 | TX_DROP | 曾经出现 CAN 发送失败（配合 `can_tx_drop` 判断严重程度） |
| bit3 | WARMUP | PIR 仍在预热期内（上电后约 60 秒） |

**`uptime_s` 的用途**：它同时是一个便宜的**重启检测**手段——`uptime_s` 突然变小就说明节点重启过。`can_tx_drop` 持续增长说明总线上有应答缺失或对端未上线，值得排查。

**示例帧**：上电 1234 秒、无丢帧、固件 v1.0、PIR 与温湿度均就绪

```
1122334455: can0  113#D204000000001003
                  ││││││││││││└─ health     = 0x03（PIR_READY | ENV_READY）
                  │││││││││││└─ fw_version = 0x10（v1.0）
                  ││││││││└─┴─ can_tx_drop = 0
                  └─┴─┴─┴─ uptime_s = 0x000004D2 = 1234
```

---

## 7. 上报时序

```
上电
 │
 ├─ 0 ~ 60 s ── PIR 预热期：health.bit3 = WARMUP，0x111 的 state 恒为 0
 │
 ├─ 每 5 s ──── 0x111 人体感应保活帧（event = 0x02）
 ├─ 状态翻转 ── 0x111 立即上报（event = 0x00 / 0x01）
 ├─ 每 2 s ──── 0x112 温湿度
 └─ 每 10 s ─── 0x113 心跳
```

- 人体感应**同时**有事件驱动和周期保活：前者保证响应速度，后者让 Linux 端能区分"确实没人"和"节点挂了/丢帧了"，并能在漏收事件帧后自行恢复状态。
- 温湿度与心跳是纯周期帧。
- 上电后第一帧温湿度会在约 2 秒内出现；第一帧保活约 5 秒后出现。

---

## 8. Linux 端接入

### 8.1 使能 CAN 接口

```bash
# 原生 CAN 适配器（candleLight / gs_usb 等）
sudo ip link set can0 up type can bitrate 500000

# 或 slcan 串口适配器（-s6 对应 500 kbps）
sudo slcand -o -c -s6 /dev/ttyUSB0 can0
sudo ip link set can0 up
sudo ip link set can0 txqueuelen 1000
```

### 8.2 快速观测

```bash
# 全部帧，带绝对时间戳
candump -ta can0

# 只看温湿度帧
candump -ta can0,0x112:0x7FF

# 十六进制显示便于比对本文档的示例
candump -ta -x can0
```

### 8.3 接收解码示例（C / SocketCAN）

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ---- 与 MCU 端 can_protocol.h 保持一致 ---- */
#define CAN_DLC_BYTES     8

#define CAN_ID_PIR        0x111
#define CAN_ID_ENV        0x112
#define CAN_ID_HEARTBEAT  0x113

#define ENV_STATUS_OK     0x00

/* 按偏移组小端数值，不要用结构体强转 */
static uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t get_i16le(const uint8_t *p)
{
    return (int16_t)get_u16le(p);
}

int main(void)
{
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); return 1; }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }

    struct can_frame f;
    while (read(s, &f, sizeof(f)) == (ssize_t)sizeof(f)) {

        /* 只处理标准数据帧 */
        if (f.can_id & CAN_EFF_FLAG) { continue; }
        if (f.can_id & CAN_RTR_FLAG) { continue; }

        /* DLC 必须为 8，否则丢弃并计数 */
        if (f.can_dlc != CAN_DLC_BYTES) {
            fprintf(stderr, "DLC=%u 不符，丢弃 ID=0x%03X\n", f.can_dlc, f.can_id);
            continue;
        }

        switch (f.can_id) {
        case CAN_ID_ENV: {
            int16_t  temp = get_i16le(&f.data[0]);
            uint16_t rh   = get_u16le(&f.data[2]);
            uint8_t  st   = f.data[4];
            if (st == ENV_STATUS_OK) {
                printf("温湿度: %.2f °C  %.2f %%RH  seq=%u\n",
                       temp / 100.0, rh / 100.0, get_u16le(&f.data[6]));
            } else {
                /* 数值是上次有效值，不是本次采样值 */
                printf("温湿度[陈旧]: %.2f °C  %.2f %%RH  status=%u driver=%u\n",
                       temp / 100.0, rh / 100.0, st, f.data[5]);
            }
            break;
        }
        case CAN_ID_PIR: {
            const char *ev = (f.data[0] == 0x01) ? "进入"
                           : (f.data[0] == 0x00) ? "离开" : "保活";
            printf("人体感应: %s  state=%u raw=%u flags=0x%02X ts=%u ms\n",
                   ev, f.data[1], f.data[2], f.data[3], get_u32le(&f.data[4]));
            break;
        }
        case CAN_ID_HEARTBEAT:
            printf("心跳: uptime=%u s  tx_drop=%u  fw=v%u.%u  health=0x%02X\n",
                   get_u32le(&f.data[0]), get_u16le(&f.data[4]),
                   (f.data[6] >> 4) & 0x0F, f.data[6] & 0x0F, f.data[7]);
            break;
        default:
            printf("未知帧 ID=0x%03X\n", f.can_id);
            break;
        }
    }

    close(s);
    return 0;
}
```

### 8.4 快速验证用的 Python 片段

接在 `candump -L can0` 的输出上可直接解析（`-L` 格式为 `时间 接口 ID#数据`）：

```python
import struct

CAN_ID_ENV = 0x112

def parse_env(data_hex: str):
    b = bytes.fromhex(data_hex)
    assert len(b) == 8, f"DLC 应为 8，实际 {len(b)}"
    temp, rh = struct.unpack_from('<h', b, 0)[0], struct.unpack_from('<H', b, 2)[0]
    status, driver, seq = b[4], b[5], struct.unpack_from('<H', b, 6)[0]
    return dict(temperature=temp / 100.0, humidity=rh / 100.0,
                status=status, driver_code=driver, seq=seq, valid=(status == 0))

# 对应本文档示例帧
print(parse_env('98099A1500001B00'))
# {'temperature': 24.56, 'humidity': 55.3, 'status': 0, 'driver_code': 0,
#  'seq': 27, 'valid': True}
```

### 8.5 发送测试帧（验证 Linux → STM32 通路）

```bash
cansend can0 221#0102000000000000
```

MCU 端的接收路径目前只做到"能正确取帧"，收到后不会执行具体配置动作（见第 10 节）。

---

## 9. 异常与超时判定建议

| 现象 | 建议判定 |
|---|---|
| 超过 **15 秒** 未收到任何 `0x113` 心跳 | 判定 MCU 离线或总线故障。15 s = 1.5 倍心跳周期，容一次丢帧 |
| `0x112` 的 `status != 0` 连续出现 | 传感器故障。`status = 0x04`（IIC_ERR）优先检查接线与供电 |
| `0x112` 的 `seq` 出现跳号 | CAN 丢帧。结合 `candump -ta` 的时间间隔确认 |
| `0x113` 的 `uptime_s` 变小 | MCU 重启过，此前所有缓存状态应作废 |
| `0x113` 的 `can_tx_drop` 持续增长 | MCU 发不出去 —— 检查对端是否在线、终端电阻、总线长度 |
| 超过 **10 秒** 未收到 `0x111` | 理论上不应发生（保活周期 5 s）。判定为丢帧或节点异常 |
| `0x111` 的 `state = 1` 但 `raw = 0` 持续存在 | 正常现象，处于保持窗口内（`flags.bit1 = 1`）。超过 5 秒仍如此则说明 PIR 模块输出卡在高电平，模块可能故障 |

**建议**：以上判定都配合接收时间戳（`SO_TIMESTAMPING`）而不是 MCU 的 `timestamp_ms`，因为后者未与系统时钟同步。

---

## 10. 预留与扩展

### 10.1 Linux → STM32（已定义 ID，未实现）

| ID | 消息 | 载荷 |
|---|---|---|
| `0x221` | 配置写入 | `[0]` 配置项，`[1]` 长度，`[2..7]` 数据（待实现时细化） |
| `0x222` | 查询请求 | `[0]` 查询项，`[1..7]` 参数 |
| `0x228` | 应答 / ACK | `[0]` 被应答帧的类型，`[1]` 结果（0 = OK），`[2..7]` 数据 |

规划中的可配置项：上报周期、PIR 消抖与保持时间、心跳周期。

**当前 MCU 端状态**：`CAN_Receive_Msg()` 已能正确从 FIFO0 逐帧取报文（含 DLC 校验与越界修复），但收到帧后不执行任何动作。

### 10.2 扩展帧（29 位）

当前全部使用 11 位标准帧。若将来需要传输长文本、日志或固件升级数据，再启用 29 位扩展帧，位域规则沿用同样的语义划分。分片传输已预留 `0x1Fx` 段。

### 10.3 变更记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-09-20 | 1.0 | 初版。定义 `0x111` / `0x112` / `0x113` 三帧，预留反向通道与分片段 |
