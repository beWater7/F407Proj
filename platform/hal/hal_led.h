#ifndef _HAL_LED_H
#define _HAL_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 独立 LED 编号 */
typedef enum
{
    HAL_LED_1 = 0, /* 红 */
    HAL_LED_2,     /* 绿 */
    HAL_LED_3,     /* 蓝 */
    HAL_LED_NUM
} hal_led_id_t;

/* 三色 LED 混色 */
typedef enum
{
    HAL_LED_COLOR_OFF = 0,
    HAL_LED_COLOR_RED,
    HAL_LED_COLOR_GREEN,
    HAL_LED_COLOR_BLUE,
    HAL_LED_COLOR_YELLOW,
    HAL_LED_COLOR_PURPLE,
    HAL_LED_COLOR_CYAN,
    HAL_LED_COLOR_WHITE,
} hal_led_color_t;

void hal_led_init(void);
void hal_led_on(hal_led_id_t id);
void hal_led_off(hal_led_id_t id);
void hal_led_toggle(hal_led_id_t id);
void hal_led_set_color(hal_led_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_LED_H */
