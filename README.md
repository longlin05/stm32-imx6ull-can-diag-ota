# STM32 + i.MX6ULL | CAN 诊断与 OTA 闭环系统

> 从“CAN 能通”升级到“可诊断、可升级、可恢复、可度量”。

**Status:** 🚧 仓库骨架 / 进行中。代码和测试数据会按里程碑逐步提交，未完成项不会写成“已实现”。

---

## 1. 项目定位

这是一个**增量项目**，不是从零重写。

基线已经具备：

- STM32F407 与 i.MX6ULL 双机 CAN 500 kbps 通信；
- DHT11 温湿度、光照、HC-SR501 人体感应采集；
- Linux SocketCAN 收发程序。

本项目在此基础上增加三个闭环：

| 闭环 | 新增能力 | 结果 |
|---|---|---|
| 诊断闭环 | ISO-TP 多帧 + UDS 子集 + DTC | 能读版本、读/清故障码、保持诊断会话 |
| 升级闭环 | CAN Bootloader + A/B 分区 + CRC32 + 回滚 | 能通过 CAN 升级，掉电不变砖 |
| 可靠性闭环 | bus-off 恢复、故障注入、24h/72h 测试 | 用数据证明稳定，而不是“目测能跑” |

一句话：

> 基于 STM32F407 + i.MX6ULL 的车载风格 CAN 诊断与 OTA 闭环系统。

---

## 2. 系统架构

```mermaid
flowchart LR
    subgraph NODE["STM32F407 节点"]
        S["Sensors: DHT11 / Light / PIR"]
        U["UDS Server + ISO-TP"]
        D["DTC Manager"]
        B["Bootloader / A-B Slots"]
    end

    subgraph BUS["CAN Bus 500 kbps"]
        C["CANH / CANL + 120Ω x2"]
    end

    subgraph GW["i.MX6ULL Linux 网关"]
        SK["SocketCAN"]
        CLI["Diagnostic CLI"]
        OTA["OTA Server"]
        DB[("SQLite")]
        MQ["MQTT"]
    end

    S --> U
    U <--> C
    C <--> SK
    SK --> CLI
    SK --> OTA
    SK --> DB
    DB --> MQ
    OTA --> B
```

核心链路：**STM32 节点负责采集、诊断服务、DTC 和 Bootloader；i.MX6ULL 负责诊断客户端、OTA 服务端、日志和上报。**

---

## 3. 相比 baseline 新增什么

| 能力 | Baseline（已有） | 本项目（新增） |
|---|---|---|
| CAN 传输 | STM32→Linux 500 kbps 已通；Linux→STM32 命令/ACK 已通 | 错误帧统计、bus-off 恢复、总线负载/延迟测试 |
| 应用协议 | AImate `0x111/0x112/0x113`；`0x221/0x222/0x228` 已实现 | ISO-TP 多帧 + UDS 诊断 + DTC |
| 诊断 | 无 | `0x10 / 0x3E / 0x22 / 0x19 / 0x14` |
| 故障管理 | 无 | 5 类 DTC、状态位、掉电存储、读/清 |
| 升级 | 无 | CAN Bootloader、A/B 分区、CRC32、断点续传、回滚 |
| Linux 网关 | `my_ai`：SocketCAN 收发、命令/ACK、丢包/RTT 统计 | epoll、ISO-TP/UDS 客户端、诊断 CLI、OTA 服务端、SQLite、MQTT、systemd |
| 证据 | 博客/功能演示 | 测试报告 + 故障注入 + 24h/72h 数据 + demo |

**Baseline 只保留一句简历描述，不占主 bullet。** 主 bullet 全部留给新增能力。

### Baseline 来源

| 模块 | 来源 | 位置 | 备注 |
|---|---|---|---|
| CAN | `CAN-` @ `8a5d561` | `firmware/baseline/` | STM32 示例；主循环需重写 |
| DHT11 / AHT20 / PIR / FreeRTOS | 旧外设工程 | 独立仓库：[DHT11-AHT20-RIR-Freertos](https://github.com/longlin05/DHT11-AHT20-RIR-Freertos) | 已迁出，主仓库不重复维护 |
| AImate 协议/上报 | `模块代码/AImate` | `firmware/reference/AImate/` | `0x111/0x112/0x113` 参考；双向命令/ACK 已在 `My_AI` 工程补全 |
| SocketCAN / ISO-TP | 本地虚拟机 `gateway` | `gateway/` | ISO-TP 核心 + vcan 自测；UDS 下一步 |

> 通信层已完成：1000/1000 命令、0 重试、P99 22.06 ms，tag `a-v0.2-bidirectional`。
> ISO-TP 核心已入库：SF/FF/CF/FC，selftest 覆盖 1/7/8/20/1000/4095 字节。
> 导入清单和清理规则见 [`docs/module-import.md`](docs/module-import.md)。

---

## 4. 项目范围（A 方案）

### 4.1 UDS 子集

| 服务 | 名称 | 说明 |
|---|---|---|
| `0x10` | DiagnosticSessionControl | 默认会话 / 扩展会话 |
| `0x3E` | TesterPresent | 保持会话 |
| `0x22` | ReadDataByIdentifier | 读版本、运行时间、错误计数 |
| `0x19` | ReadDTCInformation | 子功能 `0x02`，按状态掩码读 DTC |
| `0x14` | ClearDiagnosticInformation | 清除 DTC |

可选进阶（不作为本期必做）：

- `0x27` SecurityAccess：简化 seed/key，仅演示流程，不宣称量产安全；
- `0x34/0x36/0x37`：UDS 方式 OTA。先完成自定义 OTA，再考虑包装。

### 4.2 DTC

| DTC | 含义 | 触发条件 |
|---|---|---|
| `0xC001` | DHT11 超时/校验和错 | 单总线连续失败 3 次 |
| `0xC002` | CAN bus-off | 检测到 bus-off |
| `0xC003` | OTA CRC 失败 | 升级包校验失败 |
| `0xC004` | 传感器越界 | ADC/温湿度超出阈值 |
| `0xC005` | Flash 错误 | 擦写/校验失败 |

### 4.3 OTA

- 升级只写非活动分区；
- CRC32 + 向量表校验；
- 支持断点续传；
- trial / confirm / rollback；
- 验收：50 次升级、25%/50%/75% 掉电测试。

### 4.4 明确不做

- 不做完整 AUTOSAR；
- 不做功能安全认证；
- 不做完整 UDS 全服务；
- 不把“简化 security access”宣传成安全方案。

---

## 5. 硬件

| 硬件 | 作用 | 备注 |
|---|---|---|
| STM32F407VGT6-Disc | CAN 节点 / UDS Server / Bootloader | 具体引脚按原理图确认 |
| i.MX6ULL mini EMMC | Linux 网关 / 诊断客户端 / OTA Server | 板载 CAN 或 MCP2515 |
| CAN 收发器 ×2 | 物理层 | 优先 SN65HVD230 / TJA1051T/3（3.3V） |
| DHT11 | 温湿度 | 单总线 GPIO，3.3V，4.7k–10k 上拉 |
| HC-SR501 | 人体感应 | 5V 供电，输出 3.3V，共地 |
| 120Ω ×2 | 终端电阻 | 总线两端各一个 |
| USB-TTL | 调试串口 | 115200 |
| 示波器/逻辑分析仪 | 底层调试 | 排查晶振、SPI、CAN 波形 |

> DHT11 是单总线，采样间隔建议 ≥ 2 s；baseline 的 `0x112` 就是 2 s 上报一帧。
> 光照传感器未做，已从本项目移除；不要再写“光照采集”。
>
> 详细接线与 bring-up 步骤见 [`docs/bringup.md`](docs/bringup.md)，旧代码审计见 [`docs/baseline-audit.md`](docs/baseline-audit.md)。

---

## 6. 仓库结构

```text
.
├── README.md
├── .gitignore
├── docs/
│   ├── README.md
│   ├── scope-a.md
│   ├── baseline-audit.md
│   ├── protocol.md
│   ├── test-report.md
│   └── blog-series.md
├── firmware/
│   ├── app/           # STM32 应用：ISO-TP echo、命令/ACK、传感器
│   ├── baseline/      # 旧 CAN 示例
│   └── reference/     # AImate 参考
├── bootloader/        # STM32 Bootloader：A/B、CRC、跳转、回滚
├── gateway/           # i.MX6ULL Linux 网关：SocketCAN、诊断、OTA、SQLite、MQTT
├── test/              # 测试脚本、测试记录、故障注入
└── tools/             # 抓包、诊断 CLI、辅助脚本
```

每个目录的职责见对应 `README.md`。

> **项目 B（i.MX6ULL 传感器驱动）已独立成仓库：**
> https://github.com/longlin05/imx6ull-dht22-pir-drivers
>
> 驱动代码、设备树改造和对应测试都搬过去了；本仓库的 `b-v0.1` / `b-v0.2`
> tag 里仍保留着当时的代码，但新的提交不会再出现 `drivers/`、`dts/`。

---

## 7. 快速开始（占位）

> 代码尚未提交，以下命令用于说明最终的使用方式，后续按里程碑更新。

### 7.1 依赖

```bash
sudo apt install can-utils sqlite3 libsqlite3-dev libmosquitto-dev mosquitto
```

### 7.2 启动 CAN

```bash
sudo ip link set can0 down
sudo ip link set can0 up type can bitrate 500000 sample-point 0.875
ip -details -statistics link show can0
candump -ta can0
```

### 7.3 编译网关

```bash
cd gateway
# 本机语法验证 / vcan
make
make test

# 交叉编译到 i.MX6ULL：先 source NXP SDK 环境
. /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
make
```

产物：

```text
gateway/gw
gateway/mock_ecu
gateway/selftest
```

### 7.4 运行

```bash
scp gateway/gw root@<board-ip>:/home/root/
ssh root@<board-ip>
chmod +x /home/root/gw
sudo /home/root/gw can0
```

---

## 8. 测试状态

| 测试 | 目标 | 状态 |
|---|---|---|
| ISO-TP 单帧/多帧 | 4095 字节 0 错误 | **真实 CAN 4095 通过**：585 CF，rx_ovf=0；selftest 覆盖 1/7/8/20/100/1024/4095 |
| ISO-TP BS/STmin | BS=8 / STmin=5ms | **真实 CAN 通过**：74 个 FC，块内 STmin 校验通过 |
| ISO-TP 异常 | 丢 CF / 错序 / N_Bs / N_Cr | **真实 CAN 通过**：drop-fc / mangle-cf / drop-last-cf；selftest 9 用例 |
| DHT11 并发 | 4095 收发时每 2s 读 DHT11 | **通过**：rx_ovf=0、tx drops=0 |
| UDS `0x10/0x3E/0x22` | 响应 < 50 ms | **通过**：`10 01/03/04`、`3E 00`、`F180`、`F190`、配置 DID、100/100（2.034s）、S3 5s；首次卡顿已修复 |
| UDS `0x19/0x14` | 5 类 DTC 可读/可清 | **真实 CAN 通过**：`19 02/0A/01`、C001=0x0D、C002=0x08、`14` 清除；置位/清除掉电保存（sector 7 已验证，迁 sector 4 后待复测） |
| Bootloader A/B | App A/B、trial、rollback、confirm | **通过**：A 启动、trial B、未确认回滚 A、确认 B 固定启动 |
| OTA `0x34/0x36/0x37` | 写非活动槽、trial、回滚 | **冒烟通过**：A↔B 双向、中断后保持旧槽；10/50 次未做 |
| Linux 网关 | gatewayd/gwctl/SQLite/MQTT/OTA | **冒烟通过**：PC 收到 MQTT、SQLite 有记录、gatewayd OTA 成功 |
| 最小可靠性 | 15min soak + 3 次 OTA | **通过**：+168 行、PID 不变、CPU 0%、RSS 2092KB；24h/50 次未做 |
| i.MX6ULL 驱动 | DHT22 IIO + PIR 字符设备 | **通过**（v0.2 platform driver + DTS）：probe `from device tree`、19.6°C/50.5%RH、PIR read 5 次/60s、select 3 次事件、rmmod/insmod 2 轮无告警；详见 `docs/test-report-b-drivers.md` |
| 24h 稳定性 | 丢帧 < 0.01%，P99 < 50 ms | 待实现 |
| bus-off 恢复 | 3 s 内恢复 | 待实现 |

**未测试的项目不会写成“已通过”。**

---

## 9. Roadmap

- [x] 创建仓库骨架
- [x] 完成 baseline 审计，打 `a-baseline-stm32`
- [x] 双向命令/ACK，打 `a-v0.2-bidirectional`
- [x] ISO-TP：SF / FF / CF / FC（Linux 核心）
- [x] ISO-TP：BS/STmin、N_Bs/N_Cr、错序处理
- [x] ISO-TP：STM32 `isotp_port` + 双机 4095 字节（已归档 `firmware/app`）
- [x] UDS：`0x10 / 0x3E / 0x22`（会话/保活/读 DID/负响应/100 次通过）
- [x] UDS：`0x19 / 0x14` + DTC（C001/C002/清除/Flash 持久化通过；DTC 已迁 sector 4，待复测）
- [x] Bootloader：A/B、CRC、Metadata、跳转、trial/rollback/confirm 已实测通过
- [x] OTA：`0x34/0x36/0x37` 写非活动槽（A↔B 冒烟通过；10/50 次未做）
- [x] Linux 网关：gatewayd + gwctl + SQLite + MQTT + OTA + F1B0（冒烟通过；24h 未做）
- [ ] 故障注入 + 24h/72h 测试
- [ ] README、demo 视频、测试报告
- [ ] 博客系列收尾

---

## 10. 文档

- [项目 A 范围](docs/scope-a.md)
- [Baseline 审计](docs/baseline-audit.md)
- [模块代码导入清单](docs/module-import.md)
- [CAN / ISO-TP / UDS / OTA 协议](docs/protocol.md)
- [测试记录模板](docs/test-report.md)
- [博客系列索引](docs/blog-series.md)

---

## 11. 博客系列

CSDN：`blog.csdn.net/longlin101`

系列索引见 [`docs/blog-series.md`](docs/blog-series.md)。

已发布：

1. [00 从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
2. [01 基线审计：从“能通”到“可复现”](https://blog.csdn.net/longlin101/article/details/169563492)
3. [02 双向命令/ACK：1000 次压测与 50 ms 超时假丢包](https://blog.csdn.net/longlin101/article/details/169950471)
4. [03 ISO-TP：从 0 到 4095 字节，以及 30% 丢帧是怎么修掉的](https://blog.csdn.net/longlin101/article/details/170053591)
5. [04 UDS 最小闭环：0x10/0x3E/0x22，会话、S3 与读 VIN](https://blog.csdn.net/longlin101/article/details/170145581)
6. [05 UDS DTC：0x19/0x14，从故障检测到 Flash 掉电保存](https://blog.csdn.net/longlin101/article/details/170192523)
7. [06 CAN Bootloader：A/B 分区、CRC32、跳转与回滚](https://blog.csdn.net/longlin101/article/details/170201376)
8. [07 OTA 实战：0x34/0x36/0x37，A/B 双向升级与中断保持旧槽](https://blog.csdn.net/longlin101/article/details/170201534)
9. [08 Linux 网关：SocketCAN + gatewayd + SQLite + MQTT](https://blog.csdn.net/longlin101/article/details/170245246)

计划：

```text
09 i.MX6ULL 驱动：DHT22 IIO + PIR 字符设备（本地稿，待发布）   ← 本系列的桥梁篇
10 测试报告：24h/72h、P99、bus-off 数据
```

> 设备树 / platform driver 之后的内容不放在本系列，移到独立系列
> 「i.MX6ULL 驱动笔记」，代码与文章都在
> [imx6ull-dht22-pir-drivers](https://github.com/longlin05/imx6ull-dht22-pir-drivers)。

---

## 12. 说明

- 这是一个学习/求职作品，不是量产 AUTOSAR/UDS 协议栈；
- 安全访问（如果实现）是简化流程，不代表量产安全；
- 所有性能数字以 `docs/test-report.md` 的实测记录为准；
- 基线代码来自之前的 CAN 实验，新增代码会单独提交，便于看出增量。
