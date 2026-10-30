# AImate 参考（旧集成工程）

来源：`模块代码/AImate/AImate/`。

这是一个已经跑通的 STM32 集成工程，包含：

- `Core/CustomDrivers/can_protocol.h`：CAN 应用层协议定义；
- `Core/CustomDrivers/app_report.c/h`：PIR/温湿度采集与上报调度；
- `Core/CustomDrivers/driver_pir.c/h`：PIR 驱动；
- `Core/CustomDrivers/driver_aht20.c/h`、`aht20_port.c/h`：AHT20 驱动/移植层；
- `Core/can.c/h`：CAN 发送/接收；
- `Docs/CAN_Protocol.md`：完整协议说明。

## 重要事实

- 当前实现是 **STM32 → Linux 单向上报**：
  - `0x111` PIR；
  - `0x112` 温湿度；
  - `0x113` 心跳。
- `0x221/0x222/0x228`（Linux → STM32）**只预留了 ID 和帧格式，MCU 端尚未实现**。
- **没有 SocketCAN/Linux 端源码**；在 AImate 目录里搜索 `PF_CAN`、`SocketCAN`、`can0` 均无结果。

## 用途

- 作为 A 项目的真实 baseline 参考，而不是重新发明 `0x301` 协议；
- 后续要补的增量：
  1. Linux → STM32 命令/ACK（`0x221/0x222/0x228`）；
  2. UDS/ISO-TP 诊断；
  3. DTC；
  4. Bootloader/OTA。
- `Docs/CAN_Protocol.md` 与 `can_protocol.h` 必须保持一致；改协议时同步改两处。

## 注意

- 这是 STM32 工程参考，不含 `Drivers/`、`FreeRTOS/`、`Debug/`；
- A 项目最终用 DHT11，不是 AHT20；AHT20 代码只作 B 项目参考。
