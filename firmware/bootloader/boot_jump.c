#include "boot_jump.h"

#include "main.h"

typedef void (*boot_app_entry_t)(void);

void boot_jump_to(uint32_t app_base)
{
    uint32_t sp = *(volatile uint32_t *)app_base;
    uint32_t reset = *(volatile uint32_t *)(app_base + 4U);
    boot_app_entry_t entry = (boot_app_entry_t)reset;

    __disable_irq();

    /* Bootloader 的 HAL_Init 会打开 SysTick。App 启动到 main 之前也会经过
       Reset_Handler/BSS 清零，此时 App 的 FreeRTOS 还没初始化；如果 SysTick
       仍然挂着，会进 App 的 SysTick_Handler，访问未初始化的任务链表。
       跳转前必须关掉并清 pending。 */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    SCB->VTOR = app_base;
    __DSB();
    __ISB();
    __set_MSP(sp);
    __enable_irq();
    entry();

    for (;;) {
        /* 不应返回 */
    }
}
