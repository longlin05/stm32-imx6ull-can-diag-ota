# 文档目录

| 文档 | 作用 | 状态 |
|---|---|---|
| [scope-a.md](scope-a.md) | 项目 A 的目标、范围、验收标准（A 方案） | 当前 |
| [baseline-audit.md](baseline-audit.md) | 旧 CAN 项目审计：哪些复用、哪些重写、哪些不要 | 待填写 |
| [bringup.md](bringup.md) | 硬件接线、上电顺序、第一次通信 | 初稿 |
| [module-import.md](module-import.md) | 模块代码导入清单、丢弃项、PC1 冲突 | 当前 |
| [protocol.md](protocol.md) | CAN 应用层、ISO-TP、UDS、DTC、OTA 协议 | 初稿 |
| [test-report.md](test-report.md) | 测试记录模板与验收指标 | 模板 |
| [blog-series.md](blog-series.md) | 博客系列索引、写作规范、第一篇提纲 | 初稿 |

## 写作规则

1. **计划 ≠ 结果**：未完成的功能写在 Roadmap，不写在测试结果里。
2. **数字必须有日志**：每个指标对应 `test/records/` 下的原始日志。
3. **协议先写文档再写代码**：字段、字节序、超时、错误码先定，避免边写边改。
4. **每个里程碑更新一次文档**：不要最后补。
