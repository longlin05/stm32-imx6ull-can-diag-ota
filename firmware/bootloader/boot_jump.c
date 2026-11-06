#include "boot_jump.h"

#include "main.h"

typedef void (*boot_app_entry_t)(void);

void boot_jump_to(uint32_t app_base)
{
    uint32_t sp = *(volatile uint32_t *)app_base;
    uint32_t reset = *(volatile uint32_t *)(app_base + 4U);
    boot_app_entry_t entry = (boot_app_entry_t)reset;

    __disable_irq();
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
