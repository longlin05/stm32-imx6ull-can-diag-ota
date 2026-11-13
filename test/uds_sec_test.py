#!/usr/bin/env python3
"""UDS 0x27 SecurityAccess / 0x2E WriteDataByIdentifier 板上测试。

在 i.MX6ULL 上直接跑（python3 + Linux 原生 CAN socket），不依赖 uds_cli：

    python3 uds_sec_test.py [can0]

依赖：can0 已 up、STM32 已上电、总线上有 ACK。
"""

import socket
import struct
import sys
import time

CAN_IF = sys.argv[1] if len(sys.argv) > 1 else "can0"

REQ_ID = 0x7E0      # tester -> ECU（gateway/include/uds.h）
RESP_ID = 0x7E8     # ECU -> tester

LEVEL = 1


def iso_tp_send(sock, payload):
    """请求都 <= 7 字节，单帧足够。"""
    assert len(payload) <= 7, "本脚本只发单帧请求"
    sock.send(b"\x02" + bytes(payload) + b"\x00" * (7 - len(payload)))


def iso_tp_recv(sock, timeout=1.0):
    """收一条完整的 ISO-TP 响应（单帧或多帧）。"""
    end = time.time() + timeout
    data = bytearray()

    while time.time() < end:
        sock.settimeout(max(0.05, end - time.time()))
        try:
            frame = sock.recv(16)
        except socket.timeout:
            return None

        pci = frame[0] >> 4
        if pci == 0:                       # SF
            n = frame[0] & 0x0F
            return bytes(frame[1:1 + n])
        if pci == 1:                       # FF
            total = ((frame[0] & 0x0F) << 8) | frame[1]
            data = bytearray(frame[2:])
            sock.send(b"\x30\x00\x00\x00\x00\x00\x00\x00")   # FC: CTS
            continue
        if pci == 2:                       # CF
            data.extend(frame[1:])
            if len(data) >= total:
                return bytes(data[:total])

    return None


def uds(sock, *payload):
    iso_tp_send(sock, payload)
    return iso_tp_recv(sock)


def calc_key(seed):
    """与 uds_server.c 里的 uds_calc_key() 保持一致。"""
    k = seed ^ 0x5A5A5A5A
    k = ((k << 13) | (k >> 19)) & 0xFFFFFFFF
    k ^= 0xA5A5A5A5
    k = (k * 2654435761) & 0xFFFFFFFF
    return k ^ 0x55AA55AA


def show(tag, resp):
    if resp is None:
        print("%-38s -> 超时" % tag)
        return None
    print("%-38s -> %s" % (tag, " ".join("%02X" % b for b in resp)))
    return resp


def main():
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_IF,))
    sock.connect((CAN_IF, RESP_ID))

    ok = fail = 0

    def check(tag, resp, expect):
        nonlocal ok, fail
        if resp is None:
            fail += 1
            return
        if resp[:len(expect)] == bytes(expect):
            ok += 1
        else:
            fail += 1

    print("== 1. 进扩展会话 / 读基线")
    r = show("10 03", uds(sock, 0x10, 0x03))
    check("10 03", r, [0x50, 0x03])
    show("22 F1A0（PIR 开关）", uds(sock, 0x22, 0xF1, 0xA0))

    print("\n== 2. 未解锁就写，应当 7F 2E 33")
    r = show("2E F1A0 00", uds(sock, 0x2E, 0xF1, 0xA0, 0x00))
    check("2E 未解锁", r, [0x7F, 0x2E, 0x33])

    print("\n== 3. 请求 seed")
    r = show("27 01", uds(sock, 0x27, 0x01))
    if r is None or r[0] != 0x67:
        print("拿不到 seed，后面的用例跳过")
        return 1
    seed = struct.unpack(">I", bytes(r[2:6]))[0]

    print("\n== 4. 错误 key，应当 7F 27 35")
    r = show("27 02 <wrong>", uds(sock, 0x27, 0x02, 0xDE, 0xAD, 0xBE, 0xEF))
    check("错 key", r, [0x7F, 0x27, 0x35])

    print("\n== 5. 重新取 seed 后再发正确 key")
    r = show("27 01", uds(sock, 0x27, 0x01))
    seed = struct.unpack(">I", bytes(r[2:6]))[0]
    key = calc_key(seed)
    print("   seed=0x%08X  key=0x%08X" % (seed, key))
    kb = struct.pack(">I", key)
    r = show("27 02 <key>", uds(sock, 0x27, 0x02, *kb))
    check("正确 key", r, [0x67, 0x02])

    print("\n== 6. 已解锁，写 F1A0 关掉 PIR")
    r = show("2E F1A0 00", uds(sock, 0x2E, 0xF1, 0xA0, 0x00))
    check("写 F1A0", r, [0x6E, 0xF1, 0xA0])
    show("22 F1A0（应为 00）", uds(sock, 0x22, 0xF1, 0xA0))

    print("\n== 7. 写周期参数 F1A1 / F1A2（小端）")
    r = show("2E F1A1 88 13 (5000ms)", uds(sock, 0x2E, 0xF1, 0xA1, 0x88, 0x13))
    check("写 F1A1", r, [0x6E, 0xF1, 0xA1])
    show("22 F1A1", uds(sock, 0x22, 0xF1, 0xA1))
    r = show("2E F1A1 00 00（越界，应 7F 2E 31）", uds(sock, 0x2E, 0xF1, 0xA1, 0x00, 0x00))
    check("越界", r, [0x7F, 0x2E, 0x31])

    print("\n== 8. 写只读 DID，应当 7F 2E 31")
    r = show("2E F190 ...", uds(sock, 0x2E, 0xF1, 0x90, 0x41))
    check("只读 DID", r, [0x7F, 0x2E, 0x31])

    print("\n== 9. 恢复 PIR 开关")
    r = show("2E F1A0 01", uds(sock, 0x2E, 0xF1, 0xA0, 0x01))
    check("恢复 PIR", r, [0x6E, 0xF1, 0xA0])

    print("\n== 10. 回默认会话后 0x27 应当 7F 27 7F")
    show("10 01", uds(sock, 0x10, 0x01))
    r = show("27 01", uds(sock, 0x27, 0x01))
    check("默认会话拒绝 27", r, [0x7F, 0x27, 0x7F])

    print("\n== 11. 连续 3 次错误 key，应当 35 -> 35 -> 36，然后 37")
    uds(sock, 0x10, 0x03)
    for i in range(3):
        uds(sock, 0x27, 0x01)
        r = show("27 02 <wrong> #%d" % (i + 1),
                 uds(sock, 0x27, 0x02, 0x00, 0x00, 0x00, 0x00))
        if i < 2:
            check("错 key #%d" % (i + 1), r, [0x7F, 0x27, 0x35])
        else:
            check("锁定", r, [0x7F, 0x27, 0x36])
    uds(sock, 0x27, 0x01)
    r = show("27 02（锁定期内）", uds(sock, 0x27, 0x02, 0x11, 0x22, 0x33, 0x44))
    check("延迟中", r, [0x7F, 0x27, 0x37])

    print("\n结果：%d 项符合预期，%d 项不符" % (ok, fail))
    return 0 if fail == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
