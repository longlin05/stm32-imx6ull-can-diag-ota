# gateway — 诊断/升级网关（ISO-TP 核心完成，UDS 未开始）

在 imx6ull 上做 CAN 诊断/升级网关：南向 ISO-TP + UDS，北向 v1 只做 CLI + JSON。

- 设计契约：[DESIGN.md](DESIGN.md)
- 字节级协议：[PROTOCOL.md](PROTOCOL.md)（STM32 侧实现参照它）

## 当前状态

- ✅ 平台无关 ISO-TP 核心（[include/isotp.h](include/isotp.h) / [src/isotp.c](src/isotp.c)）：SF/FF/CF/FC 状态机，只走收发/定时回调，STM32 复用本文件。
- ✅ 流控：BS=0/8、STmin=0/5ms；N_Bs/N_Cr 超时；错序 CF 回 OVFLW。
- ✅ Linux socketcan 适配层（[src/can.c](src/can.c)）。
- ✅ vcan 自测对端（[test/mock_ecu.c](test/mock_ecu.c)）。
- ✅ 免 root 进程内自测（[test/selftest.c](test/selftest.c)）：1/7/8/20/100/1024/4095、BS8/ST5、丢 CF、N_Cr/N_Bs。
- ⬜ UDS 服务层 + CLI（下一步）。

## 构建

```bash
make            # 本机 x86 语法验证，产物 gw / mock_ecu / selftest
make test       # 进程内自测（免 root，验证 ISO-TP 状态机）
# 交叉编译到 imx6ull：先 source NXP SDK 环境脚本，再 make
. /opt/fsl-imx-x11/4.1.15-2.1.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
make
```

## vcan 自测

```bash
./test/run_vcan.sh vcan0
# 或手动：
sudo ip link add vcan0 type vcan && sudo ip link set vcan0 up
./mock_ecu vcan0 &
./gw vcan0
```

`gw` 发 20 字节测试消息，`mock_ecu` 原样回显，`gw` 校验通过输出 `"ok":true`。
