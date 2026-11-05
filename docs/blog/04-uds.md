# STM32 + i.MX6ULL CAN 诊断与 OTA（04）：UDS 最小闭环——0x10/0x3E/0x22，从会话到读 VIN

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[03 ISO-TP：从 0 到 4095 字节](https://blog.csdn.net/longlin101/article/details/170053591)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.5-uds`

ISO-TP 解决的是“长数据怎么过 CAN”。

UDS 解决的是“这条数据是什么意思”。

这一篇做 UDS 的最小闭环：

```text
0x10 会话控制
0x3E TesterPresent
0x22 读 DID
```

只做这三条，不碰 DTC、FOTA、安全访问。

---

## TL;DR

- Linux 是 Tester，STM32 是 ECU；
- `0x7E0` 请求，`0x7E8` 响应；
- 支持默认/编程/扩展会话，S3=5s；
- `0x3E 00` 保活；
- `0x22` 读 DID：VIN、版本、协议版本、标定 ID、PIR、周期配置；
- 负响应 `7F SID NRC`；
- 真实 CAN：100 次读 DID 2.034s，S3 超时日志正确；
- 当前只做 UDS 基础，DTC/FOTA 还没开始。

---

## 1. UDS 在链路里的位置

```text
应用：UDS 0x10 / 0x3E / 0x22
        ↓
传输：ISO-TP SF/FF/CF/FC
        ↓
链路：CAN 2.0A 500 kbps
        ↓
物理：CANH/CANL、120Ω
```

- ISO-TP 不知道 SID 是什么；
- UDS 不直接碰 CAN 帧；
- UDS 响应长于 7 字节时，自动走 ISO-TP 多帧。

例如读 VIN：

```text
UDS: 62 F1 90 + 17 字节 VIN = 20 字节
ISO-TP: FF + 2 个 CF
```

---

## 2. 两端角色

| 端 | 角色 | 代码 |
|---|---|---|
| i.MX6ULL | Tester / 客户端 | `gateway/src/uds_client.c`、`uds_cli.c` |
| STM32 | ECU / 服务端 | `firmware/app/.../uds_server.c` |
| 传输 | ISO-TP | 两端共用 `isotp.c` |

请求：

```text
Linux 0x7E0
```

响应：

```text
STM32 0x7E8
```

---

## 3. 第一版只做三条服务

### `0x10` 会话控制

```text
10 01   默认会话
10 02   编程会话
10 03   扩展会话
```

正响应：

```text
50 01
50 02
50 03
```

非法子功能：

```text
10 04 → 7F 10 12
```

### `0x3E` TesterPresent

```text
3E 00 → 7E 00
3E 80 → 不响应（抑制正响应）
```

作用：

- 重置 S3；
- 保持会话。

### `0x22` 读 DID

```text
22 F1 90 → 62 F1 90 + 17 字节 VIN
```

第一版 DID：

| DID | 内容 | 长度 |
|---|---|---|
| `F190` | VIN | 17 字节 ASCII |
| `F180` | 固件版本 | 1 字节 BCD |
| `F181` | 协议版本 | 1 字节 |
| `F182` | 标定 ID | 4 字节 LE |
| `F183` | 序列号 | 8 字节 ASCII（占位） |
| `F1A0` | PIR 开关 | 1 字节 |
| `F1A1` | 温湿度周期 | 2 字节 LE |
| `F1A2` | 心跳周期 | 2 字节 LE |

VIN 是占位值，量产由产线写入。

---

## 4. 会话与 S3

STM32 维护：

```c
typedef enum {
    UDS_SESSION_DEFAULT = 0x01,
    UDS_SESSION_PROGRAM = 0x02,
    UDS_SESSION_EXTENDED = 0x03
} uds_session_t;
```

规则：

```text
S3 = 5000ms
任何有效请求重置 S3
超时后回默认会话
```

Linux 端每 2s 可以发一次 `3E 00` 保活；第一版 CLI 是单次请求，不常驻。

实测：

```text
17:10:03  session 03 → 50 03
17:10:08  UDS: S3 timeout -> default session
17:10:31  session 03 → 50 03
17:10:36  UDS: S3 timeout -> default session
```

5.001s / 5.006s，符合预期。

---

## 5. 负响应

统一格式：

```text
7F SID NRC
```

用到的 NRC：

```text
0x11 serviceNotSupported
0x12 subFunctionNotSupported
0x13 incorrectMessageLength
0x31 requestOutOfRange
```

实测：

```text
22 12 34 → 7F 22 31
10 04    → 7F 10 12
```

---

## 6. Linux CLI

```bash
sudo ip link set can0 up type can bitrate 500000 restart-ms 100

./uds_cli can0 session 03
./uds_cli can0 tester-present
./uds_cli can0 read-did F180
./uds_cli can0 read-did F190
./uds_cli can0 read-did 1234
```

输出：

```text
TX: 22 F1 90
RX: 62 F1 90 4C 46 56 32 41 32 31 4B 37 41 33 30 30 30 30 30 31
{"cmd":"read-did","did":"F190","status":"ok","len":17}
VIN: LFV2A21K7A3000001
```

`F190` 20 字节，走 FF + 2 个 CF。

---

## 7. candump 里的 UDS

```text
7E0 [8] 02 10 03 CC CC CC CC CC
7E8 [8] 02 50 03 CC CC CC CC CC

7E0 [8] 03 22 F1 90 CC CC CC CC
7E8 [8] 10 14 62 F1 90 4C 46 56
7E0 [8] 30 08 05 CC CC CC CC CC
7E8 [8] 21 32 41 32 31 4B 37 41
7E8 [8] 22 33 30 30 30 30 30 31

7E0 [8] 03 22 12 34 CC CC CC CC
7E8 [8] 03 7F 22 31 CC CC CC CC
```

- `02` = SF，长度 2；
- `03` = SF，长度 3；
- `10 14` = FF，总长 20；
- `30 08 05` = FC，BS=8，STmin=5ms；
- `21/22` = CF。

---

## 8. 踩坑

### 8.1 `uds_cli` 每次启动都执行 `ip link`

第一版 `uds_client_init()` 调用了：

```c
can_bring_up(ifname, 500000);
```

内部是：

```bash
ip link set can0 up type can bitrate 500000 restart-ms 100
```

100 次循环时，每个进程都 fork 一次 `ip`。  
有一次第一帧之前卡住，candump 看不到任何请求。

修复：

```text
删除 uds_cli 的自动 can_bring_up
要求先手动 up 接口
```

修复后：

```text
100 次读 DID：2.034s
无卡顿、无 fail
```

### 8.2 `mock_ecu` 和 STM32 抢 `0x7E8`

如果板上还跑着 `mock_ecu`，它也会回显 `0x7E8`，日志会混。

测试前：

```bash
kill $(pidof mock_ecu)
```

### 8.3 CAN TX 偶发 “no free mailbox”

中间出现过一次：

```text
CAN tx: no free mailbox, frame dropped
```

对应 Linux 侧：

```text
can state ERROR-PASSIVE (tx 244 rx 128)
```

后来重新烧录/复位后恢复正常。  
当前加了诊断日志：

```text
CAN tx: AddTx failed st=... state=... err=... ESR=... TSR=...
```

再出现时可以直接看 HAL 状态，而不是猜邮箱满。

### 8.4 首次 3.2s 延迟

出现过一次首次请求响应晚 3.2s，超过 CLI 的 3s 超时。  
后续 100 次未复现。当前保留监控，不把它当成已解决。

---

## 9. 测试结果

```text
10 01 / 03 / 04        通过
3E 00                  通过
F180 / F190            通过
F1A0 / F1A1 / F1A2     通过
未知 DID NRC31         通过
S3 5s 超时             通过
100 次读 DID           通过（2.034s）
```

当前限制：

- 没有 `0x27` 安全访问；
- 没有 `0x19/0x14` DTC；
- 没有 `0x34/0x36/0x37` FOTA；
- `0x10 02` 只切会话，不代表升级流程已实现；
- `3E 80` 抑制响应已实现，但未单独测试。

---

## 10. 下一步：DTC

```text
0x19 02 FF   读 DTC
0x14 FF FF FF 清 DTC
```

计划 5 类：

```text
C001 DHT11 超时/校验
C002 bus-off
C003 OTA CRC
C004 温湿度越界
C005 Flash 错误
```

DTC 完成后再做 Bootloader/FOTA。

---

## 11. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.5-uds`
- Linux UDS Client：`gateway/src/uds_client.c`
- Linux CLI：`gateway/src/uds_cli.c`
- STM32 UDS Server：`firmware/app/Core/CustomDrivers/uds_server.c`
- 测试日志：`test/records/20261105-uds-smoke.log`

标签：`STM32` `CAN` `UDS` `i.MX6ULL` `嵌入式Linux`
