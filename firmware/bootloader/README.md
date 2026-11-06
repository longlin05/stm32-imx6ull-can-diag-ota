# Bootloader

A 项目的 STM32F407 Bootloader。核心逻辑、HAL Flash 适配、Metadata 双记录、镜像校验、跳转代码已写；
还差 CubeIDE 工程和 App A/B 构建配置。

## 当前完成

- CRC-32/ISO-HDLC（一次性 + 流式）；
- App Header 校验（magic / length / CRC32）；
- 向量表校验（SP 在 SRAM、Reset_Handler 在槽内、Thumb 位）；
- Metadata 双记录选择（seq + CRC + 两 sector）；
- trial / confirm / rollback 策略；
- Flash CRC 流式校验（不把 256KB 读进 RAM）；
- HAL Flash 读/擦/写适配；
- MSP/VTOR 跳转；
- 主机单元测试。

```bash
cd firmware/bootloader
make test
# boot core tests: OK
# boot meta store tests: OK
```

## 文件

```text
boot_layout.h        分区表
boot_crc32.c/h       CRC32 + 流式
boot_image.c/h       Header / 镜像 / 向量表
boot_meta.c/h        Metadata 状态机
boot_meta_store.c/h  双记录读写
boot_flash.h         Flash 操作接口
boot_flash_stm32.c/h HAL 适配
boot_jump.c/h        跳转
boot_loader.c/h      上电选槽 + 跳转
boot_app.c/h         App 自检确认（Boot_AppConfirm）
linker/              Bootloader / App A / App B 链接脚本
tools/mk_app_image.py 给 App bin 加 512B header
```

## 分区表

```text
0x08000000  32KB   Bootloader      sector 0-1
0x08008000  16KB   Metadata A      sector 2
0x0800C000  16KB   Metadata B      sector 3
0x08010000  64KB   DTC Config      sector 4
0x08020000  256KB  App A           sector 5-6
0x08060000  256KB  App B           sector 7-8
0x080A0000  384KB  free            sector 9-11
```

每个 App 槽：

```text
slot base + 0x000 : App Header (512B)
slot base + 0x200 : 真正的向量表 / App
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

## A/B 状态

```text
active_slot
trial_slot
slot_state[2] = invalid / trial / confirmed
attempts
```

启动规则：

```text
trial 有效且 attempts==0  → 启动 trial
trial 启动过一次未确认    → 回滚 active
active 有效               → 启动 active
active 无效、另一槽有效   → 启动另一槽
两个都无效                → 停在 Bootloader
```

## 还没做

- CubeIDE Bootloader 工程；
- App 侧 `Boot_Confirm()` 和自检调用；
- App A/B 实际构建、烧录与硬件测试。

DTC 已从 `0x08060000` 迁到 `0x08010000`（sector 4）。

## 接入 CubeIDE

Bootloader 工程在 `main()` 里完成 HAL/时钟初始化后调用：

```c
#include "boot_loader.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    if (Bootloader_Run() != 0) {
        for (;;) {
            /* 没有可启动 App：LED 慢闪 / 串口打印 */
        }
    }
    for (;;) {
    }
}
```

链接：

```text
linker/bootloader.ld
```

App 自检通过后调用：

```c
Boot_AppConfirm(0);   /* App A */
Boot_AppConfirm(1);   /* App B */
```

## 链接脚本

```text
linker/bootloader.ld   0x08000000, 32KB
linker/app_a.ld        0x08020200, 0x3FE00
linker/app_b.ld        0x08060200, 0x3FE00
```

App A 编译宏：

```text
VECT_TAB_OFFSET=0x20200
```

App B：

```text
VECT_TAB_OFFSET=0x60200
```

## 生成带 Header 的 App

```bash
python tools/mk_app_image.py app.bin app_with_header.bin --version 1
```

输出：

```text
0x000  header (512B)
0x200  app.bin
```

烧录时从槽基地址开始：

```text
App A: 0x08020000
App B: 0x08060000
```

## 下一步

1. 建 CubeIDE Bootloader 工程；
2. 调 `Bootloader_Run()`；
3. 用 `app_a.ld`/`app_b.ld` 编 A/B；
4. 写 App 自检和 `Boot_Confirm()`；
5. 硬件测试正常/CRC 坏/trial 回滚。
