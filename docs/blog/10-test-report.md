# STM32 + i.MX6ULL CAN 诊断与 OTA（10）：测试报告——15 分钟稳定性、3 次 OTA，以及 CAN 采样点这个坑

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
2. Linux CAN 采样点 0.866，STM32 是 0.75
```

改成：

```bash
ip link set can0 down
ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100
```

之后成功。

---

## 4. 采样点这个坑

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

根因：

```text
Linux sample-point 0.866
STM32 sample-point 0.75
```

改成 0.75：

```bash
sudo ip link set can0 down
sudo ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100
```

立刻恢复。

这件事值得记住：

> **CAN 不只要 bitrate 一样，采样点也要匹配。**

---

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
