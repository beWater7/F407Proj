#include "hal_uart.h"
#include "bsp_usart.h"
#include <stdarg.h>
#include <stdio.h>

void hal_uart_init(void)
{
    Debug_USART_Config();
}

void hal_uart_write(const uint8_t *buf, uint32_t len)
{
    usart_api_write((uint8_t *)buf, len);
}

uint32_t hal_uart_read(uint8_t *buf, uint32_t len)
{
    return usart_api_read(buf, len);
}

int hal_uart_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        uint32_t len = (uint32_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1);
        usart_api_write((uint8_t *)buf, len);
    }
    return n;
}

void hal_uart_flush(void)
{
    waitUsartSend(DEBUG_USART);
}
