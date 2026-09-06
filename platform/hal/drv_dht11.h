#ifndef _DRV_DHT11_H
#define _DRV_DHT11_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DHT11_ERR_PARAM            1u
#define DHT11_ERR_NO_RESP_LOW      2u
#define DHT11_ERR_NO_RESP_HIGH     3u
#define DHT11_ERR_NO_DATA_START    4u
#define DHT11_ERR_BIT              5u
#define DHT11_ERR_CHECKSUM         6u

typedef struct
{
    float humidity;
    float temperature;
    uint8_t humi_int;
    uint8_t humi_deci;
    uint8_t temp_int;
    uint8_t temp_deci;
    uint8_t check_sum;
    uint8_t err_stage;
} DHT11_Data_TypeDef;

void DHT11_Init(void);
uint8_t DHT11_PinLevel(void);
uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef *data);

#ifdef __cplusplus
}
#endif

#endif /* _DRV_DHT11_H */
