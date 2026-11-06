/**
  ******************************************************************************
  * @file    isotp_port.c
  * @brief   ISO-TP 核心的 STM32 适配层（CAN HAL + FreeRTOS 任务）
  ******************************************************************************
  * @attention
  *
  * 数据流：
  *
  *   CAN 总线 → FIFO0 ──CAN1_RX0 中断──→ 软件环形缓冲（can.c，32 帧）
  *                                            │
  *            (IsoTpPort_Task 1ms 取空) ──────┤
  *                                            ├→ isotp_on_frame()      (0x7E0)
  *                                            └→ AppCmd_ProcessFrame() (0x221/0x222)
  *
  *   isotp_tick() ──tx_frame()──→ CAN_Send() ──→ 发送邮箱 → 总线
  *
  *   isotp_rx_ready() ──把数据拷进 s_uds_resp_buf──→ isotp_send() 原样发回 0x7E8
  *
  * 为什么 echo 要单独拷一份：
  *   isotp_send() 只保存 data 指针，多帧发送期间每个 tick 都会从这块内存取
  *   下一段 7 字节。如果直接传 isotp_rx_data()（即 rx_buf），发送途中又收到
  *   新消息就会把正在发的数据改掉；传局部数组则在发送完成前就出了作用域。
  *   所以用静态 s_uds_resp_buf，等 isotp_tx_done() 之后才认为它可以复用。
  ******************************************************************************
  */

#include "isotp_port.h"
#include "isotp.h"

#include "can.h"        /* CAN_Send() / CAN_Receive_Msg() / CAN_RxMsgTypeDef */
#include "app_cmd.h"    /* AppCmd_ProcessFrame() */
#include "uds_server.h" /* UdsServer_Handle() / UdsServer_Tick() */
#include "dtc.h"        /* Dtc_Init() / Dtc_ReportImmediate() */

#include "main.h"       /* HAL_GetTick() */

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * 调试日志（两级）
 *
 * 一级（默认开）只打"每条消息一次"的事件，开销与消息长度无关：
 *
 *   ISOTP rx: 4095B in 2987ms | ...    整条消息收齐（详细格式见下面的统计段）
 *   ISOTP tx: echo started 4095B ...   回声已交给核心
 *   ISOTP tx: 4095B echo in ...ms      回声发完
 *   ISOTP sum: ...                     累计统计
 *   ISOTP rx: N_Cr timeout ...         等续帧超时（丢 CF）
 *   ISOTP rx: SN mismatch ...          序号错乱（错序 CF）
 *   ISOTP tx: N_Bs timeout ...         等流控超时（丢 FC）
 *   ISOTP rx: invalid length ...       长度非法，丢弃
 *
 * 二级（默认关）打每一帧的 PCI，用来排查"对端到底发了什么"：
 *
 *   ISOTP rx: SF len=5 / FF len=4095 / CF seq=1 / FC fs=0 bs=0 stmin=0x00
 *   ISOTP tx: CF 13/4095               回声的续帧进度
 *
 * 为什么二级默认关：一条 4KB 消息是 586 帧，二级日志就是上千行。printf 虽然
 * 是非阻塞的（USART2 环形缓冲 + DMA），但 512 字节的队列装不下一百多行，
 * 绝大多数会被整行丢掉；更关键的是格式化本身在 1ms 的 ISO-TP 任务里做，
 * 586 次 snprintf 会挤占本该用来发 CF 的 CPU。排查完记得关回去。
 * ------------------------------------------------------------------------*/
#define ISOTP_PORT_DEBUG_PRINT   1
#define ISOTP_PORT_DEBUG_VERBOSE 0

/** 每条流控帧打一行（含 HAL 时刻和收帧位置）。默认开：验证"每 8 个 CF
 *  回一次 FC"时，把它和 Linux 侧 candump -t d 的时间戳对照，就能判断
 *  对端的停顿是等 FC，还是它自己的节奏。
 *  4095B 一条消息 74 行，UART 能承受；测试完可以改成 0。 */
#define ISOTP_PORT_DEBUG_FC      1

#if (ISOTP_PORT_DEBUG_PRINT == 1)
#include <stdio.h>
#define ISOTP_LOG(...)   printf(__VA_ARGS__)
#else
#define ISOTP_LOG(...)   do { } while (0)
#endif

#if (ISOTP_PORT_DEBUG_PRINT == 1) && (ISOTP_PORT_DEBUG_VERBOSE == 1)
#define ISOTP_VLOG(...)  printf(__VA_ARGS__)
#else
#define ISOTP_VLOG(...)  do { } while (0)
#endif

/* --------------------------------------------------------------------------
 * 私有变量
 * ------------------------------------------------------------------------*/

/** ISO-TP 上下文。rx_buf[4095] 已经包含在结构体里，约 4.2KB（.bss） */
static isotp_ctx_t s_isotp;

/** echo 专用的发送缓冲：isotp_send() 只存指针，这块内存必须一直有效到发完。
    是否可复用由 s_isotp.tx_state 判定（发送中核心会拒绝新的 isotp_send），
    所以这里不需要额外的忙标志。
    长度按 ISOTP_PORT_MAX_PAYLOAD 分配，和 core 的 rx_buf 一样大，任何一条
    合法的 ISO-TP 消息（≤4095 字节）都能原样回放 */
static uint8_t s_uds_resp_buf[ISOTP_PORT_MAX_PAYLOAD];

/** 当前广播的流控参数：接收方向回 FC 时用（bs=0 不限块长，stmin=0 无间隔）。
    运行时由 IsoTpPort_SetFlowControl() 修改，写的是同一份状态 */
static uint8_t s_flow_bs    = ISOTP_PORT_BS_DEFAULT;
static uint8_t s_flow_stmin = ISOTP_PORT_STMIN_DEFAULT;

/** 接收超时只报一次：rx_state 会一直停在 TIMEOUT 直到下一条消息 */
static uint8_t s_rx_timeout_reported;

/** 1 = 有一次 tx_frame 没能把帧交给 CAN 控制器。
 *
 *  为什么单独记：isotp.c 的 isotp_tick() 是先推进 tx_pos / tx_seq 再调用
 *  tx_frame() 的（先推进是有意的，见核心里的注释）。如果这一帧没发出去，
 *  序号已经跳到下一格，对端会看到 CF 序号不连续而中止整条消息。
 *  核心不管 tx_frame 的返回值，所以由本文件记下失败，在下个 tick 主动把
 *  这条传输干净地中止并打日志——宁可明确失败，也不要让对端收到坏序列。 */
static uint8_t s_tx_frame_failed;

/* --------------------------------------------------------------------------
 * 传输统计
 *
 * 一次完整的"收请求 + 回显"打三行，每行都是**每条消息一次**，不随帧数增长：
 *
 *   ISOTP rx: 4095B in 2987ms | 585 CF, 74 FC, CF gap 5001..5210us | 1370B/s
 *   ISOTP tx: 4095B echo in 2987ms | 1370B/s
 *   ISOTP sum: n=3 | rx 2984/2987/2990ms 1370B/s | tx 2984/2987/2990ms 1370B/s | total 17922ms 1370B/s
 *
 * 上面的数字是 BS=8 / STmin=5ms 时的期望量级：585 个 CF × 5ms ≈ 2.9s；
 * CF gap 的下界应当 ≥5000us（实测略大是因为任务的 1ms 节拍把等待量化了）。
 *
 * rx 耗时：收到 FF 起算，到最后一个 CF 收齐。tx 耗时：isotp_send 受理起算，
 * 到 tx_state 变 DONE。sum 行的 min/avg/max 是累计统计，total 是两侧累计
 * 耗时，末尾是累计吞吐。
 *
 * CF gap：相邻两帧续帧的**到达**间隔，首帧之后的第一帧不计（它和 FF 之间
 * 还夹着流控往返）。时间戳来自 can.c 的 DWT 周期计数，微秒级——用来验证
 * "STmin=5ms 时相邻 CF ≥5ms"，1ms 的 HAL tick 会被 DHT11 的临界区冻住，
 * 测不准。
 *
 * FC 计数含 FF 之后的第一帧流控：BS=8 收 4095B 应为 1 + 584/8 = 74 帧。
 * 只统计多帧消息：单帧耗时没有意义，还会把 min 拉成 0。
 * ------------------------------------------------------------------------ */
typedef struct
{
  uint32_t count;      /* 完成条数 */
  uint32_t bytes;      /* 累计字节 */
  uint32_t total_ms;   /* 累计耗时 */
  uint32_t min_ms;     /* 单条最短 */
  uint32_t max_ms;     /* 单条最长 */
} isotp_timing_t;

static isotp_timing_t s_rx_timing;   /* 接收方向：Linux → STM */
static isotp_timing_t s_tx_timing;   /* 回显方向：STM → Linux */

/* 当前这条接收消息的上下文（FF 之后有效） */
static uint8_t  s_rx_active;
static uint32_t s_rx_start_ms;
static uint32_t s_rx_cf_count;
static uint32_t s_rx_fc_count;
static uint32_t s_rx_last_cf_cyc;
static uint32_t s_rx_gap_min_us;
static uint32_t s_rx_gap_max_us;
static uint8_t  s_rx_gap_have;
static uint8_t  s_rx_last_cf_seq;
static uint8_t  s_rx_abort_reported;

/* 当前这笔回显的上下文 */
static uint8_t  s_tx_active;
static uint32_t s_tx_start_ms;
static uint16_t s_tx_len;

/* --------------------------------------------------------------------------
 * 平台回调：只有这两个函数和具体平台有关
 * ------------------------------------------------------------------------*/

/**
  * @brief  把一帧 8 字节数据交给 CAN 硬件
  * @retval 0 成功，-1 失败
  * @note   这里的"等队列"就是 Linux 侧 can_write 重试的等价物：CAN_Send()
  *         在 3 个发送邮箱都满时会一直等到有邮箱空出来，最多
  *         ISOTP_PORT_TX_TIMEOUT_MS。500kbps 下一帧约 0.27ms，3 个邮箱排空
  *         不到 1ms，5ms 留了足够余量。
  * @note   本端不会像 Linux 那样一次灌几百帧：isotp_tick() 每次调用最多发
  *         一帧，任务周期 1ms，而总线的发送能力约 3~4 帧/ms，所以邮箱不会
  *         被 ISO-TP 自己塞满。会超时的只有两种情况：总线上没有对端应答
  *         （自动重传一直占着邮箱），或者对端不回 ACK/进了 bus-off。
  * @note   失败时必须记下来：核心在调用本函数前已经推进了序号，这一帧丢了
  *         对端就会看到序号跳变。由任务在下一个 tick 中止整条传输。
  */
static int isotp_tx_frame(uint32_t id, const uint8_t *data, uint8_t dlc)
{
  /* 接收方向的本端流控帧（PCI=0x3x）：只做统计。发送方向的 FF/CF 不会出现
     0x3x，所以按内容分类不会混淆 */
  uint8_t is_fc = (uint8_t)((data != NULL) && (dlc > 0U) &&
                            ((data[0] & 0xF0U) == 0x30U));
  uint8_t ret;

  if (is_fc != 0U)
  {
    s_rx_fc_count++;
  }

  ret = CAN_Send(id, data, dlc, ISOTP_PORT_TX_TIMEOUT_MS);

#if (ISOTP_PORT_DEBUG_FC == 1)
  if (is_fc != 0U)
  {
    /* 时间戳用来和 Linux 侧 candump -t d 的到达时刻对照：本行和下一帧 CF
       的间隔如果接近 0，说明停顿在对端；如果本行本身就晚了，才是我们的问题 */
    ISOTP_LOG("ISOTP fc: fs=%u bs=%u stmin=%u at %lums pos=%u/%u %s\r\n",
              (unsigned)(data[0] & 0x0FU), (unsigned)data[1], (unsigned)data[2],
              (unsigned long)HAL_GetTick(),
              (unsigned)s_isotp.rx_pos, (unsigned)s_isotp.rx_len,
              (ret == CAN_SEND_OK) ? "ok" : "SEND-FAILED");
  }
#endif

  if (ret != CAN_SEND_OK)
  {
    s_tx_frame_failed = 1U;
    return -1;
  }
  return 0;
}

/**
  * @brief  毫秒时间基准
  * @note   HAL 时基是 TIM6（FreeRTOS 的 SysTick 归调度器），任务上下文里稳定。
  *         DHT11 读位图时会关中断约 4ms，时间会短暂停走，对 ISO-TP 的
  *         1000ms 级超时没有影响。
  */
static uint32_t isotp_now_ms(void)
{
  return HAL_GetTick();
}

/**
  * @brief  DWT 周期差换算成微秒
  * @note   无符号减法：CYCCNT 约 25.6s 回绕一次，跨回绕的差值依然正确
  *         （前提是单次间隔远小于 25.6s，帧间隔最多 N_Cr=1s）。
  */
static uint32_t isotp_cycles_to_us(uint32_t cycles)
{
  uint32_t per_us = (uint32_t)(SystemCoreClock / 1000000U);

  return (per_us == 0U) ? 0U : (cycles / per_us);
}

/** 累计一组计时：count/bytes/total_ms 累加，min/max 跟踪极值 */
static void isotp_timing_add(isotp_timing_t *t, uint32_t bytes, uint32_t ms)
{
  if (t->count == 0U)
  {
    t->min_ms = ms;
    t->max_ms = ms;
  }
  else
  {
    if (ms < t->min_ms) { t->min_ms = ms; }
    if (ms > t->max_ms) { t->max_ms = ms; }
  }

  t->count++;
  t->bytes    += bytes;
  t->total_ms += ms;
}

/* --------------------------------------------------------------------------
 * 内部流程
 * ------------------------------------------------------------------------*/

/**
  * @brief  打印一帧收到的 ISO-TP 帧的 PCI 类别（只看本端接收 ID）
  * @note   这里不做协议处理，只是把核心内部的判断结果"翻译"出来；
  *         dlc 不足的帧也照样打印，正好能看出对端发了畸形帧
  * @note   整段受 ISOTP_PORT_DEBUG_VERBOSE 控制：关掉时函数体和调用点一起
  *         被预处理器去掉，1ms 任务里不会多出一次函数调用和分支
  */
#if (ISOTP_PORT_DEBUG_VERBOSE == 1)
static void isotp_log_rx_frame(uint32_t id, const uint8_t *data, uint8_t dlc)
{
  uint8_t pci;

  if ((id != ISOTP_PORT_RX_ID) || (dlc == 0U))
  {
    return;
  }

  pci = data[0];

  switch (pci & 0xF0U)
  {
    case 0x00U:   /* 单帧 */
      ISOTP_VLOG("ISOTP rx: SF len=%u\r\n", (unsigned)(pci & 0x0FU));
      break;

    case 0x10U:   /* 首帧：12 位总长 */
      if (dlc >= 2U)
      {
        ISOTP_VLOG("ISOTP rx: FF len=%u\r\n",
                   (unsigned)((((uint16_t)(pci & 0x0FU)) << 8) | data[1]));
      }
      break;

    case 0x20U:   /* 续帧 */
      ISOTP_VLOG("ISOTP rx: CF seq=%u\r\n", (unsigned)(pci & 0x0FU));
      break;

    case 0x30U:   /* 流控帧 */
      if (dlc >= 3U)
      {
        ISOTP_VLOG("ISOTP rx: FC fs=%u bs=%u stmin=0x%02X\r\n",
                   (unsigned)(pci & 0x0FU), (unsigned)data[1], (unsigned)data[2]);
      }
      break;

    default:      /* 0x4x~0xFx：ISO 15765-2 未定义，核心会忽略 */
      ISOTP_VLOG("ISOTP rx: unknown pci=0x%02X\r\n", (unsigned)pci);
      break;
  }
}
#define ISOTP_LOG_RX_FRAME(id, data, dlc)   isotp_log_rx_frame((id), (data), (dlc))
#else
#define ISOTP_LOG_RX_FRAME(id, data, dlc)   do { } while (0)
#endif

/**
  * @brief  更新接收方向的统计（只看本端接收 ID 的 FF / CF）
  * @param[in] msg 刚取出的报文（含 DWT 时间戳）
  * @note   只做统计，不改变任何协议状态——协议判断都在 isotp.c 里。
  *         FF 视为一条新消息的开始；CF 记录序号和相邻帧的到达间隔。
  */
static void isotp_track_rx_frame(const CAN_RxMsgTypeDef *msg)
{
  uint8_t pci = msg->Data[0];

  if ((pci & 0xF0U) == 0x10U)                 /* FF：新消息开始 */
  {
    s_rx_active         = 1U;
    s_rx_start_ms       = HAL_GetTick();
    s_rx_cf_count       = 0U;
    s_rx_fc_count       = 0U;
    s_rx_gap_have       = 0U;
    s_rx_gap_min_us     = 0xFFFFFFFFU;
    s_rx_gap_max_us     = 0U;
    s_rx_abort_reported = 0U;
  }
  else if ((pci & 0xF0U) == 0x20U)            /* CF */
  {
    s_rx_last_cf_seq = (uint8_t)(pci & 0x0FU);

    if (s_rx_active == 0U)
    {
      return;                                 /* 没有 FF 的孤立 CF，不统计 */
    }

    s_rx_cf_count++;

    if (s_rx_gap_have != 0U)
    {
      uint32_t gap_us = isotp_cycles_to_us(msg->Cycles - s_rx_last_cf_cyc);

      if (gap_us < s_rx_gap_min_us) { s_rx_gap_min_us = gap_us; }
      if (gap_us > s_rx_gap_max_us) { s_rx_gap_max_us = gap_us; }
    }

    s_rx_gap_have    = 1U;
    s_rx_last_cf_cyc = msg->Cycles;
  }
}

/**
  * @brief  一条消息收齐：打一行统计（多帧）或一行简短提示（单帧）
  * @param[in] len 收到的字节数
  */
static void isotp_report_rx_done(uint16_t len)
{
  uint32_t ms;
  uint32_t avg;

  if ((s_rx_active == 0U) || (len <= 7U))
  {
    /* 单帧：没有 FF，也就没有跨帧计时；打一行就够 */
    s_rx_active = 0U;
    ISOTP_LOG("ISOTP rx: SF %uB\r\n", (unsigned)len);
    return;
  }

  s_rx_active = 0U;
  ms  = HAL_GetTick() - s_rx_start_ms;
  avg = (ms > 0U) ? (uint32_t)(((uint64_t)len * 1000U) / ms) : 0U;

  isotp_timing_add(&s_rx_timing, len, ms);

  ISOTP_LOG("ISOTP rx: %uB in %lums | %lu CF, %lu FC, CF gap %lu..%luus | %luB/s\r\n",
            (unsigned)len, (unsigned long)ms,
            (unsigned long)s_rx_cf_count, (unsigned long)s_rx_fc_count,
            (unsigned long)((s_rx_cf_count >= 2U) ? s_rx_gap_min_us : 0U),
            (unsigned long)s_rx_gap_max_us,
            (unsigned long)avg);
}

/**
  * @brief  排空接收软件环形缓冲：每帧都喂给 ISO-TP（核心按 rx_id 过滤），
  *         命令帧同时交给 app_cmd 处理
  * @note   硬件 FIFO0 由 CAN1_RX0 中断搬进缓冲（can.c），本函数是那个缓冲的
  *         唯一消费者；Report_Task 不碰接收路径（它的 AppCmd_Task() 调用
  *         已经移除），否则命令帧可能被其中一个取走、另一个永远看不到。
  * @note   每轮最多处理 ISOTP_PORT_RX_BURST 帧：突发时把时间让给别的任务，
  *         剩下的留在缓冲里下个 tick 继续。缓冲只有 32 帧，长时间积压会溢出，
  *         所以这个上限不能小于"1ms 内可能到达的帧数"（500kbps 下约 4 帧）。
  */
static void isotp_drain_rx(void)
{
  CAN_RxMsgTypeDef msg;
  uint8_t guard = 0U;

  while ((guard < ISOTP_PORT_RX_BURST) && (CAN_Receive_Msg(&msg) == 0U))
  {
    /* 统计必须在 isotp_on_frame 之前更新：FF 会重置计数，而核心在 FF 分支里
       就会回第一帧流控，顺序反了 FC 会被算到下一条消息头上 */
    if ((msg.StdId == ISOTP_PORT_RX_ID) && (msg.DLC > 0U))
    {
      isotp_track_rx_frame(&msg);
    }

    ISOTP_LOG_RX_FRAME(msg.StdId, msg.Data, (uint8_t)msg.DLC);
    isotp_on_frame(&s_isotp, msg.StdId, msg.Data, (uint8_t)msg.DLC);

    if (isotp_rx_ready(&s_isotp) != 0)
    {
      isotp_report_rx_done(isotp_rx_len(&s_isotp));
    }

    AppCmd_ProcessFrame(&msg);
    guard++;
  }
}

/**
  * @brief  收到完整 ISO-TP 消息后交给 UDS Server，并把响应发回 0x7E8
  * @note   发送忙时不做任何事：rx 状态保持 DONE，下个 tick 再试。这样既不会
  *         丢消息，也不会覆盖正在发送的 s_uds_resp_buf。
  */
static void isotp_uds_if_ready(void)
{
  uint16_t len;
  uint16_t resp_len;

  /* 上一次发送以超时 / 收到 OVFLW 收尾时，核心把 tx_state 停在 TIMEOUT 或
     ABORT，而 isotp_send() 只接受 IDLE/DONE —— 不拨回来，UDS 响应就发不出去
     （每 tick 都试、每次都 -1）。核心代码不在本文件维护，所以在这里复位 */
  if (s_isotp.tx_state == ISOTP_TX_TIMEOUT)
  {
    ISOTP_LOG("ISOTP tx: N_Bs timeout after %lums (no FC from peer), UDS response aborted at %u/%uB\r\n",
              (unsigned long)((s_tx_active != 0U) ? (HAL_GetTick() - s_tx_start_ms) : 0U),
              (unsigned)s_isotp.tx_pos, (unsigned)s_isotp.tx_len);
    s_tx_active = 0U;
    s_isotp.tx_state = ISOTP_TX_IDLE;
  }
  else if (s_isotp.tx_state == ISOTP_TX_ABORT)
  {
    ISOTP_LOG("ISOTP tx: peer FC OVFLW after %lums, UDS response aborted at %u/%uB\r\n",
              (unsigned long)((s_tx_active != 0U) ? (HAL_GetTick() - s_tx_start_ms) : 0U),
              (unsigned)s_isotp.tx_pos, (unsigned)s_isotp.tx_len);
    s_tx_active = 0U;
    s_isotp.tx_state = ISOTP_TX_IDLE;
  }

  if (isotp_rx_ready(&s_isotp) == 0)
  {
    return;
  }

  /* 上一笔发送还没收尾（等 FC / 发 CF），等它结束再回 */
  if ((s_isotp.tx_state != ISOTP_TX_IDLE) &&
      (s_isotp.tx_state != ISOTP_TX_DONE))
  {
    return;
  }

  len = isotp_rx_len(&s_isotp);
  if ((len == 0U) || (len > ISOTP_PORT_MAX_PAYLOAD))
  {
    /* 非法长度（对端发来坏 FF / 长度字段为 0 的转义格式）：丢掉，
       避免卡在这里永远发不出去 */
    ISOTP_LOG("ISOTP rx: invalid length %u, dropped\r\n", (unsigned)len);
    isotp_reset_rx(&s_isotp);
    return;
  }

  /* UDS Server 直接写入静态响应缓冲；isotp_send() 只保存指针，
     所以响应发完前不能复用这块缓冲（tx_state 会保证这一点）。 */
  resp_len = UdsServer_Handle(isotp_rx_data(&s_isotp), len,
                              s_uds_resp_buf, sizeof s_uds_resp_buf);

  /* 3E 80 这类“抑制正响应”的请求不需要回包，直接释放接收状态 */
  if (resp_len == 0U)
  {
    isotp_reset_rx(&s_isotp);
    return;
  }

  if (isotp_send(&s_isotp, s_uds_resp_buf, resp_len) == 0)
  {
    s_tx_active   = 1U;
    s_tx_start_ms = HAL_GetTick();
    s_tx_len      = resp_len;

    ISOTP_LOG("ISOTP tx: UDS response started %uB -> 0x%03lX\r\n",
              (unsigned)resp_len, (unsigned long)ISOTP_PORT_TX_ID);
    isotp_reset_rx(&s_isotp);   /* 发送已受理，接收侧可以接下一帧 */
  }
  /* isotp_send 返回 -1 时保持 rx 状态，下个 tick 重试 */
}

/**
  * @brief  观察回显是否发完，发完就打耗时/吞吐和累计统计
  * @note   每轮任务末尾调用：多帧由 isotp_tick() 推进到 DONE，单帧在
  *         isotp_send() 里同步完成，两种都在下一次调用时被看到。
  * @note   单帧（≤7B）不计入耗时统计：耗时基本是 0，只会把 min 拉成 0。
  */
static void isotp_report_tx_done(void)
{
  uint32_t ms;
  uint32_t avg;
  uint32_t n;
  uint32_t rx_avg;
  uint32_t tx_avg;
  uint32_t rx_rate;
  uint32_t tx_rate;
  uint32_t total_ms;
  uint32_t total_bytes;
  uint32_t overall;

  if ((s_tx_active == 0U) || (s_isotp.tx_state != ISOTP_TX_DONE))
  {
    return;
  }

  s_tx_active = 0U;

  if (s_tx_len <= 7U)
  {
    return;             /* 单帧，不统计 */
  }

  ms  = HAL_GetTick() - s_tx_start_ms;
  avg = (ms > 0U) ? (uint32_t)(((uint64_t)s_tx_len * 1000U) / ms) : 0U;

  isotp_timing_add(&s_tx_timing, s_tx_len, ms);

  ISOTP_LOG("ISOTP tx: %uB UDS response in %lums | %luB/s\r\n",
            (unsigned)s_tx_len, (unsigned long)ms, (unsigned long)avg);

  /* 累计统计：rx/tx 各自的最短/平均/最长 + 该方向累计吞吐，最后是两侧合计。
     n 只计多帧消息（单帧不进统计），所以 rx 和 tx 的条数在这里是相等的 */
  n           = s_tx_timing.count;
  rx_avg      = (s_rx_timing.count > 0U) ? (s_rx_timing.total_ms / s_rx_timing.count) : 0U;
  tx_avg      = (n > 0U) ? (s_tx_timing.total_ms / n) : 0U;
  rx_rate     = (s_rx_timing.total_ms > 0U)
                ? (uint32_t)(((uint64_t)s_rx_timing.bytes * 1000U) / s_rx_timing.total_ms) : 0U;
  tx_rate     = (s_tx_timing.total_ms > 0U)
                ? (uint32_t)(((uint64_t)s_tx_timing.bytes * 1000U) / s_tx_timing.total_ms) : 0U;
  total_ms    = s_rx_timing.total_ms + s_tx_timing.total_ms;
  total_bytes = s_rx_timing.bytes + s_tx_timing.bytes;
  overall     = (total_ms > 0U) ? (uint32_t)(((uint64_t)total_bytes * 1000U) / total_ms) : 0U;

  ISOTP_LOG("ISOTP sum: n=%lu | rx %lu/%lu/%lums %luB/s | tx %lu/%lu/%lums %luB/s | total %lums %luB/s | drops rx_ovf=%u tx=%u\r\n",
            (unsigned long)n,
            (unsigned long)s_rx_timing.min_ms,
            (unsigned long)rx_avg,
            (unsigned long)s_rx_timing.max_ms,
            (unsigned long)rx_rate,
            (unsigned long)s_tx_timing.min_ms,
            (unsigned long)tx_avg,
            (unsigned long)s_tx_timing.max_ms,
            (unsigned long)tx_rate,
            (unsigned long)total_ms,
            (unsigned long)overall,
            (unsigned)CAN_GetRxOverflowCount(),
            (unsigned)CAN_GetTxDropCount());
}

/* --------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------*/

void IsoTpPort_Init(void)
{
  s_flow_bs    = ISOTP_PORT_BS_DEFAULT;
  s_flow_stmin = ISOTP_PORT_STMIN_DEFAULT;
  s_rx_timeout_reported = 0U;
  s_tx_frame_failed     = 0U;

  /* 统计与"当前消息"上下文清零：Init 可能在复位后重跑，.bss 不保证是新的 */
  memset(&s_rx_timing, 0, sizeof s_rx_timing);
  memset(&s_tx_timing, 0, sizeof s_tx_timing);

  s_rx_active       = 0U;
  s_rx_start_ms     = 0U;
  s_rx_cf_count     = 0U;
  s_rx_fc_count     = 0U;
  s_rx_last_cf_cyc  = 0U;
  s_rx_gap_min_us   = 0U;
  s_rx_gap_max_us   = 0U;
  s_rx_gap_have     = 0U;
  s_rx_last_cf_seq  = 0U;
  s_rx_abort_reported = 0U;

  s_tx_active   = 0U;
  s_tx_start_ms = 0U;
  s_tx_len      = 0U;

  isotp_init(&s_isotp,
             ISOTP_PORT_TX_ID,      /* 本端发 0x7E8 */
             ISOTP_PORT_RX_ID,      /* 本端收 0x7E0 */
             ISOTP_PORT_PAD,
             ISOTP_PORT_N_BS_MS,
             ISOTP_PORT_N_CR_MS,
             s_flow_bs,
             s_flow_stmin,
             isotp_tx_frame,
             isotp_now_ms);

  UdsServer_Init();
  Dtc_Init();
}

void IsoTpPort_SetFlowControl(uint8_t bs, uint8_t stmin)
{
  /* 一个 tick 只发一帧 CF：STmin 比周期还小的话根本达不到，configure 了也
     只是自我安慰。这里拦下来按 0（能发多快发多快）处理，并留一条日志 */
  if ((stmin > 0U) && (ISOTP_PORT_TASK_PERIOD_MS >= stmin))
  {
    ISOTP_LOG("ISOTP cfg: stmin=%u <= task period %ums, forced to 0\r\n",
              (unsigned)stmin, (unsigned)ISOTP_PORT_TASK_PERIOD_MS);
    stmin = 0U;
  }

  s_flow_bs    = bs;
  s_flow_stmin = stmin;
  s_isotp.bs    = bs;
  s_isotp.stmin = stmin;
}

uint8_t IsoTpPort_GetBs(void)
{
  return s_flow_bs;
}

uint8_t IsoTpPort_GetStmin(void)
{
  return s_flow_stmin;
}

void IsoTpPort_Task(void *argument)
{
  TickType_t last_wake;

  (void)argument;

  last_wake = xTaskGetTickCount();

  for (;;)
  {
    /* 1. 先把总线上来的帧收进来：流控帧/续帧要及时处理，否则对端会超时 */
    isotp_drain_rx();

    /* 2. 超时判定 + 该发 CF 就发一帧 */
    if (s_isotp.rx_state != ISOTP_RX_TIMEOUT)
    {
      s_rx_timeout_reported = 0U;   /* 状态已离开超时，允许下次再报 */
    }

    if (isotp_tick(&s_isotp) != 0)
    {
      /* 每帧一行，4KB 消息就是 586 行：默认关掉（见文件头的日志分级） */
      ISOTP_VLOG("ISOTP tx: CF %u/%u\r\n",
                 (unsigned)s_isotp.tx_pos, (unsigned)s_isotp.tx_len);
    }

    /* 核心发帧失败时不回退序号，继续发下去对端只会看到序号跳变然后中止。
       这里主动把这条传输收尾：状态回到 IDLE，echo 通道可以接下一帧，日志里
       留一条明确的失败记录，而不是让总线上出现一条"半截消息" */
    if (s_tx_frame_failed != 0U)
    {
      s_tx_frame_failed = 0U;
      ISOTP_LOG("ISOTP tx: frame not queued (no free mailbox) at %u/%uB, tx state reset\r\n",
                (unsigned)s_isotp.tx_pos, (unsigned)s_isotp.tx_len);
      s_tx_active      = 0U;
      s_isotp.tx_state = ISOTP_TX_IDLE;
    }

    if ((s_isotp.rx_state == ISOTP_RX_TIMEOUT) && (s_rx_timeout_reported == 0U))
    {
      /* 丢 CF 的典型结果：N_Cr 到期。rx_pos/rx_len 能看出断在第几字节 */
      ISOTP_LOG("ISOTP rx: N_Cr timeout after %lums, got %u/%uB (%lu CF, %lu FC), dropped\r\n",
                (unsigned long)((s_rx_active != 0U) ? (HAL_GetTick() - s_rx_start_ms) : 0U),
                (unsigned)s_isotp.rx_pos, (unsigned)s_isotp.rx_len,
                (unsigned long)s_rx_cf_count, (unsigned long)s_rx_fc_count);
      s_rx_timeout_reported = 1U;
      s_rx_active = 0U;
    }

    if (s_isotp.rx_state == ISOTP_RX_ABORT)
    {
      if (s_rx_abort_reported == 0U)
      {
        if (s_isotp.rx_len > ISOTP_PORT_MAX_PAYLOAD)
        {
          ISOTP_LOG("ISOTP rx: bad length %u > %u, ABORT (no OVFLW)\r\n",
                    (unsigned)s_isotp.rx_len, (unsigned)ISOTP_PORT_MAX_PAYLOAD);
        }
        else
        {
          /* 错序 CF 的典型结果：核心已回 FC(OVFLW)，对端的 tx_state 转 ABORT。
             期望的序号是核心内部的 rx_seq（错序时它不推进） */
          ISOTP_LOG("ISOTP rx: SN mismatch (expected %u, got %u) at %u/%uB, FC OVFLW sent\r\n",
                    (unsigned)s_isotp.rx_seq, (unsigned)s_rx_last_cf_seq,
                    (unsigned)s_isotp.rx_pos, (unsigned)s_isotp.rx_len);
        }
        s_rx_abort_reported = 1U;
        s_rx_active = 0U;
      }
    }
    else
    {
      s_rx_abort_reported = 0U;   /* 已离开 ABORT，允许下一条再报 */
    }

    /* 3. UDS S3 会话超时检查（不打印，避免 1ms 任务刷日志） */
    UdsServer_Tick(HAL_GetTick());

    /* 3.1 CAN bus-off 立即置位 C002；恢复后清 testFailed，confirmed 留给 0x14 */
    Dtc_ReportImmediate(DTC_CODE_CAN_BUSOFF,
                        ((hcan1.Instance->ESR & CAN_ESR_BOFF) != 0U)
                        ? 1U : 0U);

    /* 状态变化后延迟 1s 写 Config Flash；正常时只读标志，几乎无开销 */
    Dtc_Task(HAL_GetTick());

    /* 4. 完整消息到了就交给 UDS Server；发送忙就留到下个 tick。
          s_uds_resp_buf 的复用由核心的 tx_state 把关，发完之前 isotp_send 会拒绝 */
    isotp_uds_if_ready();

    /* 5. UDS 响应发完就结算：单条耗时/吞吐 + 累计统计 */
    isotp_report_tx_done();

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ISOTP_PORT_TASK_PERIOD_MS));
  }
}
