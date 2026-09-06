#ifndef _HAL_BOARD_H
#define _HAL_BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 系统时钟信息 */
typedef struct
{
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
} hal_board_clocks_t;

/* 板级硬件初始化：
 * 调试串口 / LED / 按键 / SysTick / TIM3 / RTC / 外部PSRAM / SPI Flash / 独立看门狗 */
void hal_board_init(void);

/* 毫秒级忙等延时 */
void hal_board_delay_ms(uint32_t ms);

/* 系统软复位 */
void hal_board_reboot(void);

/* 读取系统时钟频率 */
void hal_board_get_clocks(hal_board_clocks_t *clk);

/* 喂独立看门狗（空闲任务中调用，防止复位） */
void hal_board_feed_watchdog(void);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_BOARD_H */
