# bootloader

STM32F407 Bootloader：启动选择、CRC32、A/B 分区、跳转、试运行、回滚。

## 目录建议

```text
bootloader/
├── Core/
├── App/
│   ├── Inc/
│   │   ├── flash_layout.h
│   │   ├── ota_meta.h
│   │   ├── crc32.h
│   │   └── jump.h
│   └── Src/
│       ├── flash_layout.c
│       ├── ota_meta.c
│       ├── crc32.c
│       └── jump.c
├── .ioc
└── README.md
```

## 启动流程

1. 读 Config 区两份 metadata（magic + CRC + seq），选有效且序号大的；
2. 校验目标 App 的栈顶/向量表/CRC32；
3. 有效则跳转；无效则尝试另一个分区；
4. 跳转前关中断、复位外设、设置 `SCB->VTOR`、设置 MSP；
5. 新 App 在 5 s 内写 confirm；否则看门狗复位后回滚。

## 注意

- F407 扇区大小不均匀，先定分区表再写代码；
- 不要在中断里擦写 Flash；
- 不要擦当前活动分区；
- 跳转前确认 `SCB->VTOR` 和 MSP；
- 每个版本记录 `version + crc32 + size`。
