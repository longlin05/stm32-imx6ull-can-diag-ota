# firmware

STM32F407 应用固件：传感器采集、CAN 应用帧、ISO-TP、UDS Server、DTC、OTA Client。

## 目录建议

```text
firmware/
├── Core/                 # CubeMX 生成代码（尽量不手改）
├── App/
│   ├── Inc/
│   │   ├── bsp_can.h
│   │   ├── aht20.h
│   │   ├── light.h
│   │   ├── pir.h
│   │   ├── protocol.h
│   │   ├── isotp.h
│   │   ├── uds.h
│   │   ├── dtc.h
│   │   └── ota.h
│   └── Src/
│       ├── bsp_can.c
│       ├── aht20.c
│       ├── light.c
│       ├── pir.c
│       ├── protocol.c
│       ├── isotp.c
│       ├── uds.c
│       ├── dtc.c
│       └── ota.c
├── .ioc
└── README.md
```

## 开发顺序

1. baseline：CAN 收发、传感器读取；
2. 错误帧统计、bus-off 恢复；
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
