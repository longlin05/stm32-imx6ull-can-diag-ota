# firmware

STM32F407 应用固件：CAN baseline、AImate 协议参考、ISO-TP 服务端、后续 UDS/DTC/OTA。

## 目录

```text
firmware/
├── baseline/                 # 旧 CAN 工程（只保留 Core + .ioc + .ld）
│   ├── can_test/
│   └── can_loopback_silence/
├── app/                      # A 项目 STM32 应用（从 My_AI 整理）
│   ├── Core/Inc
│   ├── Core/Src
│   ├── Core/CustomDrivers   # app_cmd/app_report/isotp/isotp_port/PIR
│   ├── Core/Startup
│   ├── Docs
│   ├── My_AI.ioc
│   └── STM32F407VGTX_FLASH.ld
├── reference/
│   └── AImate/               # 旧集成工程：协议、app_report、can.c、PIR/AHT20 驱动
└── README.md
```

## 外设模块去哪了

DHT11、AHT20、HC-SR501、FreeRTOS 的独立代码已经移到单独仓库：

```text
https://github.com/longlin05/DHT11-AHT20-RIR-Freertos
```

主仓库只保留 CAN baseline、AImate 协议/上报参考和后续诊断/OTA 代码，避免两个仓库重复维护同一份外设驱动。

## 重要说明

- 没有提交 `Drivers/`、`Debug/`、`.metadata/`；HAL/CMSIS 用 CubeMX 按 `.ioc` 生成。
- `baseline/can_test` 只发一次、收一次，是 bring-up 示例，不是最终网关。
- `app` 是 A 项目实际 STM32 应用：CAN 命令/ACK + ISO-TP + UDS 最小服务端。
- `app/Core/CustomDrivers/isotp.c` 是 STM32 实测快照；`gateway/src/isotp.c` 后续加了防护，两边需再同步并重跑 4095。
- `reference/AImate` 是旧集成工程的参考代码，不是最终 App；A 项目最终用 DHT11/PIR。
- `reference/AImate` 里仍然有 AHT20 代码，仅作 B 项目参考；不要把它当 Linux 驱动。

## 开发顺序

1. CAN baseline：错误帧统计、bus-off 恢复、`FW_VERSION`；
2. ✅ Linux → STM32 命令/ACK（`0x221/0x222/0x228`）：1000/1000，P99 22ms；
3. ✅ ISO-TP：SF/FF/CF/FC、BS/STmin、4095 字节、真实 CAN + DHT11 并发；
4. ✅ UDS `0x10/0x3E/0x22`：真实 CAN 通过；
5. 🟡 UDS `0x19/0x14` + DTC：C001/C002/清除已通过；Flash 持久化待掉电验证；
6. OTA Client（写非活动分区）。

## 约定

- 中断只做收/发/置标志，业务放任务；
- 初始化后不再动态分配内存；
- 所有失败返回错误码，不吞错误；
- 版本号在 `version.h` 中统一维护；
- 新增文件先更新 README 和协议文档。
