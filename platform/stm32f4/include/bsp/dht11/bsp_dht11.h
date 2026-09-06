#ifndef __BSP_DHT11_H
#define __BSP_DHT11_H

#include "stm32f4xx.h"
#include "drv_dht11.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 默认 DATA=PG9（杜邦线接排针时可改）。
 * 改脚：只改下面三行宏。
 */
#ifndef DHT11_GPIO_PORT
#define DHT11_GPIO_PORT            GPIOG
#define DHT11_GPIO_CLK             RCC_AHB1Periph_GPIOG
#define DHT11_GPIO_PIN             GPIO_Pin_9
#define DHT11_GPIO_PINSOURCE       GPIO_PinSource9
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DHT11_H */
