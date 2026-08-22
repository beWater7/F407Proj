#ifndef __BSP_DHT11_H
#define __BSP_DHT11_H

#include "stm32f4xx.h"

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

/* 读失败阶段（便于串口排查） */
#define DHT11_ERR_PARAM            1u
#define DHT11_ERR_NO_RESP_LOW      2u  /* 等不到从机拉低应答 */
#define DHT11_ERR_NO_RESP_HIGH     3u  /* 应答低电平后等不到拉高 */
#define DHT11_ERR_NO_DATA_START    4u  /* 等不到首 bit 低电平 */
#define DHT11_ERR_BIT              5u  /* 读 bit 超时 */
#define DHT11_ERR_CHECKSUM         6u

typedef struct
{
    float humidity;      /* RH % */
    float temperature;   /* °C */
    uint8_t humi_int;
    uint8_t humi_deci;
    uint8_t temp_int;
    uint8_t temp_deci;
    uint8_t check_sum;
    uint8_t err_stage;   /* 失败时填 DHT11_ERR_*，成功为 0 */
} DHT11_Data_TypeDef;

void DHT11_Init(void);
uint8_t DHT11_PinLevel(void); /* 当前 DATA 电平 0/1 */
uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef *data);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DHT11_H */
