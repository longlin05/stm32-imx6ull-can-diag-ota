# 硬件 Bring-up：接线、上电、第一次通信

> 目标：在不烧板子的前提下，把 STM32、i.MX6ULL、CAN 总线、传感器逐个点亮。
> 下面引脚是**示例**，必须按你的板子原理图/排针丝印确认。

---

## 1. 上电前检查

- [ ] 3.3V/5V 电源不短路（万用表测 VCC-GND）
- [ ] 所有模块共地
- [ ] CAN 收发器电压与逻辑电平匹配（优先 3.3V 型）
- [ ] CANH/CANL 没有接反
- [ ] 总线两端各一个 120Ω（模块自带就不要再加）
- [ ] 传感器输出不超过 3.3V
- [ ] 先不接电机/大功率负载

---

## 2. STM32 侧接线

| STM32 引脚（示例） | 接到 | 说明 |
|---|---|---|
| PA12 / CAN1_TX | 收发器 TXD | 发送 |
| PA11 / CAN1_RX | 收发器 RXD | 接收 |
| 3.3V | 收发器 VCC | 3.3V 型收发器 |
| GND | 收发器 GND | 共地 |
| PC1（示例） | DHT11 DATA | 单总线，4.7k–10k 上拉到 3.3V |
| PE0 | HC-SR501 OUT | 输入/轮询，示例 |
| PD12 | LED + 电阻 | 状态指示 |
| PA9 / PA10 | USB-TTL | USART1，115200 |

```text
STM32F407                SN65HVD230              CAN 总线
PA12 (CAN1_TX)  -------> TXD
PA11 (CAN1_RX)  <------- RXD
3.3V            -------> VCC
GND             -------> GND
                         CANH  ---- 双绞线 ---- CANH
                         CANL  ---- 双绞线 ---- CANL
```

注意：

- SN65HVD230 的 `Rs` 接 GND（高速模式）；TJA1050 的 `S` 接 GND。
- TJA1050 是 5V 供电，逻辑侧要确认；优先用 3.3V 型。
- HC-SR501 一般 5V 供电，输出 3.3V；共地。

---

## 3. i.MX6ULL 侧

### 3.1 板载 CAN

```text
i.MX6ULL CANH ---- 总线 CANH
i.MX6ULL CANL ---- 总线 CANL
GND           ---- 共地
```

```bash
ip link
ip link set can0 up type can bitrate 500000 sample-point 0.875
ip -details -statistics link show can0
candump -ta can0
```

### 3.2 MCP2515（没有板载 CAN 时）

| i.MX6ULL SPI | MCP2515 |
|---|---|
| SCLK | SCK |
| MOSI | SI |
| MISO | SO |
| GPIO CS | CS |
| GPIO INT | INT |
| 3.3V | VCC（确认模块逻辑电平） |
| GND | GND |

设备树示例：

```dts
&ecspi1 {
    status = "okay";

    mcp2515: can@0 {
        compatible = "microchip,mcp2515";
        reg = <0>;
        spi-max-frequency = <10000000>;
        interrupt-parent = <&gpio1>;
        interrupts = <18 IRQ_TYPE_LEVEL_LOW>;
        clocks = <&mcp2515_clk>;
        oscillator-frequency = <8000000>;
    };
};
```

```bash
dmesg | grep -i mcp
ip link
```

看不到 `can0` 时查：SPI 波形、CS、INT、晶振、compatible。

---

## 4. 传感器

### DHT11

```text
VCC  -> 3.3V
GND  -> GND
DATA -> GPIO（示例 PE1），4.7k–10k 上拉到 3.3V
```

DHT11 是单总线，不是 I2C：
- 主机拉低至少 18 ms，再释放；
- 读取 40 bit（湿度 16 + 温度 16 + 校验 8）；
- 校验和 = 前 4 字节相加取低 8 位；
- 采样间隔 ≥ 2 s，不要 500 ms 连续读。

先写一个裸机测试：读 10 次，打印原始 5 字节和校验结果。

### HC-SR501

```text
VCC  -> 5V
GND  -> GND
OUT  -> GPIO（3.3V 高电平）
```

上电预热 30–60 s，再调灵敏度和延时；先用人手移动确认 OUT 翻转。

---

## 5. Bring-up 顺序

1. 只给 STM32 上电，串口打印版本/心跳。
2. CAN 回环模式：自己发、自己收。
3. 接收发器，但不接总线，测 TXD/RXD 波形。
4. 接 i.MX6ULL，先 125 kbps，再 500 kbps。
5. `candump` 能看到 STM32 的 0x301。
6. 接 DHT11，读 10 次，校验和正确、数据合理。
7. 接 PIR，确认 PE0 电平变化和消抖输出。
8. 记录接线照片、日志、commit，打 `a-baseline` tag。

---

## 6. 常见问题

| 现象 | 先查 |
|---|---|
| `candump` 没数据 | `can0` 是否 up、120Ω、共地、CANH/CANL、波特率 |
| 错误帧多 | 采样点、终端电阻、线太长、收发器 5V 逻辑 |
| MCP2515 不出现 | SPI、CS、INT、晶振、设备树 |
| DHT11 读不到/校验错 | 上拉、时序、采样间隔 ≥2s、电源、DATA 是否接对 |
| PIR 一直触发 | 预热、灵敏度、电源纹波、地线 |
| STM32 复位/跑飞 | 供电、看门狗、中断优先级、栈溢出 |
