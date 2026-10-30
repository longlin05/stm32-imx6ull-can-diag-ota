# Baseline：CAN 工程

来源：`模块代码/can通讯/`（本地 clone，HEAD `8a5d561`，与 `origin/main` 一致）。

## 内容

```text
baseline/
├── can_test/                 # CAN 发送/接收基础示例
└── can_loopback_silence/     # CAN 静默回环示例
```

每个工程只保留：

- `Core/Inc/`、`Core/Src/`、`Core/Startup/`
- `.ioc`
- `STM32F407VGTX_FLASH.ld` / `RAM.ld`

**没有提交 `Drivers/`、`Debug/`、`.metadata/`。**

## 为什么没有 HAL/CMSIS

HAL/CMSIS 由 STM32CubeMX 根据 `.ioc` 生成。仓库只保留应用层和工程配置，避免几万行第三方代码 + 构建产物。

复现：

1. 用 STM32CubeIDE 打开 `.ioc`；
2. 生成代码（会生成 `Drivers/`）；
3. 把本目录的 `Core/Inc`、`Core/Src` 覆盖回生成工程；
4. 编译下载。

## 注意

`can_test` 的主循环只发一次、收一次，然后空循环。它是 bring-up 示例，不是最终网关；A 项目会重写为周期任务 + 状态机。
