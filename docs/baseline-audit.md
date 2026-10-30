# Baseline 审计：旧 CAN 项目哪些能复用

> 目的：在写新功能之前，先把已有代码“冻结”成基线。
> 产出：一份可复现的现状说明 + 一个 `a-baseline` tag。
> 原则：**能跑的先不重写；先记录坑，再决定改不改。**

---

## 1. 审计范围

| 项目 | 位置 | 说明 |
|---|---|---|
| STM32 固件 | 旧 `CAN-` 仓库 / 本地工程 | CAN、DHT11、PIR、USART |
| i.MX6ULL 程序 | 旧仓库 / 板端 | SocketCAN 收发 |
| 硬件 | STM32F407VGT6-Disc、i.MX6ULL mini、CAN 模块 | 接线、终端电阻、供电 |
| 文档 | 旧 README、CSDN 文章 | 哪些还能用，哪些过时 |

### 1.1 已确认：`CAN-` 仓库里实际有什么

`CAN-` @ `8a5d561bf74d9f5f27b0a7818d3b32c6da72beab`：

- `can_loopback_silence/`：STM32 CAN 静默回环示例；
- `can_test/`：STM32 CAN 发送/接收示例；
- 仓库里**没有** DHT11、PIR、SocketCAN 的源码；
- 有大量 `.metadata/`、`Debug/` 构建产物，导入新仓库时要排除。

所以“复用 DHT11/PIR/SocketCAN”需要补来源：

| 模块 | 来源 | 现状 |
|---|---|---|
| CAN | `CAN-` @ `8a5d561` | 已导入 `firmware/baseline`；`can_test` 主循环只发一次，需重写 |
| DHT11 | `模块代码/DHT11/DHT11` | 已导入 `firmware/modules/dht11`；PC1，≥2s 采样 |
| PIR | `模块代码/HC-SR501/HC-SR501` | 已导入 `firmware/modules/pir`；默认 PE0，轮询/消抖 |
| SocketCAN | 待确认：本地工程/板端 | 仍缺；需要导入/重构成 `gateway/can_rx` |

> 博客里写“复用”之前，先把这张表的“来源”填上；否则读者无法验证。

---

## 2. 审计步骤

### Step 1：把旧代码跑起来

```bash
# 旧仓库
git clone <old-repo-url>
cd <old-repo>

# 记录当前 commit
git rev-parse HEAD
git log --oneline -10
```

在板子上确认：

- STM32 能启动、串口有日志；
- 传感器能读；
- 双机 CAN 能收发；
- i.MX6ULL `candump` 能看到帧。

### Step 2：记录“能跑”的证据

| 证据 | 怎么留 |
|---|---|
| 串口日志 | `minicom`/`picocom` 保存到 `test/records/` |
| CAN 抓包 | `candump -ta can0 > can.log` |
| 总线状态 | `ip -details -statistics link show can0` |
| 版本 | `git rev-parse HEAD`、固件版本号 |
| 接线 | 拍一张带标注的照片 |
| 波形 | 示波器/逻辑分析仪截图（如有） |

### Step 3：逐模块打标签

| 模块 | 现状 | 复用决定 | 动作 |
|---|---|---|---|
| `bsp_can` | 能收发 | **复用** | 补错误帧统计、bus-off 恢复 |
| `dht11` | 能读 | **复用** | 补校验和/超时错误码，接 DTC；采样间隔 ≥2s |
| `light` | 能读 | **复用** | 补越界判断 |
| `pir` | 能触发 | **复用** | 补消抖/事件计数 |
| `usart` | 能打印 | **复用** | 只作为调试口，不再扩展 |
| 应用协议 | `0x301/0x401` 等 | **复用帧格式** | 新增 ISO-TP/UDS，不推翻旧帧 |
| SocketCAN 程序 | 能收发 | **部分复用** | 重构成网关的 `can_rx` 模块 |
| 旧 README | 教程式 | **不直接复用** | 重写为项目 README |

### Step 4：标记“不要”的代码

以下情况不要带进新仓库：

- 教程里复制但你没理解的代码；
- 大段 `HAL_Delay` 阻塞逻辑；
- 中断里做业务/打印；
- 全局变量满天飞、没有模块边界；
- 只在一个 `.c` 里堆 1000 行的“万能 main”；
- 厂商 SDK 的 build 产物、`.o/.elf/.bin`。

> 不是让你重写，而是**先隔离**：能用的放 `firmware/drivers/`，不确定的放 `legacy/`，不要污染新模块。

---

## 3. 基线验收

- [ ] STM32 能启动，串口有版本日志
- [ ] DHT11 能读，校验和正确，数据合理
- [ ] 光照/PIR 能读
- [ ] 双机 CAN 500 kbps 能收发
- [ ] `candump` 连续 10 分钟无错误帧
- [ ] i.MX6ULL 能跑 SocketCAN 程序
- [ ] 接线照片/抓包日志已保存
- [ ] 旧 commit 已记录
- [ ] 打 tag：`a-baseline`

---

## 4. 已知问题清单（填写）

| 编号 | 现象 | 影响 | 是否阻塞新功能 | 计划 |
|---|---|---|---|---|
| B-01 | 例如：偶尔丢帧 | 影响 24h 测试 | 否 | 第 1 周修 |
| B-02 | 例如：错误帧没统计 | 无法做可靠性 | 是 | 第 1 周补 |
| B-03 | 例如：DHT11 偶发校验错 | DTC 触发 | 否 | 第 2 周加 retry |
| B-04 |  |  |  |  |
| B-05 |  |  |  |  |

---

## 5. 基线之后的第一件事

不要马上写 UDS。先做两件小事：

1. **加错误帧统计**：`CAN_IT_ERROR` / `CAN_IT_BUSOFF` 回调，记录计数。
2. **加版本号**：固件里定义 `FW_VERSION`，通过串口和 `0x22 0x0100` 都能读到。

这两件事是后面诊断和 OTA 的基础。

---

## 6. 提交规范

```text
chore: import working CAN baseline
docs: add baseline audit and wiring
feat(can): add error counter and bus-off recovery
feat(uds): add 0x10/0x3E/0x22 services
```

第一个 commit 只放“能跑的基线”，不要在同一个 commit 里混入新功能。
