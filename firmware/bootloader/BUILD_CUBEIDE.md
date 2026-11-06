# Bootloader / App A / App B CubeIDE 建法

本文按 STM32CubeIDE 的菜单写。不同版本菜单文字可能略有差异，但路径基本一致。

目标：

```text
Bootloader   0x08000000  32KB
App A        0x08020000  256KB，镜像从 0x08020200 开始
App B        0x08060000  256KB，镜像从 0x08060200 开始
```

---

## 0. 先备份

当前 `My_AI` 工程仍链接在 `0x08000000`。

在改链接脚本前：

```text
1. 备份 D:\STM32CubeIDE\My_AI\My_AI
2. 确认 ST-Link / BOOT0 能恢复
3. 不要直接把 Bootloader 烧进去后断电不再管
```

---

## 1. 新建 Bootloader 工程

### 1.1 新建

```text
File
  → New
  → STM32 Project
```

选择：

```text
MCU: STM32F407VGTX
Project Name: Bootloader
Location: D:\STM32CubeIDE\Bootloader
```

### 1.2 CubeMX 配置

只保留 Bootloader 需要的外设，不要 FreeRTOS、不要 CAN、不要 I2C/OLED。

```text
System Core
  RCC
    High Speed Clock (HSE): Crystal/Ceramic Resonator
  SYS
    Debug: Serial Wire
    Timebase Source: SysTick
```

时钟按当前 My_AI 板子的配置：

```text
HCLK   168 MHz
APB1    42 MHz
APB2    84 MHz
```

如果只是验证 Bootloader，可以不加 UART；要打印错误可以加 USART2。

### 1.3 Project Manager

```text
Project
  Toolchain/IDE: STM32CubeIDE
  Generate Under Root: 勾选
Code Generator
  Generate peripheral initialization as a pair of .c/.h files: 建议勾选
```

点 `GENERATE CODE`。

---

## 2. 加入 Bootloader 源文件

把仓库里的这些文件复制到 Bootloader 工程：

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
├── boot_policy.c/h
```

推荐目录：

```text
Bootloader/Core/Src/
Bootloader/Core/Inc/
```

不要复制：

```text
boot_app.c/h      # App 用
test/             # 主机测试
tools/            # 主机工具
linker/           # 链接脚本单独用
```

### 2.1 加入 include 路径

```text
Project
  → Properties
  → C/C++ Build
  → Settings
  → Tool Settings
  → MCU/MPU GCC Compiler
  → Include paths
```

确认包含：

```text
Core/Inc
```

如果你把 Bootloader 文件放在单独目录，也要把那个目录加进去。

---

## 3. 替换链接脚本

把：

```text
firmware/bootloader/linker/bootloader.ld
```

复制到 Bootloader 工程，例如：

```text
Bootloader/STM32F407VGTX_BOOT.ld
```

然后：

```text
Project
  → Properties
  → C/C++ Build
  → Settings
  → Tool Settings
  → MCU/MPU GCC Linker
  → Linker Script
```

选择 `STM32F407VGTX_BOOT.ld`。

该脚本：

```text
FLASH ORIGIN = 0x08000000
FLASH LENGTH = 32K
```

Bootloader 的 `VECT_TAB_OFFSET` 保持 0，不需要额外定义。

---

## 4. 修改 Bootloader 的 main.c

CubeMX 生成的 `main()` 里，在 `SystemClock_Config()` 之后加入：

```c
#include "boot_loader.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    if (Bootloader_Run() != 0) {
        for (;;) {
            /* 没有可启动 App：可加 LED 慢闪 / 串口打印 */
        }
    }

    for (;;) {
    }
}
```

注意：

- `Bootloader_Run()` 成功时不会返回；
- 返回 `-1` 表示 A/B 都没有有效镜像；
- 不要在 Bootloader 里初始化 FreeRTOS、CAN、OLED。

编译，检查：

```text
Program Size: text+data+bss
FLASH 使用量 < 32KB
```

如果超过 32KB，先裁剪 HAL/日志，不要改大 Bootloader 分区。

---

## 5. 新建 App A 工程

复制整个：

```text
D:\STM32CubeIDE\My_AI\My_AI
```

为：

```text
D:\STM32CubeIDE\My_AI_A
```

### 5.1 换链接脚本

把：

```text
firmware/bootloader/linker/app_a.ld
```

复制到 App A 工程，替换或新增为：

```text
STM32F407VGTX_APP_A.ld
```

工程设置里选择这个链接脚本。

它把 App 链接到：

```text
0x08020200
```

### 5.2 加 VECT_TAB_OFFSET

在 App A 工程：

```text
Project
  → Properties
  → C/C++ Build
  → Settings
  → Tool Settings
  → MCU/MPU GCC Compiler
  → Preprocessor
  → Defined symbols
```

添加：

```text
VECT_TAB_OFFSET=0x20200
```

这个宏给 `system_stm32f4xx.c` 用，让 App 启动时：

```c
SCB->VTOR = FLASH_BASE | 0x20200;
```

不加这个，App 的中断会跳回 Bootloader 的向量表。

### 5.3 编译

```text
Project → Build Project
```

检查 map：

```text
.isr_vector 在 0x08020200
```

生成 bin：

```bash
arm-none-eabi-objcopy -O binary My_AI_A.elf My_AI_A.bin
```

---

## 6. 新建 App B 工程

再复制一份：

```text
D:\STM32CubeIDE\My_AI_B
```

换：

```text
app_b.ld
```

加：

```text
VECT_TAB_OFFSET=0x60200
```

编译，生成 `My_AI_B.bin`。

---

## 7. 给 App bin 加 Header

使用仓库工具：

```bash
python firmware/bootloader/tools/mk_app_image.py \
    My_AI_A.bin \
    app_a.img \
    --version 1
```

输出：

```text
0x000  header 512B
0x200  My_AI_A.bin
```

App B 同理：

```bash
python firmware/bootloader/tools/mk_app_image.py \
    My_AI_B.bin \
    app_b.img \
    --version 1
```

---

## 8. 烧录顺序

第一次不要把 App 烧回 `0x08000000`。

### 8.1 Bootloader

用 STM32CubeProgrammer 或 CubeIDE 烧：

```text
bootloader.elf / bootloader.bin → 0x08000000
```

### 8.2 App A

```bash
STM32_Programmer_CLI -c port=SWD \
  -w app_a.img 0x08020000 -v
```

或 CubeProgrammer GUI：

```text
Address: 0x08020000
File: app_a.img
```

### 8.3 上电

Bootloader 会：

```text
读 Metadata
校验 App A Header
CRC 校验
跳转到 0x08020200
```

App 正常跑起来后，CAN/UDS/DTC 应该恢复。

---

## 9. 第一次验证

### 9.1 正常启动

```text
上电
  → Bootloader
  → App A
  → OLED / CAN 心跳
```

### 9.2 验证向量表

App 里如果有中断，必须正常；  
如果 UDS 超时，先查：

```text
VECT_TAB_OFFSET 是否为 0x20200
SCB->VTOR 是否等于 0x08020200
```

### 9.3 验证 DTC 迁移

DTC 已从 sector 7 移到：

```text
0x08010000 sector 4
```

App A 启动后：

```bash
./uds_cli can0 read-dtc
```

如果之前的 DTC 不见了是正常的：分区迁移后旧数据不读。

---

## 10. 后续再测

这一轮只验证：

```text
Bootloader 能启动 App A
```

A/B 切换、CRC 坏、trial 回滚需要：

```text
1. 烧 App B
2. 写 Metadata 指向 B
3. 让 App 自检失败
```

这些下一步再做，不要第一次烧录就全上。

---

## 11. 常见问题

### Bootloader 起来后停住

检查：

```text
App A Header 是否在 0x08020000
CRC 是否正确
App A 是否从 0x08020200 开始
```

### App 能进 main 但中断不对

检查：

```text
VECT_TAB_OFFSET
SCB->VTOR
链接脚本 ORIGIN
```

三者必须一致：

```text
App A: 0x20200
App B: 0x60200
```
