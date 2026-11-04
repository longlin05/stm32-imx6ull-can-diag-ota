# My_AI 工程注意事项 / 踩坑记录

> 写给接手这个工程的人（也包括几个月后的你自己）。这里只放**代码里看不出来的东西**：
> 会重复踩的坑、排查套路、"看着像 bug 其实不是"的现象。
>
> 配套文档：[CAN_Protocol.md](CAN_Protocol.md)（CAN 帧格式契约）、
> [CAN_Framing_Plan.md](CAN_Framing_Plan.md)（上报层实现与联调）。
>
> **每踩一个新坑就往这里加一条**——不写下来，下次还会踩。

工程概况：STM32F407VGTx @168MHz，FreeRTOS V11.3.1（手动集成，**不在 .ioc 里**），
CubeIDE 6.15.0，FW_F4 V1.28.3。外设：CAN1、I2C1(OLED)、USART2(日志)、
PC0(人体感应)、PC1(DHT11)。

## 一、CubeMX 重新生成代码之后必须检查的 5 件事

这是本工程最大的坑源。重新生成完**先按这张表走一遍**，再编译：

| # | 检查什么 | 症状 / 怎么恢复 |
|---|---|---|
| 1 | `Middlewares/Third_Party/FreeRTOS/` 是否还在 | 不在就要恢复（下面有恢复源）。丢了会链接报 `undefined reference to xTaskCreate` 等一堆 FreeRTOS 符号 |
| 2 | `Core/Src/stm32f4xx_it.c` 里有没有多出 `SVC_Handler` / `PendSV_Handler` / `SysTick_Handler` 三个空函数 | 有就**删掉**。`FreeRTOSConfig.h` 用宏把 `vPortSVCHandler` 等改名成了这三个名字，两处定义会重复冲突。删的位置旁边留了注释块说明原因 |
| 3 | `.cproject` 的 `<sourceEntries>` 里 `Middlewares` 还在不在 | 不见了就会"FreeRTOS 函数全部找不到定义、但编译期毫无抱怨"（见第七节）。补回 `<entry ... name="Middlewares"/>`，Debug/Release 两份都要补 |
| 4 | 引脚标签有没有丢 | `OUT`(PC0) 和 `DATA`(PC1) 是驱动的编译期依赖：`DATA_Pin` 没了 dht11.c 会 `#error` 拒绝编译。标签在 `.ioc` 里，别删 |
| 5 | 手改进 `.ioc` 的项还在不在 | `I2C1.ClockSpeed=400000`、各 NVIC 优先级=5、`CAN1.*`。少了就加回去（`.ioc` 是纯文本，可以直接编辑，但改完建议在 CubeMX 里打开一次确认它认） |

**FreeRTOS 中间件的恢复源**：`C:\Users\祖龙\Desktop\FreeRTOS-LTS\FreeRTOS\FreeRTOS-Kernel`
（V11.3.1，仓库根目录就是内核根目录：`.c` 在根下、`include/` 与 `portable/` 在其下）。

拷进 `Middlewares/Third_Party/FreeRTOS/Source/` 后，**核对这 8 个 .c 都在**
（少一个就是链接报一堆 undefined）：

```
list.c  queue.c  tasks.c  timers.c  event_groups.c  stream_buffer.c
portable/GCC/ARM_CM4F/port.c        portable/MemMang/heap_4.c      ← 只要 heap_4
include/ 里的全部头文件             portable/GCC/ARM_CM4F/portmacro.h
```

`Middlewares` 整个目录都会被编（`.cproject` 里登记成了源目录），所以
**别把 heap_1/2/3/5.c 一起拷进来**——它们和 heap_4.c 定义同名符号，会重复定义。

## 二、FreeRTOS 使用约定

### 任务与优先级（`configMAX_PRIORITIES = 7`）

| 任务 | 优先级 | 周期 | 干什么 |
|---|---|---|---|
| `StartDefaultTask` | `tskIDLE_PRIORITY + 2` | 10 ms | PIR 采样 + 通知上报任务 |
| `DisplayTask` | `+ 1` | 500 ms / 2 s | 读 DHT11、刷 OLED、发布快照 |
| `Report_Task` | `+ 1` | 100 ms 唤醒 + 事件 | 组装并发送 CAN 帧 + 处理 Linux 命令（**唯一调 `CAN_Send()` 的地方**） |
| 定时器任务 | 6（内核设定） | —— | FreeRTOS 自带 |
| 空闲任务 | 0 | —— | 同上 |

**加新任务时的规矩**：采样任务(+2) 必须最高。刷一帧 OLED 会在 I2C 上自旋阻塞约 25ms
（`HAL_I2C_Mem_Write` 是轮询实现，不是 DMA，也不让出 CPU），采样周期只有 10ms，
同级的任务只能靠时间片在 tick 边界轮转——把采样任务挤到 +1 或更低，采样节奏就会被拖成 25ms 一格。

### 三个必须记住的模式

1. **调度器起来之前不能用 `vTaskDelay()` / `xTaskGetTickCount()` / 任何 FreeRTOS API。**
   本工程统一的写法是（可照抄）：
   ```c
   if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
   {  vTaskDelay(pdMS_TO_TICKS(20U)); }
   else
   {  HAL_Delay(20U); }
   ```
   （`INCLUDE_xTaskGetSchedulerState = 1` 已开）
2. **任务里禁止 `HAL_Delay()`。** 它是盯着 TIM6 计数自旋，等的时候攥着 CPU 不放，
   比它优先级低的任务全被饿死。它只在调度器启动前的初始化代码里可用。
   应用 `vTaskDelay()` / `vTaskDelayUntil()` / 信号量。
3. **周期任务用 `vTaskDelayUntil()`，不要用 `vTaskDelay()`。** 后者每轮多花的调度开销
   会累积，周期慢慢漂大（PIR 的消抖时间参数就不再有准头）。

### 时间基准有两套，别混

| 时基 | 归谁 | 谁在用 |
|---|---|---|
| **TIM6** → `HAL_GetTick()` | HAL | 驱动层所有超时、PIR 的时间戳、CAN 帧的 `timestamp_ms` / `uptime_s` |
| **SysTick** | 调度器 | `xTaskGetTickCount()`、所有延时、上报层的周期判定 |

`configTICK_RATE_HZ = 1000`，所以 1 tick = 1ms。HAL 的时基中断优先级是 15（最低），
SysTick 也归调度器管——**别去动 `NVIC.TimeBase=TIM6_DAC_IRQn`**。

### 中断里用 FreeRTOS API 的优先级红线

`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`，NVIC 是 4 位（数越小优先级越高）。
**任何要调用 `xxxFromISR()` 的中断，优先级数值必须 ≥ 5**（也就是不能比 5 更紧急），
否则会踩到内核临界区、偶发死机。`.ioc` 里 USART2、DMA1_Stream5/6 都已经钉在 5，
新加中断别忘了设。

### 内存

`configTOTAL_HEAP_SIZE = 20480`（heap_4），静态分配没开（`configSUPPORT_STATIC_ALLOCATION = 0`），
所以任务/队列只能动态创建。现在 3 个任务各 1KB 栈 + 定时器任务 1KB + 空闲任务 0.5KB，
大约还有 15KB 余量——加任务前用 `xPortGetFreeHeapSize()` 在调试器里看一眼。
栈溢出检查开了 `configCHECK_FOR_STACK_OVERFLOW = 2`，溢出时钩子会**关中断死循环**
（现象：整个系统突然静止），哪个任务溢出的用 `uxTaskGetStackHighWaterMark()` 查。

`configUSE_NEWLIB_REENTRANT = 0`：printf/malloc 的全局状态是多任务共享的。
本工程靠"一次 printf 整段原子入队"规避了日志交错，但 `errno`、`malloc` 这类
在多任务下**不是严格线程安全**的——关键逻辑别依赖它们。

## 三、各模块的调用约束速查

| 模块 | 只能在哪里调 | 阻塞多久 | 不能做什么 |
|---|---|---|---|
| `PIR_Task()` | **某一个**任务里，10ms 周期 | 不阻塞（一次读引脚） | 别放两个任务里调（滤波计数会被算错） |
| `PIR_GetXxx()` | 任何任务，甚至中断 | 单字读，立即返回 | 组合读不是同一时刻的快照（状态和计数可能差一个采样周期） |
| `DHT11_Read()` | 任务里 | **约 25ms**（含约 4ms 关中断） | 不可重入；两次至少隔 1.5s；别在临界区/中断里调 |
| `OLED_UI_ShowEnv()` | 一个任务里（现在是 `DisplayTask`） | **约 25ms**（1024 字节 @400kHz） | 屏是全局状态，不线程安全；I2C 总线也只归它 |
| `CAN_Send()` | **只有 `Report_Task`** | 最多 `APP_CAN_TX_TIMEOUT_MS`(20ms) | 别在别的任务/中断里发（单写者约定，见下） |
| `AppCmd_Task()` | **只有 `Report_Task`**，每轮循环一次 | 不阻塞（最多排空 8 帧） | 别挪到别的任务里——它要发 ACK，会破坏 `CAN_Send()` 单写者约定 |
| `Report_PublishEnv()` | 任务里（现在是 `DisplayTask`） | 微秒级临界区 | 环境数据的唯一生产者；别在别处发布 |
| `printf()` | 任何地方（含调度器启动前） | 不阻塞，满了整行丢弃 | 打不了 `%f`（见第五节） |
| `ssd1306_DrawArc*` | —— | —— | **别调**，会拉进 libm 的三角函数；当前没人用 |

**单写者/单读者约定**（本项目不用锁，靠这个）：

- CAN 总线：只有 `Report_Task` 发帧（上报帧 + 命令 ACK 都在它的循环里，命令处理由
  同一任务调 `AppCmd_Task()`）→ 所以不需要互斥量。
- DHT11 单总线：只有 `DisplayTask` 读，结果通过 `Report_PublishEnv()` 发布快照给上报层
  （环境数据的有效性也只在这一处判定，屏和 CAN 显示/发送的内容永远一致）。
- PIR：`StartDefaultTask` 采样，其他任务读接口。

## 四、串口（printf）调试

- 三个调试开关默认**全是 0**，也就是**默认一个字都不打印**：
  `PIR_DEBUG_PRINT`（driver_pir.c）、`DHT11_DEBUG_PRINT`（dht11.c）、
  `CAN_DEBUG_PRINT`（can.c）。排查时先确认打开的是哪个。
- 通道：USART2 **115200 8N1**（PA2=TX / PA3=RX），
  `printf` 被 usart.c 覆盖：先在栈上格式化（上限 128 字节），再**整段**拷进 512 字节环形缓冲，
  DMA1_Stream6 接力发出。所以 printf **不会阻塞任务**。
- **缓冲区满时整行丢弃**（不截断、不等待），累计丢字节数用 `UART2_TxDropCount()` 查。
  日志缺行先看这个数，别怀疑代码。
- `UART2_TxFlush()` 是阻塞排空（会等），只在复位/停机前用。
- **打不了浮点**：链接参数是 `--specs=nano.specs` 且没有 `-u _printf_float`，
  所以 `printf("%f")` 不会输出。要打温度就照现有写法拆成整数：
  `printf("T:%u.%uC", th.temp_int, th.temp_dec);`（nano 下要支持 %f 得在链接器里加 `-u _printf_float`）
- **flash 尺寸突然涨了几 KB 不一定是代码出问题**：printf 第一次被真正调用时，
  newlib 的 `vsnprintf` 那一整串格式化代码才会被链进来（之前全被 `--gc-sections` 回收了）。
  本工程就因此从约 41KB 跳到约 47KB。

## 五、中断与临界区

- `taskENTER_CRITICAL()` 抬 BASEPRI，只屏蔽优先级数值 ≥ 5 的中断（那些能用 FreeRTOS API 的）；
  `__disable_irq()` 抬 PRIMASK，屏蔽一切。**想保护跨任务共享数据用前者**。
- 代价要知道：`taskENTER_CRITICAL()` 期间 **SysTick 被屏蔽，tick 停摆**——
  别的任务整体推迟几毫秒，`xTaskGetTickCount()` 也停一下。
  所以临界区要短。目前最长的是 DHT11 读 40bit 的那段（约 4ms）：
  **传感器没接时这段不会变长**（等 ACK 的超时是在临界区之外做的），这个设计是刻意的。
- `HAL_GetTick()`（TIM6）不受 `taskENTER_CRITICAL()` 影响，但会被 `__disable_irq()` 停住。

## 六、CAN 相关

- **单节点挂在总线上会进 bus-off。** CAN 的发送方必须收到 ACK 才算成功；
  总线上没有第二个节点应答时每帧判 ACK 错误、发送错误计数器 TEC 每帧 +8，
  约 16 帧后 error-passive，32 帧后 bus-off。本协议默认约 0.8 帧/s
  （温湿度 2s + PIR 保活 5s + 心跳 10s），**约 20s 开始出错、40s 左右 bus-off**；
  把上报周期改小会明显加快。
  联调时先把 Linux 侧 `can0` 拉起来（有它应答就正常），纯节点自测就配成 Loopback。
  `ABOM = ENABLE` 能让它自动恢复，但**不会**让单节点发送成功。
- 看到 `tx_drop` 上涨，先排除上面这条，再怀疑代码。
- `CAN_Start()`（配过滤器 + `HAL_CAN_Start()`）必须在 `MX_CAN1_Init()` 之后、
  任何收发之前调用；它在调度器启动前调，不需要调度器。
- CAN 发送失败**不要**改成 `Error_Handler()`：那会关中断死循环，TIM6 时基停摆，
  整个采集链路一起失效。失败只累加计数，由心跳帧 `0x113` 报给上位机。
- 位率 500kbit/s 由 `.ioc` 的 `CAN1.Prescaler/BS1/BS2` 决定（6 / 11TQ / 2TQ），
  改完要重新生成代码；两侧位率必须一致。
- **改帧格式/命令码要同步改**：`can_protocol.h`、`Docs/CAN_Protocol.md`（含里面的
  python 示例）、以及组帧/执行命令的 `app_report.c` / `app_cmd.c`；实现细节变了还要
  改 `Docs/CAN_Framing_Plan.md`。改了字段而没改文档，对接方不会报错，
  只会得到一堆莫名其妙的值。
- 上行帧：`0x111` 人体感应、`0x112` 温湿度、`0x113` 心跳；下行：`0x221` 配置写入、
  `0x222` 查询、`0x228` ACK。ID 低 4 位是源地址（`CAN_NODE_STM32 = 0x1`）。
  **AImate 和 My_AI 同挂一条总线时必须改一个源地址**，否则帧 ID 完全相同、互相冒充。
- 命令帧 CRC16（CCITT-FALSE）只覆盖 byte0..5（cmd/seq/args），CRC 在 byte6..7；
  改命令载荷布局时校验范围要跟着改，否则 Linux 端算的 CRC 永远对不上。
- `(cmd, seq)` 去重缓存只有 4 条：Linux 端在发新的 4 个不同命令之前重传，才一定命中。
  命中的重传**不执行命令**、直接回放上次的 ACK；统计里看 `dup_cmd`。

## 七、编译、链接、工程配置

- **新增目录必须登记为源目录**，否则文件根本不被编译。判断症状：
  **"整个函数找不到定义（undefined reference）+ 编译期毫无抱怨" = 新目录没登记**。
  登记位置：`.cproject` 的 `<sourceEntries>`，Debug/Release **两份都要改**。
  改完要在 CubeIDE 里 Close Project / Open Project（Eclipse 把 `.cproject` 缓存在内存里），
  再 Clean → Build。
- `Core` 是**递归**的：`Core/CustomDrivers/` 下面再建子目录（比如 `ssd1306/`）不用改配置。
- 相对包含（如 `#include "ssd1306/ssd1306.h"`）按**包含者自己所在目录**解析，
  与 `-I` 列表无关。
- 新增源文件后要在 IDE 里刷新（F5）再 Clean，否则 Eclipse 不知道新文件存在。
- **工具链不在 PATH 里**，完整路径：
  `D:/STM32CubeIDE/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-*`
- 想在命令行手工编译验证（排查链接问题时很有用）要注意三件事：
  1. MSYS/Git-Bash 会把 `@objects.list` 这种响应文件路径搞坏 →
     **目标文件要直接写在命令行上**；
  2. 必须显式带上 `Core/Startup/startup_stm32f407vgtx.s`（注意不是 `startup_stm32f407xx.s`），
     否则报 `cannot find entry symbol Reset_Handler`；
  3. 要加 `-ffunction-sections -fdata-sections`，否则 `--gc-sections` 回收不掉未用函数，
     尺寸会虚高好几倍（本工程实测：不加约 140KB，加了约 47KB）。
- 编译产物在 `Debug/`（`My_AI.elf` / `.map` / `.list`），链接参数可从 `Debug/makefile` 里看，
  它才是真正生效的那份（`.cproject` 里存的是选项、不一定直观）。
- 当前没定义 `USE_FULL_ASSERT`：HAL 的参数检查（`assert_param`）不起作用，
  传错参数不会报错，只会行为诡异。

## 八、硬件接线与电气

| 功能 | 引脚 | 备注 |
|---|---|---|
| HC-SR501 OUT | **PC0**（.ioc 标签 `OUT`） | 输入 + 下拉；低=无人，高=有人；模块输出脉宽 ≥2.3s；**上电 60s 预热**，期间一律上报"无人" |
| DHT11 DATA | **PC1**（.ioc 标签 `DATA`） | 输入（驱动运行时自己切输出/输入）；数据线要上拉（成品模块一般板载 4.7k） |
| OLED SCL/SDA | **PB6 / PB7**（I2C1） | 400kHz；地址 `0x3C`（有些模块是 `0x3D`，改 `ssd1306_conf.h`） |
| CAN | **PB8 = RX / PB9 = TX**（CAN1） | 500kbit/s；总线两端各 120Ω；**必须有第二个节点应答** |
| 串口日志 | **PA2 = TX / PA3 = RX**（USART2） | 115200 8N1，DMA1_Stream6 发送 / Stream5 接收 |
| 调试 | **PA13 / PA14** | SWD |

- **I2C 花屏/不亮先怀疑上拉**：400kHz 需要足够强的上拉（模块多半板载 4.7k）。
  要降速排查就把 `i2c.c` 里的 `ClockSpeed` 和 `.ioc` 里的 `I2C1.ClockSpeed` **两处一起**改回 100000。
- 所有模块**共地**，否则单总线和 I2C 都会时好时坏。
- HC-SR501 的跳线（H/L）决定单次触发/可重触发，会影响"保持窗口"的观感；
  驱动里的 2s 保持是可重触发的，模块设成单次触发时行为会更"干净"。

## 九、参数改哪里

| 想改什么 | 改哪个文件 |
|---|---|
| PIR 采样周期 / 滤波次数 / 保持时间 / 预热时间 | `Core/CustomDrivers/driver_pir.h`（改采样周期会改变确认延迟 = 次数 × 实际周期） |
| OLED 刷新周期、DHT11 读取间隔、过期判定 | `Core/CustomDrivers/oled_ui.h` |
| CAN 上报周期、发送超时 | `Core/CustomDrivers/app_report.h` |
| CAN 帧 ID / 字段布局 / 协议版本 | `Core/CustomDrivers/can_protocol.h` **+** `Docs/CAN_Protocol.md` |
| CAN 命令码 / 参数范围 / ACK 状态 | `Core/CustomDrivers/can_protocol.h` **+** `app_cmd.c` **+** `Docs/CAN_Protocol.md` |
| CAN 位率 | `My_AI.ioc` 的 `CAN1.Prescaler/BS1/BS2` |
| I2C 速率 | `Core/Src/i2c.c` **+** `My_AI.ioc` |
| 任务栈 / 任务优先级 | `Core/Src/main.c` 的 USER CODE 2 |
| 堆大小、tick 频率、各内核开关 | `Core/Inc/FreeRTOSConfig.h` |
| 引脚功能与标签 | `My_AI.ioc`（改完重新生成，然后走第一节的检查清单） |

## 十、排查速查：症状 → 先看哪

| 症状 | 先看 |
|---|---|
| 一堆 `undefined reference to xTaskCreate/vTaskDelay/...`，编译期没报错 | 第七节第 1 条：`Middlewares` 是否还在 `<sourceEntries>` 里 |
| 链接报 `Reset_Handler` 找不到 | 命令行手工链接？漏了 `startup_stm32f407vgtx.s` |
| `SVC_Handler` / `PendSV_Handler` 重复定义 | 重新生成代码后 `stm32f4xx_it.c` 里多出了空函数，删掉 |
| 串口什么都不打印 | 三个 `_DEBUG_PRINT` 开关都是 0（默认）；或 CAN 启动失败（会打印一句提示） |
| 日志缺行 | `UART2_TxDropCount()` 涨了 → 缓冲满，日志被整行丢弃 |
| 串口打 `%f` 没有输出 | nano.specs 不带浮点格式化，拆成整数打 |
| `candump` 一帧都没有 | `CAN_Start()` 有没有被调 → 总线是否 bus-off → 两侧位率/120Ω/接线 → 先用 Loopback 排除物理层 |
| `tx_drop` 一直涨 | 总线上没有应答方（第六节第一条） |
| 发 `0x222` 没有 ACK | 命令帧 DLC 不是 8 / CRC 不对（`crc_err` 涨）/ 目标地址不是 `0x1`；先看 `0x10` 查询版本是否通 |
| 查询统计里 `dup_cmd` 一直涨 | Linux 端重传了同一 `(cmd, seq)`；正常去重行为，检查上位机是否超时设得太短 |
| PIR 事件帧只有 PERIOD 没有 ENTER/LEAVE | 上电 60s 预热期内不报事件；或 `PIR_SetEnabled(0)` 后被关掉了 |
| 屏黑 / 花屏 | I2C 上拉、地址 0x3C vs 0x3D、降回 100kHz（两处一起改） |
| 屏上温湿度恒为 `--` | 看 CAN 的 `0x112` status：`0x04`=上电至今没读到（接线/上拉），`0x01`=读到过但已过期（>5s），`0x00`=正常 |
| 系统突然整个静止 | 栈溢出钩子关中断了，查 `uxTaskGetStackHighWaterMark()` |
| 进了 `Error_Handler()` 出不来 | 它是关中断死循环，正常现象；查谁调的（CAN 失败**不应该**走这里） |
| 温湿度小数位恒为 0 | **不是 bug**：DHT11 的小数位恒为 0，DHT22 才有 |
| PIR 人走了还显示"有人" | **不是 bug**：2s 保持窗口，属设计行为 |
| 上电 60s 内 PIR 一直"无人" | **不是 bug**：HC-SR501 预热期，`PIR_WARMING` 位为 1 |
| 调试时发现 tick 不走 | **不是 bug**：DHT11 的 4ms 临界区里 SysTick 被屏蔽 |
| flash 尺寸跳了几 KB | **多半不是 bug**：printf 第一次被调用拉进了 vsnprintf（第四节） |

## 十一、方向性约定（改代码前先认同这些）

1. **驱动和业务分开**：`driver_pir.c` / `dht11.c` 不知道 CAN、不知道 OLED，
   只提供采样和读取接口；组装帧、刷屏在它们的上层（`app_report.c` / `oled_ui.c`）。
   加新功能时保持这个分层，驱动才能单独复用和测试。
2. **文档和代码同步**：对外接口（CAN 协议）必须有两份且一致——
   文档给人看，头文件给编译器看。**改一份就改另一份**。
3. **不要在驱动里调 `Error_Handler()`**：它是"关中断死循环"，会让整个采集链路一起失效。
   运行时错误就地记录、上报、继续跑（CAN 丢帧就是这样处理的）。
4. **排查顺序永远是：物理层 → 配置 → 代码**。第五节的 bus-off、第七节的
   sourceEntries、I2C 速率这些"配置类"问题占了实际故障的大半。
