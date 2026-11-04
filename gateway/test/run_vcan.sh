#!/bin/sh
# vcan 自测：需要 root 与内核 vcan 模块（modprobe vcan）。
# 在任意目录运行均可：./test/run_vcan.sh 或 cd test && ./run_vcan.sh
set -e
IF="${1:-vcan0}"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
GW="$SCRIPT_DIR/../gw"
MOCK="$SCRIPT_DIR/../mock_ecu"

sudo ip link add "$IF" type vcan 2>/dev/null || true
sudo ip link set "$IF" up

"$MOCK" "$IF" &
MOCK_PID=$!
trap 'kill "$MOCK_PID" 2>/dev/null || true' EXIT

sleep 0.3
"$GW" "$IF"
