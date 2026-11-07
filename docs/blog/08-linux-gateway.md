# STM32 + i.MX6ULL CAN 诊断与 OTA（08）：Linux 网关——SocketCAN、gatewayd、SQLite 与 MQTT，从“一次命令”到“常驻服务”

> 系列目录：[从“CAN 能通”到“可诊断、可升级”](https://blog.csdn.net/longlin101/article/details/169562720)
> 上一篇：07 OTA：0x34/0x36/0x37，A/B 双向升级与中断保持旧槽（本地稿，待发布）
> 仓库：[stm32-imx6ull-can-diag-ota](https://github.com/longlin05/stm32-imx6ull-can-diag-ota)
> 本文对应 tag：`a-v0.9-gateway`

前面的诊断和 OTA 都是：

```bash
./uds_cli can0 ...
./uds_cli can0 ota ...
```

每次启动一个进程，执行一条命令，退出。

这只能叫“诊断 CLI”，还不是网关。

这一篇把它变成：

```text
i.MX6ULL 常驻 gatewayd
  ├── SocketCAN / ISO-TP / UDS
  ├── gwctl 控制
  ├── SQLite 事件记录
  └── MQTT 上报
```

---

## TL;DR

```text
PC
  mosquitto broker 192.168.137.1:1883

i.MX6ULL
  eth0 192.168.137.103
  can0 500 kbps
  gatewayd
    ├── /run/gatewayd.sock
    ├── gateway.db
    └── MQTT publish

STM32
  App A
```

命令：

```bash
./gwctl status
./gwctl dtc
./gwctl read-did f1b0
./gwctl ota app_a.img 0x08020000
```

---

## 1. 为什么需要 gatewayd

`uds_cli` 的问题：

```text
一次性进程
无法周期采集
没有统一状态
MQTT 只能靠命令触发
CAN 断开后没人管
```

gatewayd 的职责：

```text
持有 CAN 接口
周期读 DTC
写 SQLite
发布 MQTT
通过 Unix socket 接收 gwctl 命令
执行 OTA
```

---

## 2. 架构

```text
+---------------------- PC -----------------------+
| mosquitto broker 192.168.137.1:1883            |
| mosquitto_sub -t 'gateway/#'                   |
+------------------------^-----------------------+
                         | MQTT
+------------------------+-----------------------+
|                 i.MX6ULL gatewayd              |
|                                                |
|  gwctl ── Unix socket ── gatewayd              |
|                           ├── SQLite           |
|                           └── SocketCAN        |
|                                 │              |
|                                 │ CAN 500k     |
+---------------------------------|--------------+
                                  │
+---------------------------------v--------------+
|                    STM32                       |
|  App A/B, UDS, Bootloader, OTA                 |
+------------------------------------------------+
```

---

## 3. gatewayd 的配置

```ini
can_if=can0
db=/home/root/my_ai/gateway.db
socket=/run/gatewayd.sock
mqtt_host=192.168.137.1
mqtt_port=1883
mqtt_prefix=gateway/imx6ull-01
poll_ms=5000
```

启动：

```bash
./gatewayd -c gatewayd.conf
```

日志：

```text
gatewayd: can=can0 db=/home/root/my_ai/gateway.db
          socket=/run/gatewayd.sock mqtt=192.168.137.1:1883
```

---

## 4. gwctl 与 Unix socket

`gwctl` 只做一件事：

```text
连接 /run/gatewayd.sock
发送一行文本命令
读回一行 JSON
```

命令：

```text
STATUS
DTC
SESSION 03
READDID F190
READDID F1B0
CLEARDTC
OTA app_a.img 0x08020000
```

返回：

```json
{"ok":true,"online":true,"can":"can0"}
{"ok":true,"cmd":"dtc","rx":"5902FFC00208"}
{"ok":true,"cmd":"read-did","rx":"62F1B000"}
```

这一步把“每次打开 CAN”变成了“daemon 持有 CAN”。

---

## 5. SQLite

表：

```sql
uds_events(
  id, ts, ifname, req_hex, resp_hex,
  req_len, resp_len, status
);

ota_sessions(
  id, ts, addr, size, path, status, detail
);
```

查询：

```bash
sqlite3 /home/root/my_ai/gateway.db \
  "select ts,req_hex,resp_hex,status from uds_events order by id desc limit 5;"
```

```text
2026-11-07T08:43:38|1902FF|5902FF|ok
2026-11-07T08:43:33|1902FF|5902FF|ok
```

OTA：

```text
2026-11-07T08:43:38|0x08020000|73528|ok|transfer complete
```

注意：

```text
0x36 每个块不记日志
只记 0x34/0x37 和普通 UDS
否则 OTA 一次写几千行
```

---

## 6. MQTT

gatewayd 发布：

```text
gateway/imx6ull-01/status
gateway/imx6ull-01/dtc
gateway/imx6ull-01/event
gateway/imx6ull-01/ota
```

PC 订阅：

```bat
mosquitto_sub.exe -h 192.168.137.1 -t "gateway/#" -v
```

输出：

```text
gateway/imx6ull-01/dtc {"ok":true,"cmd":"dtc","rx":"5902FFC00208"}
gateway/imx6ull-01/status {"ok":true,"online":true,"can":"can0"}
gateway/imx6ull-01/event {"ok":true,"cmd":"read-did","rx":"62F1B000"}
```

---

## 7. 坑一：ARM 上没有 libmosquitto

一开始想让 gatewayd 链接 `libmosquitto`。

但：

```text
PC 上装的是 x86 libmosquitto
i.MX6ULL 是 ARM
不能直接拷库
交叉工具链 sysroot 里也没有 ARM 版
```

`pkg-config` 找不到：

```text
Package libmosquitto was not found
```

最后改成：

```text
有 libmosquitto → 用库
没有库         → 内置最小 MQTT 3.1.1 QoS0 发布器
```

内置客户端只做：

```text
TCP connect
CONNECT
PUBLISH QoS0
close
```

不依赖 ARM 版 mosquitto，也能让 PC 收到消息。

代价：

```text
每 5 秒重新建 TCP 连接
没有 keepalive
没有断线补传
```

后面再做长连接。

---

## 8. 坑二：x86 的 SQLite 也不能直接拷

板子上：

```text
ldd gatewayd
  libsqlite3.so.0 => /usr/lib/libsqlite3.so.0
```

运行库是 ARM 的，能加载。

但数据库打不开：

```text
unable to open database file
```

原因不是 x86 库，而是路径：

```text
配置写的是 /root/my_ai/gateway.db
板子的 home 是 /home/root
/root/my_ai 不存在
```

改成：

```ini
db=/home/root/my_ai/gateway.db
```

数据库才创建。

---

## 9. 坑三：UDS 串包

gatewayd 每 5 秒读 DTC：

```text
7E0  03 19 02 FF
7E8  06 59 02 FF C0 02 08
```

此时执行 OTA：

```text
0x34 请求
```

结果 SQLite 出现：

```text
3400440802000000011F38|5902FF|ok
```

`0x34` 收到了 **上一条 DTC 的 `59 02 FF`**，被当成 `0x34` 响应。

修复两层：

### 1. 发送前清空 socket 残留

```c
while (can_poll(c->fd, 0) > 0) {
    struct can_frame old;
    while (can_read(c->fd, &old) == (int)sizeof old) {
    }
}
```

### 2. 校验 UDS 响应 SID

```c
if (rx[0] == 0x7F) {
    if (rx[1] != req[0]) discard;
} else if (rx[0] != req[0] + 0x40) {
    discard;
}
```

之后：

```text
0x34 → 只接受 0x74 / 7F 34
0x19 → 只接受 0x59 / 7F 19
```

---

## 10. 坑四：gatewayd 反复消失

测试时经常：

```text
connect /run/gatewayd.sock: No such file or directory
```

原因：

```text
前台启动后 Ctrl-C
```

`gatewayd` 是前台进程，Ctrl-C 就退出了。

正确：

```bash
pkill -f gatewayd
cd /home/root/my_ai
nohup ./gatewayd -c gatewayd.conf >/var/log/gatewayd.log 2>&1 &
sleep 1
ps | grep gatewayd
```

或者用 rc.local 开机自启。

---

## 11. 坑五：固定 IP 被 connman 改回 DHCP

`/etc/network/interfaces`：

```conf
iface eth0 inet static
    address 192.168.137.103
```

但重启后变成：

```text
192.168.137.199/24
```

原因：

```text
connmand 在跑
它接管 eth0，重新 DHCP
```

处理：

```bash
/etc/init.d/connman stop
update-rc.d -f connman remove
```

然后：

```bash
ifdown eth0
ip addr flush dev eth0
ifup eth0
```

固定 IP 才稳定。

---

## 12. 开机自启：SysV 没有 systemd

板子是：

```text
init 1 = init
/etc/init.d/rc.local
```

没有 `/etc/systemd/system`。

所以用 `/etc/rc.local`：

```sh
/sbin/ifup eth0 2>/dev/null
/sbin/ip link set can0 up type can bitrate 500000 restart-ms 100 2>/dev/null
sleep 2

cd /home/root/my_ai
nohup ./gatewayd -c gatewayd.conf >/var/log/gatewayd.log 2>&1 &
```

---

## 13. F1B0：当前槽 DID

之前判断 A/B 只能靠 ST-Link 看 PC。

加一个 DID：

```text
22 F1 B0
→ 62 F1 B0 00   App A
→ 62 F1 B0 01   App B
```

实现：

```c
void UdsServer_SetAppSlot(uint8_t slot)
{
  s_app_slot = (slot == 0U) ? 0U : 1U;
}
```

App A/B 各自调用：

```c
UdsServer_SetAppSlot(0U);  // A
UdsServer_SetAppSlot(1U);  // B
```

之后：

```bash
./gwctl read-did f1b0
{"ok":true,"cmd":"read-did","rx":"62F1B000"}
```

不用 ST-Link。

---

## 14. gatewayd OTA

现在 OTA 也进 daemon：

```bash
./gwctl ota /home/root/my_ai/app_a.img 0x08020000
```

返回：

```json
{"ok":true,"cmd":"ota","addr":"0x08020000","size":73528,"status":"ok"}
```

流程：

```text
0x34
0x36 ...
0x37
写 ota_sessions
MQTT 发布
复位
Bootloader trial
App confirm
```

---

## 15. 当前验证

```text
gatewayd 启动                 通过
gwctl status/dtc/read-did     通过
SQLite                        通过
MQTT publish                  通过
gatewayd OTA                  通过
F1B0                          通过
固定 IP                       通过
rc.local 自启                 通过
```

未做：

```text
24h/72h
50 次 OTA
MQTT 断线补传
MQTT 长连接
CPU/RSS 统计
```

---

## 16. 下一步

```text
1. MQTT 长连接 + keepalive
2. 24h 运行
3. 50 次 OTA
4. 测试报告
```

---

## 17. 仓库 / 数据

- 仓库：https://github.com/longlin05/stm32-imx6ull-can-diag-ota
- tag：`a-v0.9-gateway`
- 网关源码：`gateway/`
- 测试日志：`test/records/20261107-gateway-smoke.log`

标签：`Linux` `SocketCAN` `i.MX6ULL` `UDS` `MQTT` `SQLite` `OTA`
