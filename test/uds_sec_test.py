#!/usr/bin/env python3
"""UDS 0x27 SecurityAccess / 0x2E WriteDataByIdentifier 板上测试。

在 i.MX6ULL 上直接跑（python3 + Linux CAN socket），不依赖 uds_cli：

    PYTHONIOENCODING=utf-8 python3 uds_sec_test.py [can0]

注意：板子上的 Python 是 3.5，AF_CAN 的 connect()/sendto() 还不支持，
所以收用 socket、发用 cansend 子进程。
"""

import socket
import struct
import subprocess
import sys
import time

CAN_IF = sys.argv[1] if len(sys.argv) > 1 else "can0"

REQ_ID = 0x7E0      # tester -> ECU（gateway/include/uds.h）
RESP_ID = 0x7E8     # ECU -> tester


class Tester(object):
    def __init__(self, ifname):
        self.ifname = ifname
        self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        self.sock.bind((ifname,))
        self.sock.settimeout(0.2)

    def send(self, payload):
        """请求 <= 7 字节，用单帧；PCI 低 4 位写真实长度。"""
        assert len(payload) <= 7
        data = bytes([len(payload)]) + bytes(payload)
        data = data + bytes(8 - len(data))
        subprocess.check_call(["cansend", self.ifname,
                               "%03X#%s" % (REQ_ID, data.hex().upper())])

    def recv_frame(self):
        raw = self.sock.recv(16)
        can_id, dlc, data = struct.unpack("<II8s", raw[:16])
        return can_id & 0x1FFFFFFF, dlc, data[:dlc]

    def recv_pdu(self, timeout=1.0):
        """收一条完整 ISO-TP 响应（跳过自己发的 echo）。"""
        end = time.time() + timeout
        buf = bytearray()
        total = 0
        while time.time() < end:
            try:
                cid, dlc, data = self.recv_frame()
            except socket.timeout:
                continue
            if cid != RESP_ID or dlc == 0:
                continue

            pci = data[0] >> 4
            if pci == 0:                       # SF
                return bytes(data[1:1 + (data[0] & 0x0F)])
            if pci == 1:                       # FF
                total = ((data[0] & 0x0F) << 8) | data[1]
                buf = bytearray(data[2:])
                self.send([0x30, 0x00, 0x00])  # FC: CTS
                continue
            if pci == 2:                       # CF
                buf.extend(data[1:])
                if len(buf) >= total:
                    return bytes(buf[:total])
        return None

    def uds(self, *payload):
        self.send(payload)
        return self.recv_pdu()


def calc_key(seed):
    """与 uds_server.c 里 uds_calc_key() 对齐。"""
    k = (seed ^ 0x5A5A5A5A) & 0xFFFFFFFF
    k = ((k << 13) | (k >> 19)) & 0xFFFFFFFF
    k ^= 0xA5A5A5A5
    k = (k * 2654435761) & 0xFFFFFFFF
    return k ^ 0x55AA55AA


def main():
    t = Tester(CAN_IF)
    stats = {"ok": 0, "fail": 0}

    def show(tag, resp):
        if resp is None:
            print("%-40s -> 超时" % tag)
            return None
        print("%-40s -> %s" % (tag, " ".join("%02X" % b for b in resp)))
        return resp

    def check(tag, resp, expect):
        if resp is not None and resp[:len(expect)] == bytes(expect):
            stats["ok"] += 1
        else:
            stats["fail"] += 1
            print("   [FAIL] %s 期望 %s" % (tag, " ".join("%02X" % b for b in expect)))

    print("== 1. 扩展会话 + 基线")
    r = show("10 03", t.uds(0x10, 0x03))
    check("10 03", r, [0x50, 0x03])
    show("22 F1A0", t.uds(0x22, 0xF1, 0xA0))

    print("\n== 2. 未解锁就写 -> 7F 2E 33")
    r = show("2E F1A0 00", t.uds(0x2E, 0xF1, 0xA0, 0x00))
    check("未解锁写", r, [0x7F, 0x2E, 0x33])

    print("\n== 3. 请求 seed")
    r = show("27 01", t.uds(0x27, 0x01))
    if r is None or len(r) < 6 or r[0] != 0x67:
        print("拿不到 seed，后面的用例跳过")
        return 1
    seed = struct.unpack(">I", bytes(r[2:6]))[0]
    print("   seed = 0x%08X" % seed)

    print("\n== 4. 错误 key -> 7F 27 35")
    r = show("27 02 DEADBEEF", t.uds(0x27, 0x02, 0xDE, 0xAD, 0xBE, 0xEF))
    check("错 key", r, [0x7F, 0x27, 0x35])

    print("\n== 5. 重新取 seed + 正确 key")
    r = show("27 01", t.uds(0x27, 0x01))
    seed = struct.unpack(">I", bytes(r[2:6]))[0]
    key = calc_key(seed)
    print("   seed=0x%08X key=0x%08X" % (seed, key))
    kb = struct.pack(">I", key)
    r = show("27 02 <key>", t.uds(0x27, 0x02, kb[0], kb[1], kb[2], kb[3]))
    check("正确 key", r, [0x67, 0x02])

    print("\n== 6. 解锁后写 F1A0=00（关 PIR）")
    r = show("2E F1A0 00", t.uds(0x2E, 0xF1, 0xA0, 0x00))
    check("写 F1A0", r, [0x6E, 0xF1, 0xA0])
    show("22 F1A0（应为 00）", t.uds(0x22, 0xF1, 0xA0))

    print("\n== 7. 写 F1A1=5000ms（小端 88 13）")
    r = show("2E F1A1 88 13", t.uds(0x2E, 0xF1, 0xA1, 0x88, 0x13))
    check("写 F1A1", r, [0x6E, 0xF1, 0xA1])
    show("22 F1A1", t.uds(0x22, 0xF1, 0xA1))

    print("\n== 8. 越界写 F1A1=0 -> 7F 2E 31")
    r = show("2E F1A1 00 00", t.uds(0x2E, 0xF1, 0xA1, 0x00, 0x00))
    check("越界", r, [0x7F, 0x2E, 0x31])

    print("\n== 9. 写只读 DID F190 -> 7F 2E 31")
    r = show("2E F190 41", t.uds(0x2E, 0xF1, 0x90, 0x41))
    check("只读 DID", r, [0x7F, 0x2E, 0x31])

    print("\n== 10. 恢复 PIR 开关")
    r = show("2E F1A0 01", t.uds(0x2E, 0xF1, 0xA0, 0x01))
    check("恢复 PIR", r, [0x6E, 0xF1, 0xA0])

    print("\n== 11. 回默认会话后 27 -> 7F 27 7F")
    show("10 01", t.uds(0x10, 0x01))
    r = show("27 01", t.uds(0x27, 0x01))
    check("默认会话拒绝", r, [0x7F, 0x27, 0x7F])

    print("\n== 12. 连续错误 key：35 -> 35 -> 36，然后 37")
    t.uds(0x10, 0x03)
    for i in range(3):
        t.uds(0x27, 0x01)
        r = show("27 02 00000000 #%d" % (i + 1),
                 t.uds(0x27, 0x02, 0x00, 0x00, 0x00, 0x00))
        if i < 2:
            check("错 key #%d" % (i + 1), r, [0x7F, 0x27, 0x35])
        else:
            check("触发锁定", r, [0x7F, 0x27, 0x36])
    t.uds(0x27, 0x01)
    r = show("27 02（锁定期内）", t.uds(0x27, 0x02, 0x11, 0x22, 0x33, 0x44))
    check("延迟中", r, [0x7F, 0x27, 0x37])

    print("\n结果：%d 项符合预期，%d 项不符" % (stats["ok"], stats["fail"]))
    return 0 if stats["fail"] == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
