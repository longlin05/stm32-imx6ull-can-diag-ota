# Bootloader

A 项目的 STM32F407 Bootloader。A/B、CRC、Metadata、跳转、trial/rollback/confirm 已实测通过。

具体建法见：[BUILD_CUBEIDE.md](BUILD_CUBEIDE.md)。

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

## 已验证

```text
Bootloader → App A          通过
trial B 第一次启动           通过
未确认回滚 A                 通过
Boot_AppConfirm(1)          通过
确认后固定启动 B             通过
```

App A/B 都链接在各自槽位，UDS 正常。

## 下一步

1. OTA 传输：`0x34/0x36/0x37` 写非活动槽；
2. 升级完成写 Metadata trial；
3. 掉电/回滚测试；
4. 50 次升级测试。
