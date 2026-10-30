# 模块代码导入清单

> 来源：`D:\嵌入式学习\嵌入式软件开发\模块代码`
> 目标：`stm32-imx6ull-can-diag-ota/firmware`
> 原则：只导入源码和工程配置；不导入 `.metadata/`、`Debug/`、`Drivers/` 构建产物。

---

## 1. 采用 / 丢弃

| 模块 | 采用来源 | 目标目录 | 状态 |
|---|---|---|---|
| CAN 基础示例 | `can通讯/can_test/can_test` | `firmware/baseline/can_test` | 已导入 |
| CAN 回环示例 | `can通讯/can_loopback_silence/can_loopback_silence` | `firmware/baseline/can_loopback_silence` | 已导入 |
| DHT11 | `DHT11/DHT11`（较新，含位对齐/校验和） | `firmware/modules/dht11` | 已导入 |
| HC-SR501 | `HC-SR501/HC-SR501`（从 `gpio.c` 抽出） | `firmware/modules/pir` | 已导入 |
| FreeRTOS | `freertos/freertos` | `firmware/reference/freertos` | 参考 |
| AHT20 | `aht20/aht20`（LibDriver） | `firmware/reference/aht20` | B 参考 |
| AImate 集成工程 | `AImate/AImate` | `firmware/reference/AImate` | 协议/上报参考 |

丢弃：

- 所有 `.metadata/`（CubeIDE 工作区缓存）
- 所有 `Debug/`（`.o/.d/.elf/.map`）
- 顶层 `can_test/`、`can_loopback_silence/`（与 `can通讯/` 重复）
- `DHT11/dht/`（旧版，位对齐逻辑较早）
- `aht20/AHT15/`（不同型号，不进入主线）
- `can通讯/.git`（保留原仓库，不要把嵌套 git 带进新仓库）
- `AImate/FreeRTOS/`、`AImate/Drivers/`、`AImate/.metadata/`、`AImate/Debug/`（只保留协议/应用参考）

> **SocketCAN：没有。** 在 AImate 里搜索 `PF_CAN`、`SocketCAN`、`AF_CAN`、`can0` 均无结果；Linux 端代码需要新写。
>
> **光照：没有做。** 已从项目范围移除，不要再写“光照采集”。

---

## 2. 为什么只保留 Core + .ioc + .ld

旧工程每个目录 30–90 MB，主要是：

- `Drivers/STM32F4xx_HAL_Driver` 和 CMSIS；
- `Debug/` 构建产物；
- `.metadata/`。

新仓库只保留：

```text
Core/Inc/
Core/Src/
Core/Startup/
*.ioc
*.ld
```

复现方式：用 CubeMX 打开 `.ioc` → 生成代码 → 用仓库的 `Core` 覆盖 → 编译。

---

## 3. 关键冲突：PC1

- DHT11 原工程 DATA = **PC1**。
- HC-SR501 原工程 OUT = **PC1**。

A 项目要同时用两个传感器，**不能都接 PC1**。

约定：

- DHT11：PC1（`DATA_Pin` / `DATA_GPIO_Port`）
- PIR：**PE0**（`pir.c` 默认 `GPIOE/GPIO_PIN_0`，可用 CubeMX 标签覆盖）

接线前先确认 CubeMX 的引脚分配，避免生成代码后冲突。

---

## 4. 接下来要做的清理

原始 `模块代码` 还保留着 `.metadata`/`Debug`，约 330 MB。建议在确认新仓库能编译后：

1. 先备份/压缩 `模块代码`；
2. 删除各工程下的 `.metadata/`、`Debug/`；
3. 保留 `Core/`、`.ioc`、`.ld`、README；
4. 把顶层重复的 `can_test/`、`can_loopback_silence/` 移入 `_archive/` 或删除。

**不要现在直接删**，先确认新仓库的 baseline 能打开、能编译。

---

## 5. 导入后的验收

- [ ] `firmware/baseline/can_test` 能用 CubeMX 打开 `.ioc` 并生成工程
- [ ] `firmware/baseline/can_test/Core` 与旧工程一致（除 Drivers）
- [ ] DHT11 能读，校验和正确
- [ ] PIR 在 PE0 上能触发，预热/消抖/2 s 保持正常
- [ ] 没有 `.metadata`/`Debug`/`Drivers` 被提交
- [ ] README 写清楚引脚和冲突
