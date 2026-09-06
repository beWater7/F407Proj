/* STM32F4 平台的板级基础适配实现 */
#include "hal_board.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_rtc.h"

#include "stm32f4xx.h"
#include "bsp_usart.h"
#include "bsp_systick.h"
#include "bsp_timer.h"
#include "bsp_sram.h"
#include "bsp_spi_flash.h"
#include "core_delay.h"
#include "os_debug.h"

/* 独立看门狗：LSI ~32kHz，128 分频 → 250Hz；reload 2000 → 8s 超时 */
static void board_iwdg_init(void)
{
    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {
    }
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_128);
    IWDG_SetReload(2000);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void hal_board_init(void)
{
    /* 调试串口 */
    Debug_USART_Config();

    /* RGB LED */
    hal_led_init();

    /* 按键 */
    hal_key_init();

    /* SysTick 时基 */
    SysTick_Init();

    /* TIM3（10ms 延时） */
    TIM3_init();

    /* RTC */
    hal_rtc_init();

    /* 外部 PSRAM：其它模块仍可能用 EXRAM，必须在网络/调试缓冲之前初始化 FSMC */
    FSMC_SRAM_Init();

    /* 16M SPI flash W25Q128 */
    SPI_FLASH_Init();
    {
        uint32_t id = SPI_FLASH_ReadID();
        os_printf(KERN_WARN"SPI Flash JEDEC ID=0x%06lX %s\r\n",
                  (unsigned long)id,
                  (id == sFLASH_ID) ? "OK" : "UNEXPECTED");
    }

    /* DWT 供 DHT11 等微秒延时；DHT11_Init 延后到首次读取，避免影响启动 */
    CPU_TS_TmrInit();

    /* 最后使能看门狗：前面的初始化都是线性执行，8s 窗口足够 */
    board_iwdg_init();
}

void hal_board_delay_ms(uint32_t ms)
{
    /* TIM3_sleep_10ms 的计数值以 10ms 为单位 */
    TIM3_sleep_10ms((ms + 9U) / 10U);
}

void hal_board_reboot(void)
{
    NVIC_SystemReset();
    for (;;) {
        /* 复位失败才走到这里 */
    }
}

void hal_board_get_clocks(hal_board_clocks_t *clk)
{
    RCC_ClocksTypeDef rcc;

    if (clk == NULL) {
        return;
    }
    RCC_GetClocksFreq(&rcc);
    clk->sysclk_hz = rcc.SYSCLK_Frequency;
    clk->hclk_hz   = rcc.HCLK_Frequency;
    clk->pclk1_hz  = rcc.PCLK1_Frequency;
    clk->pclk2_hz  = rcc.PCLK2_Frequency;
}

void hal_board_feed_watchdog(void)
{
    IWDG_ReloadCounter();
}
