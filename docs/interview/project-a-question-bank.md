# 项目 A 面试题库（48 题）

> 用法：先自己答，再对照“要点”。  
> 目标不是背答案，而是能画图、能举项目里的例子。

---

## 一、CAN（1–8）

### 1. 标准 CAN 帧有哪些字段？

```text
SOF
仲裁段（11 位 ID + RTR）
控制段（IDE/r0/DLC）
数据段（0–8 字节）
CRC 段
ACK 段
EOF
```

要点：

```text
ACK 由接收节点拉低
仲裁 ID 越小优先级越高
标准帧 11 位，扩展帧 29 位
```

### 2. CAN 为什么能仲裁？

```text
显性 0 覆盖隐性 1
节点边发边听
发 1 读到 0 → 退出仲裁
ID 最小的节点获胜
```

### 3. CAN 错误状态有哪些？

```text
Error Active
Error Passive
Bus Off
```

要点：

```text
TEC/REC 计数
TEC > 255 → Bus Off
Bus Off 后需要恢复流程
```

### 4. ACK 错误说明什么？

```text
发送节点在 ACK 位没有读到任何接收节点的显性 ACK
```

项目中的现象：

```text
Linux 0.866，STM32 0.75
→ STM32 发 0x74 时 ACK error
→ TX 邮箱不释放
→ no free mailbox
```

### 5. 位时间怎么组成？

```text
SYNC_SEG
PROP_SEG
PHASE_SEG1
PHASE_SEG2
```

```text
bit time = (1 + BS1 + BS2) × tq
tq = prescaler / fCAN
```

### 6. 采样点怎么算？

```text
sample point = (1 + BS1) / (1 + BS1 + BS2)
```

例：

```text
BS1=7, BS2=5
sample = (1+7)/(1+7+5) = 8/13 ≈ 0.615
```

实际项目以 `ip -details` 和 STM32 配置为准。

### 7. SJW 是什么？

```text
Synchronization Jump Width
重同步时允许调整的最大 tq 数
```

要点：

```text
SJW 太小 → 时钟偏差容限差
SJW 太大 → 抖动/采样不稳
```

### 8. Bus Off 后怎么恢复？

```text
硬件自动恢复（restart-ms）
或软件重新初始化 CAN
```

项目：

```bash
ip link set can0 up type can bitrate 500000 sample-point 0.75 restart-ms 100
```

---

## 二、ISO-TP（9–16）

### 9. ISO-TP 四种帧？

```text
SF 单帧
FF 首帧
CF 连续帧
FC 流控帧
```

### 10. SF/FF/CF/FC 的 PCI 怎么区分？

```text
高 4 位：
0x0  SF
0x1  FF
0x2  CF
0x3  FC
```

### 11. SF 长度怎么表示？

```text
低 4 位 = 数据长度
1..7
```

### 12. FF 长度怎么表示？

```text
12 位：PCI 低 4 位 + 第二字节
最大 4095
```

### 13. CF 的 SN 怎么走？

```text
从 1 开始
1..15
15 后回 0
```

### 14. FC 的 FS/BS/STmin 是什么？

```text
FS：
  0 CTS
  1 WAIT
  2 OVFLW

BS：
  每个块多少 CF
  0 = 不限

STmin：
  两个 CF 之间的最小间隔
```

### 15. N_Bs 和 N_Cr 是什么？

```text
N_Bs：发送方等 FC 的超时
N_Cr：接收方等 CF 的超时
```

项目：

```text
STM32 侧 N_Cr timeout
got 6/11B（0 CF, 1 FC）
```

### 16. 项目里 4095 字节怎么发的？

```text
FF 带前 6 字节
FC 指定 BS=8、STmin=5ms
每 8 个 CF 等一次 FC
```

数据：

```text
585 CF
rx_ovf=0
```

---

## 三、UDS（17–24）

### 17. 正/负响应格式？

```text
正响应：SID + 0x40 + 数据
负响应：7F + 原 SID + NRC
```

### 18. `0x10` 会话控制？

```text
请求：10 sub
正响应：50 sub
子功能：01 default / 03 extended / 02 programming
```

项目中：

```text
10 03 → 50 03
非法子功能 → 7F 10 12
```

### 19. `0x3E` 和 S3 是什么？

```text
3E 00：TesterPresent
S3：非默认会话下 5 秒无请求回默认
```

项目：

```text
UDS: S3 timeout -> default session
```

### 20. `0x22` 怎么读 DID？

```text
请求：22 DID_H DID_L
正响应：62 DID_H DID_L data...
```

项目：

```text
F190 VIN
F180 版本
F1B0 当前槽
```

### 21. `0x19` 常用子功能？

```text
19 01：按 mask 计数
19 02：按 mask 读 DTC
19 0A：读支持的 DTC
```

项目：

```text
19 02 FF → 59 02 FF + DTC 列表
```

### 22. `0x14` 怎么清 DTC？

```text
14 FF FF FF
正响应：54
```

注意：

```text
清完 fault 仍存在 → 会重新置位
```

### 23. NRC 有哪些常见值？

```text
0x11 服务不支持
0x12 子功能不支持
0x13 长度错
0x22 条件不满足
0x31 请求越界
0x72 编程失败
0x73 blockCounter 错
```

### 24. UDS 串包怎么发生？

```text
周期性 19 02 的响应 59 02 FF
被 0x34 请求误收
```

修复：

```text
发送前 drain socket
响应校验 SID
0x7F + 原 SID，或 SID+0x40
```

---

## 四、DTC（25–28）

### 25. DTC 状态位怎么理解？

```text
bit0 testFailed
bit1 testFailedThisOperationCycle
bit2 pendingDTC
bit3 confirmedDTC
bit4 testNotCompletedSinceLastClear
bit5 testFailedSinceLastClear
bit6 testNotCompletedThisOperationCycle
bit7 warningIndicatorRequested
```

项目简化为：

```text
testFailed
confirmed
pending
```

### 26. C001 是什么？

```text
DHT11 超时/校验错
```

C002：

```text
CAN bus-off
```

C004：

```text
环境越界
```

### 27. DTC 怎么掉电保存？

```text
存 Flash sector 4（0x08010000）
magic + version + 条目 + CRC32
```

### 28. `0x14` 清除后为什么又出现？

```text
故障条件仍在
Dtc_Report 再次置位
```

---

## 五、Bootloader（29–34）

### 29. Flash 分区？

```text
0x08000000  Bootloader 32K
0x08008000  Metadata A 16K
0x0800C000  Metadata B 16K
0x08010000  DTC 64K
0x08020000  App A 256K
0x08060000  App B 256K
```

### 30. App Header 有什么？

```text
magic "APP1"
version
length
crc32
build_id
reserved
```

### 31. 为什么 App 从 +0x200 开始？

```text
Header 512B
Cortex-M 向量表 0x200 对齐
```

### 32. 跳转前做了什么？

```text
关中断
关 SysTick、清 LOAD/VAL
清 pending
设 VTOR
DSB/ISB
设 MSP
开中断
跳 entry
```

### 33. 不关 SysTick 会怎样？

```text
App 启动时 BSS 还没清
SysTick 进 FreeRTOS 未初始化结构
卡在 LoopFillZerobss
```

### 34. Metadata 双记录怎么保证掉电？

```text
A/B 两个 sector
写另一份，seq++
至少一份旧记录完整
启动选有效且 seq 较新的
```

---

## 六、OTA（35–38）

### 35. `0x34` 格式？

```text
34 00 44 addr(4) size(4)
```

响应：

```text
74 20 04 00
```

### 36. `0x36` 格式？

```text
36 counter data...
```

响应：

```text
76 counter
```

### 37. `0x37` 做什么？

```text
确认 written == size
校验 Header/CRC/向量表
写 trial Metadata
返回 77
```

### 38. OTA 中断/掉电会怎样？

```text
只写非活动槽
Metadata 没改
旧槽仍可启动
```

确认后：

```text
trial → 新 App confirm → active
```

---

## 七、Linux 网关（39–40）

### 39. gatewayd 怎么工作？

```text
打开 can0
周期 19 02
SQLite 记录
MQTT 发布
Unix socket 接收 gwctl
ota_transfer 处理 0x34/36/37
```

限制：

```text
单线程
poll 不是 epoll
OTA 时周期 DTC 可能抢总线
MQTT 每次重连
```

### 40. 为什么 CAN 采样点问题在 Linux 侧改？

```text
STM32 固件固定 0.75
Linux 0.866
改 Linux 不动固件
验证最快
```

---

---

## 八、UDS 安全访问与写 DID（41–46）

### 41. 0x27 的完整流程？

```text
1. 10 03 进扩展会话
2. 27 01 请求 seed  -> 67 01 <seed 4B>
3. 上位机用算法算 key
4. 27 02 <key>       -> 67 02 解锁成功
5. 之后 0x2E 等受保护服务才能用
```

反面：

```text
没解锁就 2E     -> 7F 2E 33
没取 seed 就发 key -> 7F 27 24
key 错          -> 7F 27 35，错 3 次 -> 36，锁定期内 -> 37
```

### 42. 为什么一次 seed 只能试一次 key？

```text
否则可以对同一个 seed 暴力枚举（4 字节也要试 2^32，但如果 seed 固定就更糟）
实现上：校验失败立刻把 seed_valid 清 0，必须重新 27 01
```

追问：**seed 怎么生成？**

```text
我的实现混了 HAL_GetTick() 和 App 槽位，保证每次不同
ISO 规定 seed 全 0 表示"已解锁"，所以生成时要避开 0
```

### 43. 为什么要限制尝试次数和延迟？

```text
防暴力：3 次错 -> 锁 10 s
真实 ECU 会指数退避（10 s / 10 min / 1 h）甚至上电才能解除
面试可以补一句：这只是软件层，真正的防线是 HSM + 产线密钥
```

### 44. 种子算法怎么设计才安全？

```text
本项目的 uds_calc_key 只是演示：
  k = seed ^ 0x5A5A5A5A; rotl(k,13); ^= 0xA5A5A5A5; *2654435761; ^= 0x55AA55AA
可逆、固定，能被逆向出来

真实做法：
  产线写入每台车唯一的密钥
  seed/key 由 HSM 计算，MCU 只做搬运
  或者用 AES-CMAC(seed, secret)
```

### 45. 0x2E 为什么必须解锁？哪些 DID 可写？

```text
0x2E 改的是 ECU 的配置/标定，误写会改变行为，所以要求安全访问
本项目可写：
  F1A0 PIR 开关（1 字节）
  F1A1 温湿度上报周期（2 字节，小端）
  F1A2 心跳周期（2 字节，小端）
其余 DID（VIN/版本/序列号）只读 -> 7F 2E 31
```

追问：**写进去怎么持久化？** 当前只在 RAM，掉电丢失；量产要写 NVM 并做 CRC。

### 46. 安全等级为什么跟会话走？

```text
ISO 14229：切回默认会话、S3 超时，安全等级都要失效
否则测试端解锁一次就能一直用，安全访问形同虚设
本项目：uds_session_control 换会话时 uds_security_lock()，S3 超时也锁
```

## 九、行为/项目追问

### 47. 这个项目最难的点？

不要答“难在写代码”。

可以答：

```text
定位类问题：
SysTick 没关
VTOR 没设
采样点不一致
UDS 串包
```

每条都能讲现象、日志、根因、修复。

### 48. 哪些没做？

```text
24h/72h
50 次 OTA
多点掉电
签名验签
断点续传
```
（安全访问 0x27 / 写 DID 0x2E 已经做了，别再写"没做"）

诚实说，不要编。
