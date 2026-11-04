# STM32 应用（A 项目）

从已经跑通的 `My_AI` 工程整理而来，加入 ISO-TP 服务端（echo 阶段）。

## 功能

- DHT11 温湿度采集；
- HC-SR501 PIR；
- CAN 命令/ACK（`0x221/0x222/0x228`）；
- ISO-TP 服务端（`0x7E0/0x7E8`）；
- 收到完整 ISO-TP 消息后原样回显；
- CAN RX 中断 → 32 帧软件环形缓冲 → `IsoTpPort_Task` 1ms 消费；
- DHT11 4ms 临界区期间 CAN 中断仍可进（CAN IRQ 优先级 4，ISR 不调用 FreeRTOS API）。

## 目录

```text
Core/
├── Inc/                 # CubeMX 生成的头文件 + FreeRTOSConfig
├── Src/                 # CubeMX 生成 + can.c / dht11.c / main.c
├── CustomDrivers/
│   ├── app_cmd.c/h      # 0x221/0x222/0x228
│   ├── app_report.c/h   # 0x111/0x112/0x113
│   ├── isotp.c/h        # 平台无关 ISO-TP 核心（与 gateway 同源）
│   ├── isotp_port.c/h   # HAL CAN + 1ms 任务 + echo
│   └── driver_pir.c/h
├── Startup/
├── Docs/
├── My_AI.ioc
└── STM32F407VGTX_FLASH.ld
```

## 构建

仓库不含 `Drivers/`、`Middlewares/`、`Debug/`。用 STM32CubeIDE/CubeMX 打开：

```text
firmware/app/My_AI.ioc
```

生成 HAL/CMSIS/FreeRTOS 后编译下载。

## ISO-TP 参数

```text
CAN：11 位标准帧，500 kbps，Normal addressing
请求：0x7E0（STM32 接收）
响应：0x7E8（STM32 发送）
最大长度：4095
padding：0xCC
N_Bs / N_Cr：1000 ms
默认 BS=8，STmin=5 ms（可用 IsoTpPort_SetFlowControl 修改）
任务周期：1 ms
```

## 已验证

- 真实 CAN 20 字节回显；
- 真实 CAN 4095 字节回显，DHT11 每 2s 并发；
- BS=8 / STmin=5ms：585 CF，74 FC，`rx_ovf=0`，`tx drops=0`；
- 异常注入：
  - 丢 FC → gw `TX_TIMEOUT`，STM32 `N_Cr` 超时；
  - 错序 CF → SN mismatch + `FC OVFLW`；
  - 丢最后一个 CF → STM32 `N_Cr` 超时。

## 注意

- `isotp.c/h` 是 STM32 实测快照；`gateway/src/isotp.c` 后续又加了 DLC/发送失败防护，两边需要再同步一次并重跑 4095。
- 当前只做 echo，UDS 服务层是下一步。
