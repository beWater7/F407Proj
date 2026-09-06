/* STM32F4 平台的按键适配实现 */
#include "hal_key.h"
#include "bsp_key.h"

void hal_key_init(void)
{
    Key_GPIO_Config();
}

int hal_key_is_pressed(hal_key_id_t id)
{
    switch (id) {
    case HAL_KEY_1:
        return Key_Scan(KEY1_GPIO_PORT, KEY1_GPIO_PIN);
    case HAL_KEY_2:
        return Key_Scan(KEY2_GPIO_PORT, KEY2_GPIO_PIN);
    default:
        return 0;
    }
}
