#include "bsp_dht11.h"
#include "core_delay.h"
#include <stddef.h>

#define DHT11_TIMEOUT_US           200u   /* 应答/bit 边沿超时（us） */
#define DHT11_START_LOW_US         20000u /* 主机起始低电平 ≥18ms */
#define DHT11_START_RELEASE_US     30u
#define DHT11_BIT_SAMPLE_US        40u

static uint8_t s_dht11_inited;

static void dht11_clear_af(void)
{
    /* 清复用，避免曾被配成 AF 导致 GPIO 读写无效 */
    GPIO_PinAFConfig(DHT11_GPIO_PORT, DHT11_GPIO_PINSOURCE, 0);
}

static void dht11_mode_out(void)
{
    GPIO_InitTypeDef gpio;

    dht11_clear_af();
    gpio.GPIO_Pin = DHT11_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    /* 推挽拉低更稳；释放时立刻切输入，靠外部/内部上拉 */
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

static void dht11_mode_in(void)
{
    GPIO_InitTypeDef gpio;

    dht11_clear_af();
    gpio.GPIO_Pin = DHT11_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

static void dht11_pin_low(void)
{
    GPIO_ResetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

static void dht11_pin_high(void)
{
    GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

static uint8_t dht11_pin_get(void)
{
    return GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN) ? 1u : 0u;
}

/* 等引脚变成 expect；超时返回 1。用 DWT 做截止时间，避免循环计数漂移 */
static uint8_t dht11_wait_level(uint8_t expect, uint32_t timeout_us)
{
    uint32_t start = CPU_TS_TmrRd();
    uint32_t ticks = timeout_us * (GET_CPU_ClkFreq() / 1000000u);
    uint32_t now;
    uint32_t elapsed;

    if (ticks < 48u) {
        ticks = 48u;
    }

    for (;;) {
        if (dht11_pin_get() == expect) {
            return 0;
        }
        now = CPU_TS_TmrRd();
        elapsed = (now >= start) ? (now - start) : (0xFFFFFFFFu - start + now);
        if (elapsed >= ticks) {
            return 1;
        }
    }
}

void DHT11_Init(void)
{
    RCC_AHB1PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);
    dht11_mode_out();
    dht11_pin_high();
    s_dht11_inited = 1;
}

uint8_t DHT11_PinLevel(void)
{
    if (!s_dht11_inited) {
        DHT11_Init();
    }
    dht11_mode_in();
    CPU_TS_Tmr_Delay_US(5);
    return dht11_pin_get();
}

static uint8_t dht11_read_byte(void)
{
    uint8_t i;
    uint8_t byte = 0;

    for (i = 0; i < 8; i++) {
        /* 每 bit：50us 低 → 高；高 26~28=0，70=1 */
        if (dht11_wait_level(1, DHT11_TIMEOUT_US)) {
            return 0xFF;
        }
        CPU_TS_Tmr_Delay_US(DHT11_BIT_SAMPLE_US);
        byte <<= 1;
        if (dht11_pin_get()) {
            byte |= 0x01u;
            if (dht11_wait_level(0, DHT11_TIMEOUT_US)) {
                return 0xFF;
            }
        }
    }
    return byte;
}

uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_TypeDef *data)
{
    uint8_t buf[5];
    uint8_t i;
    uint8_t sum;

    if (data == NULL) {
        return ERROR;
    }

    if (!s_dht11_inited) {
        DHT11_Init();
    }

    data->humidity = 0;
    data->temperature = 0;
    data->err_stage = 0;

    /*
     * 主机起始：拉低 ≥18ms，再释放 20~40us，切输入等应答。
     * 应答起整段关中断，避免被 ETH/Tick 拉长。
     */
    dht11_mode_out();
    dht11_pin_low();
    CPU_TS_Tmr_Delay_US(DHT11_START_LOW_US);

    __disable_irq();

    dht11_pin_high();
    CPU_TS_Tmr_Delay_US(DHT11_START_RELEASE_US);
    dht11_mode_in();

    /* 从机：~80us 低 + ~80us 高 */
    if (dht11_wait_level(0, DHT11_TIMEOUT_US)) {
        data->err_stage = DHT11_ERR_NO_RESP_LOW;
        goto fail;
    }
    if (dht11_wait_level(1, DHT11_TIMEOUT_US)) {
        data->err_stage = DHT11_ERR_NO_RESP_HIGH;
        goto fail;
    }
    if (dht11_wait_level(0, DHT11_TIMEOUT_US)) {
        data->err_stage = DHT11_ERR_NO_DATA_START;
        goto fail;
    }

    for (i = 0; i < 5; i++) {
        buf[i] = dht11_read_byte();
        if (buf[i] == 0xFF) {
            data->err_stage = DHT11_ERR_BIT;
            goto fail;
        }
    }

    __enable_irq();

    dht11_mode_out();
    dht11_pin_high();

    sum = (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]);
    if (sum != buf[4]) {
        data->err_stage = DHT11_ERR_CHECKSUM;
        return ERROR;
    }

    data->humi_int = buf[0];
    data->humi_deci = buf[1];
    data->temp_int = buf[2];
    data->temp_deci = buf[3];
    data->check_sum = buf[4];
    data->humidity = (float)buf[0] + ((float)buf[1] * 0.1f);
    data->temperature = (float)buf[2] + ((float)buf[3] * 0.1f);
    return SUCCESS;

fail:
    __enable_irq();
    dht11_mode_out();
    dht11_pin_high();
    return ERROR;
}
