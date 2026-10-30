# firmware

STM32F407 应用固件：传感器采集、CAN 应用帧、ISO-TP、UDS Server、DTC、OTA Client。

## 目录

```text
firmware/
├── baseline/                 # 旧 CAN 工程（只保留 Core + .ioc + .ld）
│   ├── can_test/
│   └── can_loopback_silence/
├── modules/                  # 可复用传感器驱动
│   ├── dht11/                # DHT11 单总线温湿度
│   └── pir/                  # HC-SR501 轮询/消抖
├── reference/                # 只作参考，不直接编进最终工程
│   ├── freertos/             # FreeRTOSConfig + 任务示例
│   └── aht20/                # LibDriver AHT20（给 B 项目参考）
├── README.md
└── （后续新增）
    App/                      # 业务层：protocol/isotp/uds/dtc/ota
```

## 重要说明

- **没有提交 `Drivers/`、`Debug/`、`.metadata/`**。HAL/CMSIS 用 CubeMX 按 `.ioc` 生成。
- `baseline/can_test` 只发一次、收一次，是 bring-up 示例，不是最终网关。
- `modules/dht11` 默认 DATA = PC1；`modules/pir` 默认 OUT = PE0。
  **不要两个都用 PC1**，A 项目里必须分开。
- `reference/aht20` 是 STM32 的 LibDriver 集成，不是 Linux 驱动；B 项目要重写 IIO 驱动。

## 开发顺序

1. baseline：CAN 收发、DHT11/PIR 读取；
2. 错误帧统计、bus-off 恢复、`FW_VERSION`；
3. ISO-TP；
4. UDS `0x10/0x3E/0x22`；
5. UDS `0x19/0x14` + DTC；
6. OTA Client（写非活动分区）。

## 约定

- 中断只做收/发/置标志，业务放任务；
- 初始化后不再动态分配内存；
- 所有失败返回错误码，不吞错误；
- 版本号在 `version.h` 中统一维护；
- 新增文件先更新 README 和协议文档。
