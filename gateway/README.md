# gateway — 诊断/升级网关

在 imx6ull 上做 CAN 诊断/升级网关：南向 ISO-TP + UDS，北向 v1 只做 CLI + JSON。

- 设计契约：[DESIGN.md](DESIGN.md)
- 字节级协议：[PROTOCOL.md](PROTOCOL.md)（STM32 侧实现参照它）

## 当前状态

- ✅ 平台无关 ISO-TP 核心（[include/isotp.h](include/isotp.h) / [src/isotp.c](src/isotp.c)）：SF/FF/CF/FC 状态机，只走收发/定时回调，STM32 复用本文件。
- ✅ Linux socketcan 适配层（[src/can.c](src/can.c)）。
- ✅ vcan 自测对端（[test/mock_ecu.c](test/mock_ecu.c)）。
- ✅ 免 root 进程内自测（[test/selftest.c](test/selftest.c)）。
- ✅ UDS Client（[src/uds_client.c](src/uds_client.c)）+ CLI（[src/uds_cli.c](src/uds_cli.c)）：0x10 / 0x3E / 0x22。
- ⬜ STM32 UDS Server 联调、DTC、FOTA。

## 构建

```bash
make            # 产物 gw / mock_ecu / selftest / uds_cli
make test       # 进程内 ISO-TP 自测
# 交叉编译到 imx6ull：先 source NXP SDK 环境脚本，再 make
. /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
make
```

## UDS CLI

先拉起接口（`uds_cli` 不再自动执行 `ip link`，避免每次启动都 fork `ip` 导致卡顿）：

```bash
sudo ip link set can0 up type can bitrate 500000 restart-ms 100
ip -details link show can0
```

再运行：

```bash
./uds_cli can0 session 03
./uds_cli can0 tester-present
./uds_cli can0 read-did F190
./uds_cli can0 read-did F180
```

`F190` 返回 17 字节 VIN，会走 ISO-TP 多帧，正好验证 FF/CF。

## vcan 自测

```bash
./test/run_vcan.sh vcan0
# 或手动：
sudo ip link add vcan0 type vcan && sudo ip link set vcan0 up
./mock_ecu vcan0 &
./gw vcan0
```

`gw` 发 20 字节测试消息，`mock_ecu` 原样回显，`gw` 校验通过输出 `"ok":true`。
