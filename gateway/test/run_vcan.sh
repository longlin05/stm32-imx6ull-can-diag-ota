#!/bin/sh
# vcan 自测：需要 root 与内核 vcan 模块（modprobe vcan）。
set -e
IF="${1:-vcan0}"

sudo ip link add "$IF" type vcan 2>/dev/null || true
sudo ip link set "$IF" up

./mock_ecu "$IF" &
MOCK_PID=$!
trap 'kill "$MOCK_PID" 2>/dev/null || true' EXIT

sleep 0.3
./gw "$IF"
