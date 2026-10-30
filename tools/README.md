# tools

辅助工具：诊断 CLI、抓包转换、CRC 计算、固件打包、日志分析。

## 建议工具

| 工具 | 用途 |
|---|---|
| `diag_cli` | 会话、读 DID、读/清 DTC |
| `ota_cli` | 读固件、分片、升级、查进度 |
| `crc32` | 计算固件 CRC32 |
| `can_log_parser` | 从 candump 日志统计丢帧/周期 |
| `latency_report` | 从 CSV 计算 P50/P95/P99 |

## 约定

- 工具也要有 README 和示例；
- 不把大固件、日志提交进仓库；
- 测试脚本尽量可重复执行。
