# STM32 + i.MX6ULL CAN 诊断与 OTA（01）：旧 CAN 项目基线审计，先别急着打 tag

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 新仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 旧仓库：[CAN-](https://github.com/longlin05/CAN-)

我本来想直接给旧工程打一个 `a-baseline` tag，然后开始写 ISO-TP。打开仓库后我发现：**baseline 还不完整。**

这篇记录三件事：

1. 旧 CAN 仓库里到底有什么；
2. 哪些能复用，哪些必须重写；
3. 为什么我现在不打 tag，以及要补哪几块。

---

## TL;DR

- **CAN**：初始化/滤波思路可复用；`can_test` 的主循环是“发一次、收一次”，不是周期协议，必须重写。
- **DHT11 / PIR**：已从本地模块代码导入；**光照没做过，已移出**。
- **SocketCAN**：AImate 里搜不到；后来从本地虚拟机拷入基础 `can_recv.c`，但还缺 epoll/双向/诊断。
- **AHT20**：A 项目不用，留给 B 项目做 i.MX6ULL 的 IIO 驱动。
- **结论**：先补源码，再打 `a-baseline`；tag 不能指向一个只有文档的 scaffold。

---

## 1. 审计对象

| 项目 | 内容 |
|---|---|
| 旧仓库 | `CAN-` @ `8a5d561bf74d9f5f27b0a7818d3b32c6da72beab` |
| 新仓库 | `stm32-imx6ull-can-diag-ota` @ `main` |
| STM32 | STM32F407VGT6-Disc |
| Linux 板 | i.MX6ULL mini EMMC |
| 总线 | CAN 500 kbps，双节点 |
| 工具 | STM32CubeIDE、candump、iproute2、Git |
| 目标 | 冻结一个“可复现”的 baseline，再进 ISO-TP/UDS |

---

## 2. 旧仓库里实际有什么

旧仓库 README 写得很清楚：

> 本工作区包含两个基于 STM32 HAL 的 CAN 通信示例工程，适合用于学习和验证 STM32F4 系列 MCU 的 CAN 收发功能。

目录里只有：

- `can_loopback_silence/`：CAN 静默回环示例；
- `can_test/`：CAN 发送/接收示例。

没有 DHT11、PIR、SocketCAN 的源码；也没有 AHT20。

这些模块后来从本地 `模块代码/` 抽到独立外设仓库 `DHT11-AHT20-RIR-Freertos`；SocketCAN 基础接收从本地虚拟机拷入 `gateway/src/`，但还没有 epoll/双向/诊断。导入清单见 `docs/module-import.md`。

### 2.1 `can_test` 的主循环

关键代码大概是这样：

```c
TxMessage.StdId = 0x321;
TxMessage.DLC = 8;
/* 填充 8 字节数据 */

HAL_Delay(1000);
CAN_Send_Msg(&TxMessage);

HAL_Delay(1000);
CAN_Receive_Msg(&RxMessage);

while (1) {
}
```

这段代码能证明“CAN 外设能初始化、能发一帧、能收一帧”，但它不是网关：

- 没有周期发送；
- 没有超时/重传；
- 没有错误帧/bus-off 处理；
- 没有协议解析；
- 没有队列/缓冲；
- 没有状态机。

所以 `can_test` 只能当 **bring-up 参考**，不能当项目主程序。

### 2.2 仓库里还有大量构建产物

旧仓库里能看到：

- `.metadata/`：STM32CubeIDE 工作区缓存；
- `Debug/`：`.o`、`.d`、`.su`、`.cyclo` 等构建产物；
- 多个 `Drivers/`、启动文件、链接脚本。

这些不需要全部搬进新仓库。导入时只保留：

```text
Core/Inc/
Core/Src/
Core/Startup/
*.ioc
*.ld
README.md
LICENSE
```

其余构建缓存不要提交。

---

## 3. 逐模块审计

| 模块 | 来源 | 现状 | 决定 | 下一步 |
|---|---|---|---|---|
| CAN | `CAN-` @ `8a5d561` `can_test` | 能初始化/单次收发 | **复用初始化/滤波思路** | 已导入 `firmware/baseline`；重写周期主循环 |
| DHT11 | `模块代码/DHT11/DHT11` | 能读，含位对齐/校验和 | **复用** | 已迁到外设仓库 `dht11/`；PC1，≥2s |
| PIR | `模块代码/AImate/.../driver_pir.*` | 预热/消抖/2s 保持 | **复用** | 已迁到外设仓库 `pir/`；默认 PC1，需改 PE0 |
| SocketCAN | 本地虚拟机 | 基础接收/解析可用 | **部分复用** | 已拷入 `gateway/src/`；补 epoll/双向/诊断 |
| AHT20 | `模块代码/AImate/.../driver_aht20.*` | LibDriver，STM32 参考 | **移出 A baseline** | 已迁到外设仓库 `aht20/`；给 B 参考 |

> A 项目的温湿度用 DHT11；AHT20 不在 A 的 baseline 里，留给 B 项目做 i.MX6ULL 的 IIO 驱动。两者不要混。

DHT11 是单总线，不是 I2C：

- 主机拉低至少 18 ms，再释放；
- 读 40 bit：湿度 16 + 温度 16 + 校验 8；
- 校验和 = 前 4 字节相加取低 8 位；
- 采样间隔建议 ≥ 2 s，baseline 的 `0x112` 就是 2 s 一帧。

---

## 4. 为什么现在不打 `a-baseline`

一个 tag 应该指向“别人 clone 下来就能复现”的代码。

现在新仓库里只有文档骨架：

- 没有 SocketCAN 源码（AImate 里也搜不到）；
- 没有 CAN 的 baseline 工程；
- `git tag` 为空；
- `docs/baseline-audit.md` 还是模板。

如果现在打 tag，读者点进去只能看到 README，不是 baseline。所以先补代码：

```bash
# 1. 导入源码（只保留 Core/Inc、Core/Src、.ioc、.ld）
git add firmware/ gateway/
git commit -m "chore: import working CAN baseline"

# 2. 填完 docs/baseline-audit.md
git add docs/baseline-audit.md
git commit -m "docs: complete baseline audit"

# 3. 先打 STM32 侧基线 tag（Linux 双向/诊断还没接）
git tag -a a-baseline-stm32 -m "STM32 baseline: CAN + DHT11 + PIR"
git push origin main
git push origin a-baseline-stm32

# 等 Linux epoll/双向命令/诊断接入后，再打完整的 a-baseline
```

**不是不冻结，是冻结条件没满足。**

---

## 5. 这次审计发现的 5 个问题

| # | 现象 | 证据 | 影响 | 处理 |
|---|---|---|---|---|
| 1 | CAN 主循环只发一次 | `can_test/main.c` 的 `CAN_Send_Msg` 只调用一次 | 不能做周期采集/网关 | 重写为定时任务 + 队列 |
| 2 | 没有版本号 | 串口只打印初始化信息 | 无法确认板子上跑的是哪版 | 加 `FW_VERSION`，串口/诊断都能读 |
| 3 | 没有错误帧统计 | `can_test` 没处理 `CAN_IT_ERROR` | 丢帧无法定位 | 加错误计数、bus-off 回调 |
| 4 | 构建产物进仓库 | `.metadata/`、`Debug/` | clone 很大、难读 | 新仓库只保留源码/工程文件 |
| 5 | SocketCAN 只有基础接收 | AImate 里没有；虚拟机代码只有 `can_recv` | 双机 baseline 不完整 | 补 epoll/双向/诊断，再打完整 `a-baseline` |

---

## 6. 下一步

### 第 1 步：补 baseline

- CAN/DHT11/PIR 已导入；SocketCAN 基础接收已拷入，仍缺 epoll/双向；
- 填 `docs/baseline-audit.md` 的来源表；
- 先打 `a-baseline-stm32`；
- epoll/双向命令接入后再打完整 `a-baseline`；
- 更新 README 的“已有什么/没有什么”。

### 第 2 步：baseline 加固

- 加错误帧统计 + bus-off 恢复；
- 加 `FW_VERSION`；
- DHT11 加校验和/超时/重试；
- 连续跑 10 分钟，错误帧为 0。

### 第 3 步：ISO-TP

- 先 SF；
- 再 FF/CF/FC；
- 4 KB 多帧传输 0 错误；
- N_Cr 超时能重传；
- 然后才写 02。

**02 先不发**：等 ISO-TP 4 KB 真的跑通，再写“SF/FF/CF/FC、BlockSize/STmin、N_Cr 超时”的实战记录。

---

## 7. 仓库 / 系列

- 新仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- 旧仓库：https://github.com/longlin05/CAN-
- 旧仓库 commit：`8a5d561bf74d9f5f27b0a7818d3b32c6da72beab`
- 系列目录：https://blog.csdn.net/longlin101/article/details/169562720
- 下一篇：ISO-TP 多帧（等 4 KB 跑通再发）

标签：`STM32` `CAN` `i.MX6ULL` `嵌入式Linux` `OTA`
