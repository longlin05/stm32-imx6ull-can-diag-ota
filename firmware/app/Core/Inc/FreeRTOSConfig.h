/*
 * FreeRTOS Kernel V11.3.1 配置文件 —— STM32F407VGTx @168MHz（GCC / ARM_CM4F）
 *
 * 内核源码位置：Middlewares/Third_Party/FreeRTOS/Source
 * 内存管理：heap_4（可回收、可合并相邻空闲块，本工程用它）
 *
 * 几个和本工程绑定的决定，改之前先看这里：
 *   1. HAL 时基是 TIM6（stm32f4xx_hal_timebase_tim.c），SysTick 完整让给调度器，
 *      所以不需要在 HAL 和 FreeRTOS 之间搬时基。
 *   2. 凡是调 FreeRTOS 的 ...FromISR API 的中断，优先级数值必须 >= 5
 *      （configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY）。DMA1_Stream5/6 与
 *      USART2 原来都是 0，已改到 5，见 dma.c / usart.c。
 *   3. SVC_Handler / PendSV_Handler / SysTick_Handler 由文件末的宏映射交给
 *      port.c，stm32f4xx_it.c 里不能重复定义这三个函数。
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* main.h 里带出 stm32f4xx.h，SystemCoreClock 才有声明 */
#include "main.h"

/* -------------------------------------------------------------------------
 * 调度器基本行为
 * ------------------------------------------------------------------------- */
#define configUSE_PREEMPTION                         1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION      0
#define configUSE_TICKLESS_IDLE                      0
#define configCPU_CLOCK_HZ                           (SystemCoreClock)
#define configTICK_RATE_HZ                            ((TickType_t)1000)
#define configTICK_TYPE_WIDTH_IN_BITS                TICK_TYPE_WIDTH_32_BITS
#define configMAX_PRIORITIES                         7
#define configMINIMAL_STACK_SIZE                     ((uint16_t)128)
#define configMAX_TASK_NAME_LEN                      16
#define configIDLE_SHOULD_YIELD                      1
#define configUSE_TIME_SLICING                       1
#define configSTACK_DEPTH_TYPE                       uint16_t
#define configUSE_TASK_NOTIFICATIONS                 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES        1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS      5
#define configENABLE_BACKWARD_COMPATIBILITY          1

/* -------------------------------------------------------------------------
 * 内存：heap_4，20KB
 * F407 有 128KB SRAM，先给 20KB；任务建不起来先看这里和 vApplicationMallocFailedHook
 * ------------------------------------------------------------------------- */
#define configSUPPORT_DYNAMIC_ALLOCATION             1
#define configSUPPORT_STATIC_ALLOCATION              0
#define configTOTAL_HEAP_SIZE                        ((size_t)20480)
#define configAPPLICATION_ALLOCATED_HEAP             0

/* -------------------------------------------------------------------------
 * 同步原语
 * ------------------------------------------------------------------------- */
#define configUSE_MUTEXES                            1
#define configUSE_RECURSIVE_MUTEXES                  1
#define configUSE_COUNTING_SEMAPHORES                1
#define configUSE_QUEUE_SETS                         0
#define configQUEUE_REGISTRY_SIZE                    8
#define configMESSAGE_BUFFER_LENGTH_TYPE             size_t

/* -------------------------------------------------------------------------
 * 软件定时器：用 heap_4 动态创建定时器服务任务
 * ------------------------------------------------------------------------- */
#define configUSE_TIMERS                             1
#define configTIMER_TASK_PRIORITY                    (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                     10
#define configTIMER_TASK_STACK_DEPTH                 (configMINIMAL_STACK_SIZE * 2)

/* -------------------------------------------------------------------------
 * 钩子与调试
 *   configUSE_MALLOC_FAILED_HOOK 与 configCHECK_FOR_STACK_OVERFLOW 都要在 main.c 里
 *   实现对应的钩子函数，见 main.c 的 USER CODE 4 段
 * ------------------------------------------------------------------------- */
#define configUSE_IDLE_HOOK                          0
#define configUSE_TICK_HOOK                          0
#define configUSE_DAEMON_TASK_STARTUP_HOOK           0
#define configUSE_MALLOC_FAILED_HOOK                 1
#define configCHECK_FOR_STACK_OVERFLOW               2
#define configUSE_TRACE_FACILITY                     1
#define configUSE_STATS_FORMATTING_FUNCTIONS         1
#define configGENERATE_RUN_TIME_STATS                0
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES    0

/* newlib 的 printf/malloc 在多任务下共享全局状态；多个任务都用 printf 时
   把它置 1（每个任务多一份 struct _reent，约 100+ 字节/任务） */
#define configUSE_NEWLIB_REENTRANT                   0

/* -------------------------------------------------------------------------
 * 按需裁剪的 API
 * ------------------------------------------------------------------------- */
#define INCLUDE_vTaskPrioritySet                     1
#define INCLUDE_uxTaskPriorityGet                    1
#define INCLUDE_vTaskDelete                          1
#define INCLUDE_vTaskSuspend                         1
#define INCLUDE_xResumeFromISR                       1
#define INCLUDE_vTaskDelayUntil                      1
#define INCLUDE_vTaskDelay                           1
#define INCLUDE_xTaskGetSchedulerState               1
#define INCLUDE_xTaskGetCurrentTaskHandle            1
#define INCLUDE_uxTaskGetStackHighWaterMark          1
#define INCLUDE_uxTaskGetStackHighWaterMark2         1
#define INCLUDE_xTaskGetIdleTaskHandle               1
#define INCLUDE_xTaskAbortDelay                      1
#define INCLUDE_xTaskGetHandle                       1
#define INCLUDE_eTaskGetState                        1
#define INCLUDE_xSemaphoreGetMutexHolder             1
#define INCLUDE_xTimerPendFunctionCall               1
#define INCLUDE_xEventGroupSetBitFromISR             1
#define INCLUDE_xQueueGetMutexHolder                 1
#define INCLUDE_pcTaskGetTaskName                    1

/* -------------------------------------------------------------------------
 * 中断优先级
 *   Cortex-M4 的 NVIC 只用高 4 位（configPRIO_BITS = 4），所以宏要左移 (8-4) 位
 *   数值 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 的中断才能调 FromISR API
 * ------------------------------------------------------------------------- */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* 参数校验失败：关中断停住，方便调试器抓现场。
   注意别改成会继续往下跑的写法——FreeRTOS 的参数错了，继续跑只会更难查 */
#define configASSERT(x)  if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) {} }

/* -------------------------------------------------------------------------
 * 异常处理函数让位给 port.c
 *   启动文件的向量表用的是 SVC_Handler / PendSV_Handler / SysTick_Handler，
 *   这里把 port.c 的移植层函数改名过去。SVC 和 PendSV 是 naked 函数，
 *   必须这样直接映射，不能写成「在 it.c 里调用一层」——中断返回会走错栈
 * ------------------------------------------------------------------------- */
#define vPortSVCHandler                              SVC_Handler
#define xPortPendSVHandler                           PendSV_Handler
#define xPortSysTickHandler                          SysTick_Handler

/* v11 里 configUSE_16_BIT_TICKS 已被 configTICK_TYPE_WIDTH_IN_BITS 取代，
   两个都定义会直接 #error（FreeRTOS.h:64），所以这里只保留后者，别再加回来 */
#define configUSE_CO_ROUTINES                        0

#endif /* FREERTOS_CONFIG_H */
