# gateway

i.MX6ULL Linux 网关：SocketCAN、ISO-TP 客户端、UDS 诊断、OTA 服务端、SQLite、MQTT、systemd。

## 目录建议

```text
gateway/
├── src/
│   ├── main.c
│   ├── can_rx.c
│   ├── isotp.c
│   ├── uds_client.c
│   ├── ota.c
│   ├── storage.c
│   ├── mqtt.c
│   ├── config.c
│   └── ringbuf.c
├── include/
├── systemd/can-gateway.service
├── CMakeLists.txt
└── README.md
```

## 实现顺序

1. SocketCAN 打开、过滤、`epoll` 收包；
2. 解析 baseline 帧；
3. ISO-TP 客户端；
4. UDS `0x10/0x3E/0x22/0x19/0x14`；
5. OTA 分片/ACK/重试；
6. SQLite + MQTT；
7. systemd + 日志。

## 约定

- 收包/解析/落盘/上报解耦；
- 队列有界，满了丢最旧并计数；
- SQLite 用 WAL + 批量事务；
- MQTT 重连指数退避；
- 所有错误写日志，不静默失败。
