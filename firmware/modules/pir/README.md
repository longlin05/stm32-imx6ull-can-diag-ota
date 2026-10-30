# HC-SR501 PIR 模块

来源：`模块代码/HC-SR501/HC-SR501/`，从 `Core/Src/gpio.c` 中抽出 `PIR_Sensor_*`。

## 文件

- `pir.c` / `pir.h`
- `HC-SR501.ioc`（CubeMX 配置参考）

## 接线

```text
VCC  -> 5V
GND  -> GND
OUT  -> PE0（默认，示例）
```

> 原工程用 PC1；但 A 项目 DHT11 也用 PC1，所以这里默认改成 PE0。  
> 如果 CubeMX 生成了 `PIR_Pin` / `PIR_GPIO_Port`，会优先使用你的标签。

## 行为

- 上电预热 60 s，期间返回 0；
- 100 ms 软件消抖；
- 检测到一次移动后，强制保持 2 s；
- 轮询式，不依赖 EXTI；放在 100 ms 任务里调用即可。

## 集成

1. CubeMX 把 PIR 引脚配成输入（上拉/下拉按模块输出电平定）。
2. 在任务里每 100 ms 调 `PIR_Sensor_GetState()`。
3. 事件计数/上报由应用层做；不要在这个模块里直接发 CAN。

## 已知问题

- 原工程是 5V 供电、3.3V 输出；共地。
- 预热期间不要上报“无人”，应上报“传感器未就绪”。
