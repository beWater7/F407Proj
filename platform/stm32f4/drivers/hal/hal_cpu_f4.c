#include "hal_cpu.h"
#include "stm32f4xx.h"

void hal_cpu_irq_disable(void)
{
    __disable_irq();
}

void hal_cpu_irq_enable(void)
{
    __enable_irq();
}

void hal_cpu_reset(void)
{
    NVIC_SystemReset();
}

uint32_t hal_cpu_get_vtor(void)
{
    return SCB->VTOR;
}

uint32_t hal_cpu_get_msp(void)
{
    return __get_MSP();
}
