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
| A-UD-02 | TesterPresent / S3 | `3E 00`；`session 03` 后等 6s | 正响应/5s 回默认 | **通过**：`7E 00`；两次 `UDS: S3 timeout -> default session` | `test/records/20261105-uds-smoke.log` |
| A-UD-03 | 读版本 | `22 F180` | 正响应 | **通过**：`62 F1 80 10` | `test/records/20261105-uds-smoke.log` |
| A-UD-04 | 读配置 DID | `22 F1A0/F1A1/F1A2` | 数据正确 | **通过**：`01`、`D0 07`(2000ms)、`10 27`(10000ms) | `test/records/20261105-uds-smoke.log` |
| A-UD-05 | 读 DTC | `19 02 FF` | 能读到注入的 DTC | 待实现 |  |
| A-UD-06 | 清 DTC | `14 FF FF FF` | 清后 `19 02 FF` 为空 | 待实现 |  |
| A-UD-07 | 非法 DID | `22 1234` | `7F 22 31` | **通过**：`7F 22 31` | `test/records/20261105-uds-smoke.log` |
| A-UD-08 | 读 VIN 多帧 | `22 F190` | 20 字节、FF+CF 正确 | **通过**：VIN `LFV2A21K7A3000001` | `test/records/20261105-uds-smoke.log` |
| A-UD-09 | 连续 100 次读 DID | `for i in 1..100` | 100/100 成功 | **通过**：`real 2.034s`，无 fail/卡顿 | `test/records/20261105-uds-smoke.log` |
| A-UD-10 | 首次请求延迟 | 去掉每次 `ip link` 后重测 | 不超时 | **已修复**：`uds_cli` 不再自动 `ip link`；100 次 2.034s | `test/records/20261105-uds-smoke.log` |

### 2.3 DTC

| ID | 测试项 | 注入方法 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-DT-00 | DTC 协议层 | 主机桩调用 `19 02/0A/01`、`14` | 响应格式正确 | **主机自测通过**：空表、置位 C001 0x0D、清除 | `test/records/20261105-dtc-host.log` |
| A-DT-01 | `0xC001` DHT11 超时/校验错 | 拔 DATA/短接 DATA/人为改坏校验和 | DTC 置位，可读 | **真实 CAN 通过**：拔 DATA 后 C001=0x0D | `test/records/20261106-dtc-real-can.log` |
| A-DT-02 | `0xC002` bus-off | `can0 down/up` 使 STM32 失去 ACK | DTC 置位，恢复后仍可读 | **真实 CAN 通过**：C002=0x08；`14` 后清空；存储已迁 sector 4 | `test/records/20261106-dtc-real-can.log` |
| A-DT-03 | `0xC003` OTA CRC | 改固件 1 字节 | DTC 置位，不切换分区 | 占位，待 OTA |  |
| A-DT-04 | `0xC004` 越界 | 遮挡/加热传感器 | DTC 置位 | 代码已接，待真实 CAN |  |
| A-DT-05 | `0xC005` Flash | 擦除后写坏/校验失败 | DTC 置位 | 占位，待 Flash |  |
| A-DT-06 | 掉电保存 | 置位后等 2s 断电重启；clear 后重启 | DTC 保留；clear 后为空 | **旧 sector 7 通过**；已迁 sector 4，待复测 | `test/records/20261106-dtc-real-can.log` |
| A-DT-07 | 清除 DTC | `14 FF FF FF` | 清后为空 | **通过**：`54`，随后 `19 02 FF` 为空 | `test/records/20261106-dtc-real-can.log` |

### 2.4 Bootloader / OTA

| ID | 测试项 | 步骤 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-BL-00 | Bootloader 核心 | 主机运行 CRC/镜像/Metadata/策略/双记录测试 | 全部断言通过 | **通过**：`boot core tests: OK`、`boot meta store tests: OK` | `test/records/20261106-boot-core-host.log` |
| A-BL-01 | Bootloader → App A | 烧 Bootloader + app_a.img | App A 运行，UDS 正常 | **通过**：PC 0x0802xxxx，`50 03` | `test/records/20261106-boot-ab-trial.log` |
| A-BL-02 | trial B 启动 | active=A,trial=B,attempts=0 | 启动 B 一次 | **通过**：PC 0x0806xxxx | `test/records/20261106-boot-ab-trial.log` |
| A-BL-03 | 未确认回滚 A | trial B 不确认，再复位 | 回滚 A | **通过**：PC 0x0802xxxx | `test/records/20261106-boot-ab-trial.log` |
| A-BL-04 | 确认 B | App B 调 `Boot_AppConfirm(1)` | active=B，后续一直 B | **通过**：Metadata seq12 active=B；再次复位仍 B | `test/records/20261106-boot-ab-trial.log` |
| A-OT-00 | OTA 冒烟 | A→B、B→A、中断 | 双向通过，中断保持旧槽 | **通过**：0x34/0x36/0x37 双向；中断后仍 B | `test/records/20261106-ota-smoke.log` |
| A-OT-01 | OTA 后 A/B 启动 | OTA 到 A/B 后复位 | 目标槽启动并确认 | **通过**：A/B 都被 trial 启动并 confirm | `test/records/20261106-ota-smoke.log` |
| A-OT-02 | 正常升级 | 10 次 | 10/10 成功 | 未执行（当前只做冒烟） |  |
| A-OT-03 | 50 次升级 | 循环 50 次 | 50/50 成功 | 待执行 |  |
| A-OT-04 | OTA 中断 | 20KB/73KB 时 Ctrl-C | 旧槽仍可启动 | **通过**：复位仍 B | `test/records/20261106-ota-smoke.log` |
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

### 2.6 Linux 网关

| ID | 测试项 | 方法 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-GW-01 | gatewayd 启动 | `rc.local` 启动 | 进程存在，socket 可连 | **通过** | `test/records/20261107-gateway-smoke.log` |
| A-GW-02 | gwctl 诊断 | status / dtc / read-did | JSON 正常 | **通过**：`online:true`、`62F1B000`、`5902FFC00208` | 同上 |
| A-GW-03 | SQLite | 记录 UDS/OTA | 可查询 | **通过** | 同上 |
| A-GW-04 | MQTT | PC 订阅 `gateway/#` | 收到 dtc/status/event | **通过** | 同上 |
| A-GW-05 | gatewayd OTA | `gwctl ota app_a.img` | `ota_sessions` 写 ok，复位后 A | **通过** | 同上 |
| A-GW-06 | 固定 IP | 重启后 eth0 | 保持 192.168.137.103 | **通过**（关闭 connman） | 同上 |
| A-RL-08 | MQTT 断线 | 停 broker 10 分钟 | 不崩溃，恢复后继续 | 待执行 |  |

### 2.7 最小可靠性（15 分钟）

| ID | 测试项 | 方法 | 通过标准 | 结果 | 日志 |
|---|---|---|---|---|---|
| A-RL-00 | 15 分钟连续运行 | gatewayd 周期 DTC + SQLite | 进程不重启，行数增长 | **通过**：rows 3014→3182，+168，PID 不变 | `test/records/20261107-reliability-15min.log` |
| A-OT-09 | 3 次 OTA | A→B、B→A、A→B | 3/3 成功，复位后切槽 | **通过**：3 次 `ok`；中间 2 次失败为未复位/采样点问题 | 同上 |
| A-RL-03 | bus-off 恢复 | can0 down/up | 3s 内恢复，gatewayd 不崩 | **通过**：恢复 ERROR-ACTIVE，status online | 同上 |
| A-RL-07 | gatewayd 重启 | pkill + nohup | 自动恢复 | **通过**：status online | 同上 |
| A-RL-05 | MQTT 断开 | 停 broker | 不崩溃，恢复后继续 | **通过（短时）**：停 broker 后 gatewayd 正常，重开恢复 | 同上 |
| A-RL-01 | 24h 连续运行 | 24h | 丢帧 <0.01% | 未执行 |  |
| A-OT-02 | 10 次 OTA | 10 次 | 10/10 | 未执行 |  |
| A-OT-03 | 50 次 OTA | 50 次 | 50/50 | 未执行 |  |
| A-OT-04 | 25%/50%/75% 掉电 | 多点掉电 | 回滚 | 未执行（仅做一次中断） |  |

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

---

## 附：UDS 0x27 / 0x2E 实测（2026-11-13）

固件：`My_AI_A` 新建 App A（version 2，image `app_a_027.img`，CRC `0x14ECD611`）
通过本项目的 OTA 通道写入非活动槽，trial 启动后 `22 F1B0` 从 `01` 变 `00`。

测试脚本：`test/uds_sec_test.py`（板上 python3 + 原生 CAN socket，发用 cansend）

```text
10 03                                   -> 50 03
22 F1A0                                 -> 62 F1 A0 01
2E F1A0 00（未解锁）                      -> 7F 2E 33
27 01                                   -> 67 01 <seed>
27 02 DEADBEEF（错 key）                  -> 7F 27 35
27 01                                   -> 67 01 <seed>
27 02 <正确 key>                          -> 67 02
2E F1A0 00                              -> 6E F1 A0
22 F1A0                                 -> 62 F1 A0 00
2E F1A1 88 13                           -> 6E F1 A1
22 F1A1                                 -> 62 F1 A1 88 13
2E F1A1 00 00（越界）                     -> 7F 2E 31
2E F190 41（只读 DID）                    -> 7F 2E 31
2E F1A0 01（恢复）                        -> 6E F1 A0
10 01 / 27 01                           -> 50 01 / 7F 27 7F
三次错 key                               -> 35 / 35 / 36，随后 37
```

结果：**14 项用例全部符合预期**。

## A/B 两槽一致性

App A 通过 `uds_cli` 写入（见上），App B 用同一份改动重新编译：

```text
app_a_027.img  CRC 0x14ECD611   app_b_027.img  CRC 0x55DFD06B
App B 写入后：22 F1B0 -> 62 F1 B0 01
App B 上重跑 14 项用例：全部符合预期
```

## OTA 期间的总线竞争（定位清楚了）

第一次 OTA 在 `offset 1024` 就 `0x36 failed`，抓包能看到 gatewayd 每 5 s 发的
`19 02 FF` 插在 0x36 传输中间。停掉 gatewayd 后一次通过。

第二次改用 `gwctl ota`（走 gatewayd 内部的 OTA 路径），**没有停 gatewayd 也一次通过**。
读 `gatewayd.c` 的主循环可以解释：

```text
while (!g_stop) {
    poll(&pfd, 1, 500);          /* 只等 unix socket */
    serve_client(c);             /* OTA 在这里同步跑完 */
    if (time(NULL) >= next_poll) periodic_poll();   /* 串行，OTA 期间不会执行 */
}
```

结论：

```text
gatewayd 单线程，OTA 和周期 DTC 天然互斥 → 用 gwctl ota 即可
uds_cli 是独立进程，和 gatewayd 各发各的 → 用它 OTA 前必须先停 gatewayd
```

