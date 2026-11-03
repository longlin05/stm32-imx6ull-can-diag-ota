# gateway

i.MX6ULL Linux 网关：SocketCAN 收包、AImate 协议解析；后续加 ISO-TP/UDS/OTA/SQLite/MQTT。

## 当前内容

```text
gateway/
├── include/
│   ├── can.h              # SocketCAN 封装
│   └── can_protocol.h     # AImate 协议定义（从旧工程复制）
├── src/
│   ├── can.c              # open/filter/read/send/bring-up
│   └── can_recv.c         # 接收并解析 0x111/0x112/0x113/0x221/0x222/0x228
├── Makefile
└── README.md
```

## 编译

本机（Linux）：

```bash
cd gateway
make
```

交叉编译 i.MX6ULL：

```bash
make clean
make CC=arm-linux-gnueabihf-gcc
```

产物：`gateway/build/can_recv`。

## 运行

先在板子上拉起 CAN：

```bash
sudo ip link set can0 down
sudo ip link set can0 up type can bitrate 500000 sample-point 0.875
ip -details -statistics link show can0
```

再运行：

```bash
sudo ./build/can_recv can0
```

输出示例：

```text
CAN receiver on can0 (AImate 0x111/0x112/0x113, cfg 0x221/0x222/0x228)
[PIR] event=1 state=1 raw=1 flags=0x03 ts=123456ms
[ENV] temp=24.56 C hum=55.20 %RH status=0 drv=0 seq=42
[HB] uptime=120s tx_drop=0 fw=0x10 health=0x03
```

## 协议

直接复用 AImate：

- `0x111` PIR；
- `0x112` 温湿度；
- `0x113` 心跳；
- `0x221/0x222/0x228` Linux → STM32 命令/ACK，**已在本地 `my_ai` 工程实现**；本目录只做接收/打印参考。

完整字段见 `include/can_protocol.h` 或主仓库 `firmware/reference/AImate/Docs/CAN_Protocol.md`。

## 已知限制

- `can_bring_up()` 用 `system("ip link ...")`，需要 root；生产环境应改用 netlink。
- 当前只做接收/打印，没有 SQLite/MQTT/ISO-TP/UDS/OTA。
- 本目录的 `0x221/0x222/0x228` 只打印，未做真正命令/ACK；完整实现见本地 `my_ai` 工程。
- 没有 `epoll`；下一步改成 `epoll` + 有界队列。
