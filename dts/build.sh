#!/bin/bash
# 从板子的厂商 DTB 生成带 DHT22 / PIR 节点的 DTB
#
# 为什么这么做：板子的 DTB 是正点原子改过的 14x14-evk，
# 虚拟机里的内核树是 NXP 原版，直接用源码树编 DTB 会丢掉厂商节点。
# 所以以板子自己的 DTB 为基底，反编译后追加节点再编回去。
#
# 依赖：dtc（Ubuntu: apt install device-tree-compiler）
# 用法：./build.sh [输出目录]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/out}
BASE=$HERE/base/imx6ull-14x14-emmc-4.3-480x272-c.dtb
NAME=$(basename "$BASE")

DTC=${DTC:-$(command -v dtc || true)}
if [ -z "$DTC" ]; then
    echo "找不到 dtc，先安装 device-tree-compiler 或用 DTC=/path/to/dtc 指定" >&2
    exit 1
fi

mkdir -p "$OUT"
cd "$OUT"

# 1) 反编译板子的 DTB
"$DTC" -q -I dtb -O dts -o board.dts "$BASE"

# 2) 自检：原 dts 编回去再反编译，应当和 board.dts 完全一致
"$DTC" -q -I dts -O dtb -o roundtrip.dtb board.dts
"$DTC" -q -I dtb -O dts -o roundtrip.dts roundtrip.dtb
if ! diff -q board.dts roundtrip.dts >/dev/null; then
    echo "自检失败：反编译-重编-反编译 结果不一致，停止" >&2
    exit 1
fi
echo "自检通过：dtc 往返无损"

# 3) 追加节点后编译
cat board.dts "$HERE/b-dht22-pir.dtsi" > board-b.dts
"$DTC" -q -I dts -O dtb -o "$NAME" board-b.dts

# 4) 确认只多了节点，没改别的东西
"$DTC" -q -I dtb -O dts -o board-b-check.dts "$NAME"
ADDED=$(diff board.dts board-b-check.dts | grep -c '^>' || true)
REMOVED=$(diff board.dts board-b-check.dts | grep -c '^<' || true)
echo "差异：新增 $ADDED 行，删除 $REMOVED 行（应当只有新增）"
if [ "$REMOVED" != "0" ]; then
    echo "自检失败：改动不止新增节点，停止" >&2
    exit 1
fi

echo "生成：$OUT/$NAME（$(stat -c %s "$OUT/$NAME") 字节，原 $(stat -c %s "$BASE") 字节）"
