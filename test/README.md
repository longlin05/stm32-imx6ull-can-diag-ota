# test

测试脚本、测试记录、故障注入工具。

## 目录建议

```text
test/
├── records/              # 原始日志（不要提交大文件）
├── scripts/
│   ├── can_smoke.sh
│   ├── uds_smoke.py
│   ├── ota_test.py
│   └── fault_inject.sh
└── README.md
```

## 测试原则

- 每个测试记录 commit、日期、接线、命令、原始日志；
- 先跑冒烟测试，再跑 24h；
- 失败必须能复现；
- 测试报告写到 `docs/test-report.md`。

## 常用命令

```bash
candump -ta can0
cansend can0 7E0#0322F18600000000
ip -details -statistics link show can0
dmesg -w
```
