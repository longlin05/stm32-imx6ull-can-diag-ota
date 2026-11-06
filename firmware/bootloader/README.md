# Bootloader

A 项目的 STM32F407 Bootloader。当前是**第一阶段：核心逻辑 + 主机测试**，还没有接 HAL Flash 和跳转。

## 当前完成

- CRC-32/ISO-HDLC；
- App Header 校验（magic / length / CRC32）；
- 向量表校验（SP 在 SRAM、Reset_Handler 在槽内、Thumb 位）；
- Metadata 双记录选择（seq + CRC）；
- trial / confirm / rollback 策略；
- 主机单元测试。

```bash
cd firmware/bootloader
make test
# boot core tests: OK
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

- HAL Flash 读写/擦除适配；
- Metadata 双 sector 写入；
- 跳转代码；
- App A/B 链接脚本；
- App 侧 `Boot_Confirm()`；
- 把 DTC 从 `0x08060000` 迁到 `0x08010000`（当前 DTC 仍占 App B 槽，硬件阶段必须先迁）。

## 下一步

1. 建 CubeIDE Bootloader 工程（32KB，sector 0-1）；
2. 接 `boot_flash`（HAL）；
3. 接 `boot_meta` 双 sector；
4. 实现跳转；
5. 建 App A/B 两个链接配置；
6. 测试正常启动、CRC 坏、trial 回滚。
