#include "bsp_dht11.h"
#include <stddef.h>

uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef *data)
{
    if (data == NULL)
    {
        return ERROR;
    }
    data->humidity = 0;
    data->temperature = 0;
    return ERROR; /* 未接硬件时返回失败；真实驱动合入后替换本文件 */
}
