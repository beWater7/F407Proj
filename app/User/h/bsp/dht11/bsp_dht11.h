#ifndef __BSP_DHT11_H
#define __BSP_DHT11_H

#include "stm32f4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DHT11 驱动尚未合入工程时的最小类型定义（MQTT/测试任务用） */
typedef struct
{
    float humidity;
    float temperature;
} DHT11_Data_TypeDef;

uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef *data);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DHT11_H */
