# STM32 + i.MX6ULL CAN 诊断与 OTA（10）：测试报告——15 分钟稳定性、3 次 OTA，以及 CAN 采样点这个坑

> **2026-11-13 修订**：第 4 节「采样点」的结论下早了，已按复测数据改写。
> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[09 i.MX6ULL 驱动：DHT22 IIO + PIR 字符设备](https://blog.csdn.net/longlin101/article/details/170347113)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v1.0`
> 已发布：https://blog.csdn.net/longlin101/article/details/170347116

最后一篇不是再加功能，而是把前面的东西变成数据。

先说结论：

```text
功能闭环        通过
3 次 OTA       通过
bus-off 恢复   通过
gatewayd 重启  通过
MQTT 断开恢复  通过
15 分钟稳定    通过
24h / 50 次    未执行
```

---

## 1. 测试环境

```text
STM32F407VGT6 + Bootloader + App A/B
i.MX6ULL ATK-IMX6U
Linux 4.1.15-g3dc0a4b
can0 500 kbps
sample-point 0.750
gatewayd + SQLite + MQTT
PC mosquitto broker 192.168.137.1:1883
```

---

## 2. 功能矩阵

| 项目 | 结果 |
|---|---|
| CAN / ISO-TP 单帧 | 通过 |
| ISO-TP 4095 字节 | 通过 |
| UDS `0x10/0x3E/0x22` | 通过 |
| DTC `0x19/0x14` | 通过 |
| DTC 掉电保存 | 通过 |
| Bootloader A/B | 通过 |
| OTA `0x34/0x36/0x37` | 通过 |
| gatewayd / gwctl | 通过 |
| SQLite | 通过 |
| MQTT | 通过 |
| F1B0 当前槽 | 通过 |
| DHT22 IIO | 通过 |
| PIR `/dev/pir` | 基本通过，硬件抖动 |

---

## 3. 三次 OTA

```text
A -> B   ok
B -> A   ok
A -> B   ok
```

`ota_sessions`：

```text
2026-11-07T13:03:52|0x08060000|73660|ok|transfer complete
2026-11-07T13:01:46|0x08020000|73660|ok|transfer complete
2026-11-07T12:57:42|0x08060000|73660|ok|transfer complete
```

中间有两次失败：

```text
2026-11-07T12:58:54|0x08020000|73660|failed|0x36 failed
2026-11-07T12:54:04|0x08020000|73660|failed|0x34 failed
```

原因：

```text
1. OTA 后没复位，又发了下一次
2. 总线错误状态没清干净（当时的判断记成"采样点不一致"，见第 4 节的修订）
```

改成：

```bash
ip link set can0 down
ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100
```

之后成功。

---

## 4. 采样点这个坑（2026-11-13 修订）

现象：

```text
STM32 能收 FF
STM32 能发 FC
STM32 发 0x74 时 ACK error
CAN tx: no free mailbox
```

Linux：

```text
can state ERROR-ACTIVE
tx errors 0
```

看起来总线很好，但 STM32 一直 ACK error。

**当时的判断**：

```text
Linux sample-point 0.866
STM32 sample-point 0.75
```

改成 0.75 之后恢复，于是写下"必须匹配 0.75"。

---

### 11 月 13 日复测：这个因果下早了

后来把固件重新算了一遍，又做了两种采样点的对比实验：

```text
按当前源码：PSC 6 / BS1 11 / BS2 2 → (1+11)/14 = 85.7%
（11 月初那版固件确实是 0.75，后来重新生成过，两个数都对，只是固件换了）
```

同一块板子、同一段线，两种采样点各跑 10 次 `read-did`：

```text
can0 sample-point 0.866（被 round 成 0.850）   8 ok / 2 timeout
can0 sample-point 0.750                       9 ok / 1 timeout
```

**两种都能通**，差距在噪声里。

再看串口日志（MCU 侧）：

```text
21:19:39.433 ISOTP rx: SF 3B
21:19:39.433 ISOTP tx: UDS response started 4B -> 0x7E8
```

收和发在**同一毫秒**，说明 MCU 一直是即时响应的。

而出现秒级延迟的那些时段，`can0` 的状态是：

```text
can state ERROR-WARNING (berr-counter tx 74 rx 122)
bus-off 累计 18 次、error-passive 27 次
```

### 修正后的结论

```text
1. 那一次"改 0.75 就恢复"里，真正起作用的大概率是 down/up：
   清掉 bus-off 和错误计数、重新进入 ERROR-ACTIVE
2. 采样点仍然要匹配——差太多一定出问题——但"必须 0.75 才通"不成立
3. 秒级延迟来自总线错误恢复（重传/错误帧/退避），不是应用层
4. 下一步该查物理层：两端 120Ω、走线、共地
```

值得记住的改成：

> **现象消失 ≠ 找到根因。**
> 一次改动里如果同时做了 down/up 和改参数，先怀疑"重置"，再怀疑"参数"。

## 5. bus-off 恢复

```bash
ip link set can0 down
sleep 2
ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100
```

结果：

```text
can0 ERROR-ACTIVE
gwctl status online:true
```

gatewayd 不崩。

---

## 6. MQTT 断开

PC 停 broker：

```text
gatewayd 继续运行
gwctl dtc 仍正常
MQTT 发不出去
```

重启 broker 后恢复。

---

## 7. gatewayd 重启

```bash
pkill -f gatewayd
nohup ./gatewayd -c gatewayd.conf >/var/log/gatewayd.log 2>&1 &
```

结果：

```text
gwctl status online:true
SQLite 继续追加
MQTT 恢复
```

---

## 8. 15 分钟 soak

开始：

```text
13:24:53 rows=3014
pid=12098
```

结束：

```text
13:38:55 rows=3182
pid=12098
+168 rows
```

增长：

```text
约 12 rows/min
```

资源：

```text
CPU 0.0%
RSS 2092 KB
DB 179K
```

说明：

```text
gatewayd 未重启
SQLite 持续增长
CPU/RSS 稳定
```

---

## 9. 没做的

```text
24h / 72h
50 次 OTA
25%/50%/75% 掉电
MQTT 补传
```

不要写“50/50”，没有就是没有。

（安全访问 0x27 与写 DID 0x2E 已经在 11-13 补上，不在"没做"之列。）

---

## 10. 项目结论

这个项目做到了：

```text
CAN 能通
ISO-TP 多帧
UDS 诊断
DTC 掉电保存
A/B Bootloader
OTA 升级/回滚
Linux 网关
SQLite/MQTT
i.MX6ULL 内核驱动
```

还没做到：

```text
量产级 24h/50 次可靠性
安全访问/签名
差分/断点续传
```

---

## 11. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v1.0`
- 测试报告：`docs/test-report.md`
- 原始日志：`test/records/20261107-reliability-15min.log`

---

## 12. 系列结束之后

A 系列到这里结束。之后的 i.MX6ULL 设备树改造、platform driver、
以及按 IIO 子系统规范重写驱动，放在独立系列：

- 系列目录：[i.MX6ULL 驱动笔记（00）：这个系列写什么](https://blog.csdn.net/longlin101/article/details/170550740)
- 第一篇：[（01）把 insmod 的 gpio_num 换成设备树](https://blog.csdn.net/longlin101/article/details/170550878)

标签：`STM32` `i.MX6ULL` `CAN` `UDS` `OTA` `测试`
