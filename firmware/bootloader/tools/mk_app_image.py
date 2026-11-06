#!/usr/bin/env python3
"""给 App bin 加上 Bootloader Header。

用法：
  python mk_app_image.py app.bin app_with_header.bin --version 1
  python mk_app_image.py app.bin app_with_header.bin --version 1 --build-id 0x1234

输出布局：
  0x000  header (512B)
  0x200  app bin
"""

import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0x31505041  # "APP1"
HEADER_SIZE = 0x200


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--version", type=lambda x: int(x, 0), default=1)
    p.add_argument("--build-id", type=lambda x: int(x, 0), default=0)
    args = p.parse_args()

    image = args.input.read_bytes()
    if not image:
        raise SystemExit("empty input")
    if len(image) > 0x40000 - HEADER_SIZE:
        raise SystemExit("image too large for 256KB slot")

    crc = zlib.crc32(image) & 0xFFFFFFFF
    header = struct.pack(
        "<IIIII",
        MAGIC,
        args.version & 0xFFFFFFFF,
        len(image),
        crc,
        args.build_id & 0xFFFFFFFF,
    )
    header += b"\x00" * (HEADER_SIZE - len(header))
    args.output.write_bytes(header + image)

    print(f"input : {args.input} ({len(image)} bytes)")
    print(f"output: {args.output} ({len(header) + len(image)} bytes)")
    print(f"magic : 0x{MAGIC:08X}")
    print(f"crc32 : 0x{crc:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
