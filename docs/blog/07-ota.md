# STM32 + i.MX6ULL CAN 诊断与 OTA（07）：OTA 实战——0x34/0x36/0x37、A/B 双向升级与中断保持旧槽

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：[06 Bootloader：A/B 分区、跳转与回滚](https://blog.csdn.net/longlin101/article/details/170201376)
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.8-ota`

Bootloader 解决“启动哪个槽”。

OTA 解决“新固件怎么安全地进入另一个槽”。

这一篇做最小可用 OTA：

```text
0x34 RequestDownload
0x36 TransferData
0x37 RequestTransferExit
```

以及：

```text
A → B
B → A
传输中断
trial / confirm
```

---

## TL;DR

```text
当前 App 运行中
  → 0x34 擦除非活动槽
  → 0x36 分块写入
  → 0x37 校验 Header/CRC + 写 Metadata trial
  → 复位
  → Bootloader 试启动新槽
  → 新 App confirm
  → 完成切换
```

中断或掉电：

```text
非活动槽可能半包
Metadata 不变
旧槽仍可启动
```

---

## 1. OTA 不是“发 bin”

如果直接把固件写到当前运行槽：

```text
擦除时当前 App 没了
写入中断 = 板子变砖
```

OTA 的第一原则：

> **只写非活动槽。**

当前：

```text
active = B
target = A
```

OTA 只碰：

```text
0x08020000 ~ 0x0805FFFF
```

当前 B 完全不动。

---

## 2. 镜像格式

Linux 发送的不是裸 bin，而是：

```text
app_a.img
= 512B Header + App bin
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

写入后：

```text
0x08020000  Header
0x08020200  向量表 / App
```

Bootloader 校验的就是这些。

---

## 3. `0x34` RequestDownload

第一版格式：

```text
34 00 44 <addr 4B> <size 4B>
```

```text
00   不压缩、不加密
44   地址 4 字节、长度 4 字节
addr 目标槽基地址
size 整个 img 大小
```

例如：

```text
34 00 44 08 02 00 00 00 01 1F 38
```

响应：

```text
74 20 04 00
```

```text
0x0400 = 1024 字节最大块
```

服务端做：

```text
1. 检查目标是不是非活动槽
2. 检查 size 不超过槽大小
3. 擦除目标槽两个 sector
4. 记录 OTA 上下文
```

---

## 4. `0x36` TransferData

```text
36 <blockCounter> <data...>
```

响应：

```text
76 <blockCounter>
```

规则：

```text
blockCounter 从 1 开始
0xFF 后回绕到 0
每块最多 1024 字节
第一版要求 data 长度 4 字节对齐
```

---

## 5. `0x37` RequestTransferExit

```text
37
```

服务端：

```text
1. 确认 written == size
2. 读目标槽 Header
3. 校验 magic / length / CRC32
4. 校验向量表 SP / Reset_Handler
5. 写 Metadata：trial = 目标槽
6. 返回 77
```

`0x37` 成功只表示：

```text
新槽完整，可以下次复位试运行
```

并不表示已经切换。

---

## 6. 一个 off-by-one

第一版 `0x36` 实现错误：

```c
data_len = len - 1;
```

请求是：

```text
36 <counter> <data>
```

`len - 1` 把 `counter` 也当成了数据，所以：

```text
1024 字节数据 → data_len = 1025
1025 不是 4 的倍数
返回 NRC 0x13
```

现象：

```text
0x34 OK
0x36 failed at offset 0
```

修复：

```c
data_len = len - 2;
```

这个坑很典型：**协议字段计数错一位，第一块就失败。**

---

## 7. 一次完整 OTA

当前 active=B，目标 A：

```bash
./uds_cli can0 ota app_a.img 0x08020000
```

输出：

```text
0x34 OK, image=73528 bytes, addr=0x08020000, max_block=1024
  10240/73528
  20480/73528
  ...
OTA image transferred, 0x37 OK
```

复位后：

```text
PC = 0x08025CCC
Metadata:
  seq=21
  active=A
  trial=NONE
  states=[confirmed, confirmed]
```

App A 启动并确认。

反向 B：

```bash
./uds_cli can0 ota app_b.img 0x08060000
```

复位：

```text
PC = 0x08065CD8
Metadata:
  active=B
```

双向都通过。

---

## 8. 中断测试

当前 active=B，OTA 写 A：

```text
0x34 OK
...
20480/73528
Ctrl-C
```

复位：

```text
PC = 0x08065CD4
Metadata:
  active=B
  trial=NONE
```

A 槽是半包，但：

```text
Metadata 没改
Bootloader 继续 B
```

这就是 A/B 的价值。

---

## 9. 当前边界

已实现：

```text
0x34 / 0x36 / 0x37
A↔B
Header/CRC 校验
trial / confirm
中断保持旧槽
```

未实现：

```text
10 次循环
50 次升级
多点掉电
断点续传
0x27 安全访问
签名/加密
自动复位
```

这些是可靠性和量产项，不是功能闭环必需。

---

## 10. 为什么没有自动复位

`0x37` 后没有调用 `NVIC_SystemReset()`。

原因：

- CLI 需要先收到 `77` 响应；
- 人工确认更安全；
- 调试时可以先看 Metadata 再决定复位。

后面可以增加：

```text
0x37 成功 → 延时 200ms → NVIC_SystemReset()
```

---

## 11. 下一步

OTA 功能闭环后，项目 A 的核心就齐了：

```text
CAN
ISO-TP
UDS
DTC
Bootloader
OTA
```

后面做：

```text
Linux 网关 SQLite/MQTT
24h/72h
50 次升级
简历/测试报告
```

---

## 12. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.8-ota`
- OTA 服务端：`firmware/bootloader/ota_server.c`
- Linux CLI：`gateway/src/uds_cli.c`
- 测试日志：`test/records/20261106-ota-smoke.log`

标签：`STM32` `CAN` `UDS` `OTA` `Bootloader` `i.MX6ULL`
