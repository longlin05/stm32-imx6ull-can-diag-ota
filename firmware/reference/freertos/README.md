# FreeRTOS 参考

来源：`模块代码/freertos/freertos/`。

只保留：

- `FreeRTOSConfig.h`
- `main_freertos_demo.c`（任务创建/挂起/恢复/删除示例）

## 用途

A 项目的任务划分参考，不是最终工程：

- `CAN_RX`：最高优先级，收帧入队；
- `Protocol`：解析命令/组 ACK；
- `Sensor`：100 ms 采集 DHT11/PIR/光照；
- `CAN_TX`：500 ms 周期 + 事件；
- `Heartbeat`：1 s；
- `Watchdog`：1 s 检查任务健康。

## 注意

- `main_freertos_demo.c` 里有注释掉的旧任务代码，导入时不要直接编译；
- FreeRTOS 内核源码（`Middlewares/Third_Party/FreeRTOS`）没有提交，用 CubeMX 生成；
- `FreeRTOSConfig.h` 里的堆大小/优先级要按最终任务数调整。
