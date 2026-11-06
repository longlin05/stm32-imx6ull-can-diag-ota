# STM32 + i.MX6ULL CAN 诊断与 OTA（05）：UDS DTC——从故障检测到 Flash 掉电保存

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[04 UDS 最小闭环：0x10/0x3E/0x22](https://blog.csdn.net/longlin101/article/details/170145581)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.6-dtc`

UDS 有了会话、保活、读 DID，下一步是故障管理。

这一篇做 DTC：

```text
0x19 01/02/0A   读 DTC
0x14 FF FF FF   清 DTC
```

以及 5 个故障、状态位、去抖、Flash 掉电保存。

---

## TL;DR

- DTC 不是日志，是故障状态机；
- 状态位：testFailed / pending / confirmed；
- 5 个 DTC：C001 DHT11、C002 bus-off、C003 OTA CRC、C004 温湿度越界、C005 Flash；
- `19 02 FF` 读当前故障，`19 0A` 读支持列表，`19 01 FF` 读数量；
- `14 FF FF FF` 清全部；
- C001 连续 3 次失败确认，状态 `0x0D`；
- bus-off 恢复后只保留 confirmed `0x08`；
- DTC 存 sector 7（`0x08060000`），置位/清除都能跨重启保留；
- 踩了一个坑：`0x08060000` 对应 `FLASH_SECTOR_7`，不是 `SECTOR_11`。

---

## 1. DTC 是什么

DTC = Diagnostic Trouble Code。

它不是一个“事件日志”，而是：

```text
某个故障当前是什么状态
```

每个 DTC：

```text
2 字节故障码 + 1 字节状态
```

例如：

```text
C0 01 0D
```

表示：

```text
C001
状态 0x0D = testFailed + pending + confirmed
```

---

## 2. 5 个 DTC

| DTC | 含义 | 触发点 |
|---|---|---|
| `C001` | DHT11 超时/校验错 | DHT11 连续读失败 |
| `C002` | CAN bus-off | `ESR.BOFF=1` |
| `C003` | OTA CRC | 以后 Bootloader/OTA |
| `C004` | 温湿度越界 | 温度/湿度超范围 |
| `C005` | Flash 错误 | 以后 Flash 读写/校验 |

第一版真正能触发：

```text
C001
C002
C004
```

`C003/C005` 先占位，等 OTA/Flash。

---

## 3. 状态位

ISO 14229 的状态字节：

```text
bit0 testFailed
bit1 testFailedThisOperationCycle
bit2 pendingDTC
bit3 confirmedDTC
bit4 testNotCompletedSinceLastClear
bit5 testFailedSinceLastClear
bit6 testNotCompletedThisOperationCycle
bit7 warningIndicatorRequested
```

第一版用：

```text
0x01 testFailed
0x04 pending
0x08 confirmed
```

所以：

```text
0x0D = 0x01 + 0x04 + 0x08
0x08 = confirmed only
```

---

## 4. UDS 读 DTC

### `19 0A` 读支持的 DTC

请求：

```text
19 0A
```

响应：

```text
59 0A FF C0 01 00 C0 02 08 C0 03 00 C0 04 00 C0 05 00
```

5 条记录：

```text
3 字节/条：DTC 高 + DTC 低 + status
```

总长 18 字节，会走 ISO-TP FF+CF。

### `19 02 FF` 按状态掩码读

请求：

```text
19 02 FF
```

`FF` 表示所有状态。

有故障时：

```text
59 02 FF C0 01 0D C0 02 08
```

无故障时：

```text
59 02 FF
```

### `19 01 FF` 读数量

```text
59 01 FF 00 02
```

表示 2 个 DTC。

### `14 FF FF FF` 清除

```text
14 FF FF FF → 54
```

只支持全部清除；其他 group 返回 `7F 14 31`。

---

## 5. 检测与去抖

不能一次失败就报 DTC。

第一版：

```text
连续 3 次失败 → confirmed
连续 3 次通过 → 清 testFailed/pending
confirmed 只有 0x14 能清
```

DHT11 每 2s 读一次：

```text
3 次失败约 6s
```

驱动层只上报：

```c
Dtc_Report(DTC_CODE_DHT11, failed);
```

DTC 模块负责状态机、Flash、UDS 编码。

---

## 6. 检测点

### C001 DHT11

```c
if (DHT11_Read(&th) != 0U) {
    Dtc_Report(DTC_CODE_DHT11, 0U);
} else {
    Dtc_Report(DTC_CODE_DHT11, 1U);
}
```

### C004 温湿度越界

```c
if ((th.temp_int > 50U) || (th.humi_int > 90U)) {
    Dtc_Report(DTC_CODE_ENV_RANGE, 1U);
} else {
    Dtc_Report(DTC_CODE_ENV_RANGE, 0U);
}
```

### C002 bus-off

STM32F4 的 HAL 状态枚举里没有 `HAL_CAN_STATE_BUS_OFF`，所以不能这样写：

```c
HAL_CAN_GetState(&hcan1) == HAL_CAN_STATE_BUS_OFF   // 编译错误
```

直接看 ESR：

```c
Dtc_ReportImmediate(DTC_CODE_CAN_BUSOFF,
    ((hcan1.Instance->ESR & CAN_ESR_BOFF) != 0U) ? 1U : 0U);
```

---

## 7. 真实测试

### 无故障

```text
19 02 FF → 59 02 FF
19 01 FF → 59 01 FF 00 00
```

### 拔 DHT11 DATA

等约 6s：

```text
19 02 FF → 59 02 FF C0 01 0D
19 01 FF → 59 01 FF 00 01
```

### bus-off

`can0 down` 让 STM32 失去 ACK，等它进 bus-off：

```text
ESR = 0x4D000055
BOFF = 1
```

恢复后：

```text
19 02 FF → 59 02 FF C0 02 08
```

`0x08` 是 confirmed；`testFailed/pending` 已随恢复清掉。

### 清除

```text
14 FF FF FF → 54
19 02 FF    → 59 02 FF
```

---

## 8. Flash 掉电保存

DTC 必须跨重启保留。

存储：

```text
地址：0x08060000
大小：128KB
sector：FLASH_SECTOR_7
```

结构：

```c
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t crc;
    dtc_entry_t entries[5];
} dtc_store_t;
```

策略：

```text
状态变化 → dirty
Dtc_Task 每 1ms 检查
dirty 且距上次 ≥1s → 擦除 + 写入
正常读 DTC 不写 Flash
```

### 掉电验证

置位 C001，等 2s，重启：

```text
19 02 FF → 59 02 FF C0 01 0D
```

清除，等 3s，重启：

```text
19 02 FF → 59 02 FF
```

置位和清除都能跨重启保留。

---

## 9. 最大的坑：sector 号写错

一开始把 `0x08060000` 当成 sector 11：

```c
erase.Sector = FLASH_SECTOR_11;   // 错
```

但 F407 的 `0x08060000` 是 **sector 7**。

现象：

```text
第一次写 C001 成功（sector 7 本来是空的）
clear 时擦的是 sector 11（0x080E0000）
0x08060000 没被擦，再写空表失败
重启又读回旧 C001
```

修复：

```c
#define DTC_STORE_ADDR    0x08060000UL
#define DTC_STORE_SECTOR  FLASH_SECTOR_7
```

这个坑很典型：**地址对、sector 号错，第一次能写，后面擦不掉。**

---

## 10. 另一个小坑：CLI 多打一个 C

协议里是：

```text
C0 01
```

CLI 原来：

```c
printf("DTC C%03X", code);
```

`0xC001` 被打成 `CC001`。

改成：

```c
printf("DTC %04X", code);
```

只影响显示，不影响协议。

---

## 11. 下一步

DTC 完成，下一步是 Bootloader：

```text
A/B 分区
CRC32
跳转
回滚
```

再往后才是：

```text
0x34/0x36/0x37 OTA
50 次升级
掉电测试
```

---

## 12. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.6-dtc`
- DTC 代码：`firmware/app/Core/CustomDrivers/dtc.c`
- UDS 0x19/0x14：`firmware/app/Core/CustomDrivers/uds_server.c`
- 测试日志：
  - `test/records/20261105-dtc-host.log`
  - `test/records/20261106-dtc-real-can.log`

标签：`STM32` `CAN` `UDS` `DTC` `i.MX6ULL` `嵌入式Linux`
