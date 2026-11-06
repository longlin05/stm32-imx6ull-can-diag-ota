# STM32 + i.MX6ULL CAN 诊断与 OTA（06）：Bootloader——A/B 分区、跳转与回滚，以及 SysTick 没关导致的“App 卡死”

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[05 UDS DTC：从故障检测到 Flash 掉电保存](https://blog.csdn.net/longlin101/article/details/170192523)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.7-boot`

UDS 和 DTC 做完，接下来是 Bootloader。

它解决的问题不是“怎么发固件”，而是：

> **上电后启动哪个 App；App 坏了怎么退回去；升级中断怎么办。**

这一篇做：

```text
A/B 分区
App Header + CRC32
Metadata
trial / rollback / confirm
跳转
```

以及两个真实踩坑：

1. App 没设 VTOR；
2. Bootloader 没关 SysTick，App 卡在 BSS 清零。

---

## TL;DR

```text
0x08000000  32KB   Bootloader
0x08008000  16KB   Metadata A
0x0800C000  16KB   Metadata B
0x08010000  64KB   DTC Config
0x08020000  256KB  App A
0x08060000  256KB  App B
0x080A0000  384KB  free
```

App 槽前 0x200 是 Header，真正向量表在 +0x200。

流程：

```text
上电
  → 读 Metadata
  → 校验 active 槽
  → trial 槽存在则先试运行
  → App 自检后 Boot_AppConfirm()
  → 未确认则下次复位回滚
```

---

## 1. 为什么需要 Bootloader

没有 Bootloader：

```text
上电直接跑 0x08000000 的 App
App 刷坏 = 板子变砖
```

有了 Bootloader：

```text
上电先跑 Bootloader
Bootloader 校验 App
App 坏 → 换另一个槽
```

OTA 只是“把新固件写到非活动槽”的手段。

Bootloader 本身不负责网络，也不负责业务。

---

## 2. 分区

STM32F407VGTX 1MB Flash，sector 大小不均匀，必须按 sector 对齐。

```text
0x08000000  32KB   Bootloader      sector 0-1
0x08008000  16KB   Metadata A      sector 2
0x0800C000  16KB   Metadata B      sector 3
0x08010000  64KB   DTC Config      sector 4
0x08020000  256KB  App A           sector 5-6
0x08060000  256KB  App B           sector 7-8
0x080A0000  384KB  free            sector 9-11
```

这里有一个之前埋下的坑：

DTC 原来存在 `0x08060000`，但 Bootloader 分区后那是 App B。

所以 DTC 必须迁到：

```text
0x08010000  sector 4
```

---

## 3. App Header

每个 App 槽：

```text
slot base + 0x000   Header (512B)
slot base + 0x200   向量表 / App
```

Header：

```c
typedef struct {
    uint32_t magic;       /* "APP1" */
    uint32_t version;
    uint32_t length;
    uint32_t crc32;
    uint32_t build_id;
    uint32_t reserved[3];
} boot_app_header_t;
```

为什么要 0x200：

- Cortex-M 的 VTOR 要求向量表 0x200 对齐；
- 512B Header 正好把 App 向量表推到下一个 0x200 边界。

App A：

```text
Header  0x08020000
App     0x08020200
```

App B：

```text
Header  0x08060000
App     0x08060200
```

---

## 4. 链接脚本

Bootloader：

```text
FLASH ORIGIN = 0x08000000
LENGTH = 32K
```

App A：

```text
FLASH ORIGIN = 0x08020200
LENGTH = 0x3FE00
```

App B：

```text
FLASH ORIGIN = 0x08060200
LENGTH = 0x3FE00
```

App 还要定义：

```text
App A: VECT_TAB_OFFSET=0x20200
App B: VECT_TAB_OFFSET=0x60200
```

---

## 5. 第一个坑：App 没设 VTOR

一开始 App 能跳进去，但 UDS 一直 timeout。

用 ST-Link 热连接看 PC：

```text
PC = 0x08025C98
```

已经在 App A 里，但停在 `HAL_Delay`。

反汇编 `SystemInit`，发现没有写 `SCB->VTOR`。

原因是 `system_stm32f4xx.c` 里：

```c
#if defined(USER_VECT_TAB_ADDRESS)
  SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET;
#endif
```

只加了 `VECT_TAB_OFFSET`，没加：

```text
USER_VECT_TAB_ADDRESS
```

所以 VTOR 还是 `0x08000000`，中断全跳到 Bootloader 的向量表。

补上宏之后，`SystemInit` 才有：

```c
SCB->VTOR = 0x08020200;
```

---

## 6. 第二个坑：SysTick 没关

VTOR 修好后，UDS 还是 timeout。

GDB attach 看到：

```text
Reset_Handler +43
LoopFillZerobss
```

栈顶：

```text
SysTick_Handler -> xTaskIncrementTick
```

原因：

- Bootloader 的 `HAL_Init()` 打开了 SysTick；
- `boot_jump_to()` 跳转前没有关；
- App 启动到 main 之前要清零 BSS；
- 此时 App 的 FreeRTOS 还没初始化；
- SysTick 一进来就访问未初始化任务链表；
- BSS 清零都走不完，main 根本没到。

修复：

```c
__disable_irq();

SysTick->CTRL = 0;
SysTick->LOAD = 0;
SysTick->VAL  = 0;
SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

SCB->VTOR = app_base;
__DSB();
__ISB();
__set_MSP(sp);
__enable_irq();
entry();
```

注意顺序：

```text
先关 SysTick
清 pending
设 VTOR
设 MSP
开中断
跳转
```

只加 `__enable_irq()` 不够，SysTick pending 仍然会进来。

---

## 7. Metadata

Metadata 记录：

```text
active_slot
trial_slot
slot_state[2]
attempts
version/length/crc
seq
CRC
```

双记录：

```text
sector 2: Metadata A
sector 3: Metadata B
```

写入时：

```text
擦另一份
写新记录
seq++
```

掉电时至少还有一份旧记录。

---

## 8. trial / rollback

升级完成后：

```text
active=A
trial=B
B state=trial
attempts=0
```

第一次复位：

```text
Bootloader 启动 B
attempts++
保存 Metadata
```

App B 自检通过后：

```c
Boot_AppConfirm(1);
```

Metadata：

```text
active=B
trial=NONE
B=confirmed
```

如果没确认就再次复位：

```text
Bootloader 看到 trial B attempts=1
→ 回滚 A
```

这就是 A/B 的核心。

---

## 9. 实测

App A：

```text
PC = 0x08025C50
UDS 50 03
```

trial B：

```text
PC = 0x08065C50
```

不确认，再复位：

```text
PC = 0x08025C50   # 回滚 A
```

App B 调 `Boot_AppConfirm(1)` 后：

```text
Metadata seq=12 active=B trial=NONE
再次复位 PC = 0x08068424  # 仍 B
```

A/B 启动、trial、回滚、confirm 都通过。

---

## 10. 目录

```text
firmware/bootloader/
├── boot_layout.h
├── boot_crc32.c/h
├── boot_image.c/h
├── boot_meta.c/h
├── boot_meta_store.c/h
├── boot_flash.h
├── boot_flash_stm32.c/h
├── boot_jump.c/h
├── boot_loader.c/h
├── boot_app.c/h
├── ota_server.c/h
├── linker/
│   ├── bootloader.ld
│   ├── app_a.ld
│   └── app_b.ld
└── tools/mk_app_image.py
```

打包 App：

```bash
python tools/mk_app_image.py app.bin app_a.img --version 1
```

---

## 11. 下一步

Bootloader 完成后，OTA 的写入就变成：

```text
0x34 擦非活动槽
0x36 逐块写
0x37 校验 + 写 trial Metadata
复位
Bootloader trial 启动
App confirm
```

下一篇写 OTA。

---

## 12. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.7-boot`
- 测试日志：
  - `test/records/20261106-boot-core-host.log`
  - `test/records/20261106-boot-ab-trial.log`

标签：`STM32` `Bootloader` `A/B` `CRC32` `i.MX6ULL`
