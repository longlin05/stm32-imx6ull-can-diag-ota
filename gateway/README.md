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
- ✅ UDS / DTC / OTA 已联调（详见项目根 README）。
- 🟡 SQLite 事件日志（可选编译，`GW_DB` 指定数据库）。
- ⬜ MQTT / 常驻网关。

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

./uds_cli can0 read-dtc
./uds_cli can0 read-dtc-supported
./uds_cli can0 read-dtc-count
./uds_cli can0 clear-dtc

# OTA：把镜像写到非活动槽
./uds_cli can0 ota app_a.img 0x08020000
./uds_cli can0 ota app_b.img 0x08060000
```

`F190` 返回 17 字节 VIN，会走 ISO-TP 多帧，正好验证 FF/CF。

`read-dtc` 发 `19 02 FF`；`read-dtc-supported` 发 `19 0A`；
`read-dtc-count` 发 `19 01 FF`；`clear-dtc` 发 `14 FF FF FF`。

## SQLite 日志

需要 sqlite3 开发库：

```bash
sudo apt install libsqlite3-dev
make clean && make
```

如果 `pkg-config` 能识别 sqlite3，Makefile 会自动加 `-DGW_WITH_SQLITE` 和 `-lsqlite3`。

运行：

```bash
GW_DB=/root/my_ai/gateway.db ./uds_cli can0 read-dtc
sqlite3 /root/my_ai/gateway.db   "select ts,req_hex,resp_hex,status from uds_events order by id desc limit 10;"
```

没有 SQLite 时仍然可以编译运行，只是 `gw_log_*` 是空操作。

0x36 的每个块不记日志，只记 0x34/0x37 和普通诊断请求，避免 OTA 时数据库爆炸。

## gatewayd / gwctl

```bash
# 先确保 can0 已 up
sudo ./gatewayd -c gatewayd.conf

# 另一个终端
./gwctl status
./gwctl dtc
./gwctl session 03
./gwctl read-did F190
./gwctl clear-dtc

# OTA 由 gatewayd 执行，写入非活动槽
./gwctl ota app_a.img 0x08020000
./gwctl ota app_b.img 0x08060000
```

`gatewayd` 默认读 `/etc/gatewayd.conf`，仓库里有一份示例：

```ini
can_if=can0
db=/root/my_ai/gateway.db
socket=/run/gatewayd.sock
mqtt_host=192.168.137.1
mqtt_port=1883
mqtt_prefix=gateway/imx6ull-01
poll_ms=5000
```

当前 `gatewayd` 支持：

```text
status / dtc / session / read-did / clear-dtc / ota
周期 DTC + SQLite + MQTT publish
```

OTA 现在由 gatewayd 执行，并写入 `ota_sessions` 表。

MQTT 客户端：

```text
有 libmosquitto 时用 libmosquitto
没有库时用内置最小 MQTT 3.1.1 QoS0 发布器
```

所以交叉编译不再强依赖 ARM 版 libmosquitto。

## vcan 自测

```bash
./test/run_vcan.sh vcan0
# 或手动：
sudo ip link add vcan0 type vcan && sudo ip link set vcan0 up
./mock_ecu vcan0 &
./gw vcan0
```

`gw` 发 20 字节测试消息，`mock_ecu` 原样回显，`gw` 校验通过输出 `"ok":true`。
