/* STM32F4 平台的 RGB LED 适配实现 */
#include "hal_led.h"
#include "bsp_led.h"

void hal_led_init(void)
{
    LED_GPIO_Config();
}

void hal_led_on(hal_led_id_t id)
{
    switch (id) {
    case HAL_LED_1: LED1_ON;   break;
    case HAL_LED_2: LED2_ON;   break;
    case HAL_LED_3: LED3_ON;   break;
    default: break;
    }
}

void hal_led_off(hal_led_id_t id)
{
    switch (id) {
    case HAL_LED_1: LED1_OFF;  break;
    case HAL_LED_2: LED2_OFF;  break;
    case HAL_LED_3: LED3_OFF;  break;
    default: break;
    }
}

void hal_led_toggle(hal_led_id_t id)
{
    switch (id) {
    case HAL_LED_1: LED1_TOGGLE;  break;
    case HAL_LED_2: LED2_TOGGLE;  break;
    case HAL_LED_3: LED3_TOGGLE;  break;
    default: break;
    }
}

void hal_led_set_color(hal_led_color_t color)
{
    switch (color) {
    case HAL_LED_COLOR_OFF:    LED_RGBOFF; break;
    case HAL_LED_COLOR_RED:    LED_RED;    break;
    case HAL_LED_COLOR_GREEN:  LED_GREEN;  break;
    case HAL_LED_COLOR_BLUE:   LED_BLUE;   break;
    case HAL_LED_COLOR_YELLOW: LED_YELLOW; break;
    case HAL_LED_COLOR_PURPLE: LED_PURPLE; break;
    case HAL_LED_COLOR_CYAN:   LED_CYAN;   break;
    case HAL_LED_COLOR_WHITE:  LED_WHITE;  break;
    default: break;
    }
}
