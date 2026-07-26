#include "USART.h"

void USART1_puts(const char *strbuf, unsigned short len)
{
    if (strbuf == NULL || len == 0) {
        return;
    }
    usart_api_write((uint8_t *)strbuf, (uint32_t)len);
}
