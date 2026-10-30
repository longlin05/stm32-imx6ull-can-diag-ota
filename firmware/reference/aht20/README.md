# AHT20 参考（给 B 项目）

来源：`模块代码/aht20/aht20/Core/CustomDrivers/`，LibDriver AHT20（MIT）。

## 文件

- `driver_aht20.c` / `driver_aht20.h`

## 用途

B 项目要在 i.MX6ULL Linux 上写 AHT20 IIO 驱动。这份 STM32 代码**不能直接复用**，但可以对照：

- 初始化命令；
- 触发测量；
- 80 ms 等待；
- 7 字节读取；
- 状态位/CRC 检查；
- 错误码设计。

## 注意

- 这是 STM32 的 LibDriver 集成，不是 Linux 驱动；
- B 项目要重新写 `i2c_driver` + IIO，不要把这套函数指针接口搬进内核；
- 保留 MIT 版权头，不要删。
