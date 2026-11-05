# 测试记录

> 本文件是模板。每完成一项测试，复制一行/一节，填入实际数据和日志路径。
> 原则：**没有原始日志的数字不算结果。**

---

## 1. 测试环境

| 项目 | 内容 |
|---|---|
| 日期 | 2026-11-03 |
| 固件 commit | 本地 `My_AI`（STM32 工程，未入库） |
| 网关 commit | 本仓库 `d8fac30` + 本地 `my_ai`（未入库） |
| Bootloader commit | 未开始 |
| STM32 板 | STM32F407VGT6-Disc |
| Linux 板 | i.MX6ULL mini EMMC（ATK-IMX6U） |
| 内核 | `Linux 4.1.15` |
| 工具链 | `arm-poky-linux-gnueabi-gcc`（NXP SDK 4.1.15） |
| CAN 收发器 | SN65HVD230 / TJA1051T/3 / 其他 |
| 终端电阻 | 两端各 120Ω，实测 `__Ω` |
| 电源 | STM32 `__V`，i.MX6ULL `__V`，共地 |
| 测试人 | 林祖龙 |

---

## 2. 测试矩阵

### 2.0 双向命令 / ACK

| ID | 测试项 | 步骤 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-BI-01 | 1000 次命令 | `cmd_stress.py can0 1000 --timeout-ms 150` | 1000/1000，0 重试 | **通过**：1000/1000，重试 0，耗时 20.04 s，P99 22.06 ms | `test/records/20261103-can-stress-1000.log` |
| A-BI-02 | 超时重试路径 | 50 ms 超时压测 | 重试后仍成功 | **通过**：2006 次尝试 / 1006 次重试，1000/1000 成功，99.67 s | `test/records/20261103-can-timeout-50ms.log` |
| A-BI-03 | 重复 seq 去重 | 同一 `seq=0x77` 发两次，再查 `0x12` | `dup_cmd +1`，不重复执行 | **通过**：`cmd_rx 1595→1598`，`dup_cmd 300→301`，`crc_err=0`，`ack_fail=0` | `test/records/20261103-can-dedup.log` |
| A-BI-04 | 多分片 ACK | 查询 `0x12` 11 字节 | 3 帧正确重组 | **通过**：frag 0/1/2 重组正确 | `test/records/20261103-can-dedup.log` |
| A-BI-05 | 错误 CRC / 非法参数 | 注入错误帧 | 计数，不崩溃 | 待执行 |  |

> 关键数据：正常 1000 次命令 RTT min/avg/max = 18.57 / 19.75 / 26.24 ms，p50/p99 = 19.74 / 22.06 ms。
> 50 ms 超时那次是超时窗口小于 RTT p99 导致的“假丢包”，不是链路丢包；所有命令最终成功，说明重试/去重路径有效。

### 2.1 ISO-TP

| ID | 测试项 | 步骤 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-IT-01 | 单帧 SF | 1 / 7 字节 | 收发一致 | **通过**：selftest 1、7 字节 | `test/records/20261103-isotp-selftest.log` |
| A-IT-02 | 多帧 4095 字节 | FF + CF，真实 CAN | 0 错误、长度一致 | **通过**：真实 CAN 4095 收发；585 CF，rx_ovf=0 | `test/records/20261104-isotp-real-4095-bs8.log` |
| A-IT-03 | BlockSize/STmin | BS=8 / STmin=5ms，真实 CAN | 不超时、不乱序 | **通过**：74 个 FC，块内 STmin 校验通过 | `test/records/20261104-isotp-real-4095-bs8.log` |
| A-IT-04 | 丢 CF / 丢 FC | 真实 CAN：drop-last-cf / drop-fc | N_Cr/N_Bs 超时、不崩溃 | **通过（真实 CAN）**：STM32 N_Cr timeout；gw TX_TIMEOUT | `test/records/20261104-isotp-fault-injection.log` |
| A-IT-05 | 错序 CF | 真实 CAN：mangle-cf 2 | 中止并回 OVFLW | **通过（真实 CAN）**：SN mismatch，OVFLW，gw TX_ABORT | `test/records/20261104-isotp-fault-injection.log` |
| A-IT-06 | DHT11 并发 | 4095 收发时每 2s 读 DHT11 | 不丢 CF、无 overflow | **通过**：rx_ovf=0、tx drops=0 | `test/records/20261104-isotp-real-4095-bs8.log` |

### 2.2 UDS

| ID | 测试项 | 步骤 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-UD-01 | 会话控制 | `10 01` / `10 03` / `10 04` | 正响应/正确 NRC | **通过**：`50 01`、`50 03`、`7F 10 12` | `test/records/20261105-uds-smoke.log` |
| A-UD-02 | TesterPresent | `3E 00`；S3 5s 待测 | 正响应/会话保持 | **通过（单次）**：`7E 00`；超时行为待测 | `test/records/20261105-uds-smoke.log` |
| A-UD-03 | 读版本 | `22 F180` | 正响应 | **通过**：`62 F1 80 10` | `test/records/20261105-uds-smoke.log` |
| A-UD-04 | 读配置 DID | `22 F1A0/F1A1/F1A2` | 数据正确 | **通过**：`01`、`D0 07`(2000ms)、`10 27`(10000ms) | `test/records/20261105-uds-smoke.log` |
| A-UD-05 | 读 DTC | `19 02 FF` | 能读到注入的 DTC | 待实现 |  |
| A-UD-06 | 清 DTC | `14 FF FF FF` | 清后 `19 02 FF` 为空 | 待实现 |  |
| A-UD-07 | 非法 DID | `22 1234` | `7F 22 31` | **通过**：`7F 22 31` | `test/records/20261105-uds-smoke.log` |
| A-UD-08 | 读 VIN 多帧 | `22 F190` | 20 字节、FF+CF 正确 | **通过**：VIN `LFV2A21K7A3000001` | `test/records/20261105-uds-smoke.log` |

### 2.3 DTC

| ID | 测试项 | 注入方法 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-DT-01 | `0xC001` DHT11 超时/校验错 | 拔 DATA/短接 DATA/人为改坏校验和 | DTC 置位，可读 | 待执行 |  |
| A-DT-02 | `0xC002` bus-off | 拔 CANH/短接 | DTC 置位，恢复后仍可读 | 待执行 |  |
| A-DT-03 | `0xC003` OTA CRC | 改固件 1 字节 | DTC 置位，不切换分区 | 待执行 |  |
| A-DT-04 | `0xC004` 越界 | 遮挡/加热传感器 | DTC 置位 | 待执行 |  |
| A-DT-05 | `0xC005` Flash | 擦除后写坏/校验失败 | DTC 置位 | 待执行 |  |
| A-DT-06 | 掉电保存 | 置位后断电重启 | DTC 仍存在 | 待执行 |  |

### 2.4 Bootloader / OTA

| ID | 测试项 | 步骤 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-OT-01 | A/B 启动 | 分别烧 A/B | 两个分区都能启动 | 待执行 |  |
| A-OT-02 | 正常升级 | 10 次 | 10/10 成功 | 待执行 |  |
| A-OT-03 | 50 次升级 | 循环 50 次 | 50/50 成功 | 待执行 |  |
| A-OT-04 | 25% 掉电 | 传输到 25% 断电 | 回滚，旧版本可启动 | 待执行 |  |
| A-OT-05 | 50% 掉电 | 传输到 50% 断电 | 回滚，旧版本可启动 | 待执行 |  |
| A-OT-06 | 75% 掉电 | 传输到 75% 断电 | 回滚，旧版本可启动 | 待执行 |  |
| A-OT-07 | 断点续传 | 中断后重发 | 从上次 offset 继续 | 待执行 |  |
| A-OT-08 | 版本确认 | 升级后 `22 0100` | 版本号变化 | 待执行 |  |

### 2.5 可靠性

| ID | 测试项 | 方法 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-RL-01 | 24h 连续运行 | 500 ms 周期，记录 8.64M 帧 | 丢帧 < 0.01% | 待执行 |  |
| A-RL-02 | 72h 连续运行 | 同上 | 无重启，RSS 稳定 | 待执行 |  |
| A-RL-03 | bus-off 恢复 | 拔线/短接 | 3 s 内恢复 | 待执行 |  |
| A-RL-04 | 端到端延迟 | 时间戳/GPIO | P99 < 50 ms | 待执行 |  |
| A-RL-05 | 断网缓存 | 断开 MQTT 10 分钟 | 10 万条可补传 | 待执行 |  |
| A-RL-06 | 磁盘满 | 填满分区 | 不崩溃，报警 | 待执行 |  |
| A-RL-07 | 网关重启 | `systemctl restart` | 自动恢复 | 待执行 |  |

---

## 3. 指标汇总

| 指标 | 目标 | 实测 | 是否通过 |
|---|---|---|---|
| 命令成功率 | 1000/1000 | 1000/1000 | ✅ |
| 命令重试率 | 0 | 0 / 1000 | ✅ |
| 命令/ACK RTT P99 | < 50 ms | 22.06 ms | ✅ |
| 24h 丢帧率 | < 0.01% |  |  |
| 端到端 P99 | < 50 ms |  |  |
| UDS `0x22` 响应 | < 50 ms |  |  |
| bus-off 恢复 | < 3 s |  |  |
| OTA 成功率 | 50/50 |  |  |
| OTA 升级时间 | < 60 s |  |  |
| 网关 CPU | < 30% |  |  |
| 网关 RSS | < 50 MB |  |  |

---

## 4. 原始日志索引

| 日志 | 路径 | 说明 |
|---|---|---|
| CAN 抓包 | `test/records/YYYYMMDD-can.log` | `candump -ta can0` |
| 串口日志 | `test/records/YYYYMMDD-uart.log` | 115200 |
| 内核日志 | `test/records/YYYYMMDD-dmesg.log` | `dmesg` |
| 网关日志 | `test/records/YYYYMMDD-gateway.log` | journald/文件 |
| OTA 记录 | `test/records/YYYYMMDD-ota.log` | offset/ACK/结果 |
| 延迟数据 | `test/records/YYYYMMDD-latency.csv` | 原始时间戳 |

---

## 5. 缺陷记录

| 编号 | 现象 | 复现步骤 | 根因 | 修复 commit | 状态 |
|---|---|---|---|---|---|
| BUG-001 |  |  |  |  |  |
| BUG-002 |  |  |  |  |  |

---

## 6. 结论模板

```text
测试日期：
测试范围：
通过项：
失败项：
关键数据：
遗留问题：
是否可以写进简历：是/否
备注：
```
