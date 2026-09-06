#include "USART.h"
#include "hal_uart.h"
#include "shell_src.h"
#include "shell_control.h"
#include "rx_data_queue.h"

void USART1_puts(const char *strbuf, unsigned short len)
{
    if (strbuf == NULL || len == 0) {
        return;
    }
    hal_uart_write((const uint8_t *)strbuf, (uint32_t)len);
}

void shell_init_all(void)
{
    rx_queue_init();
    shell_init("STM32F407 >", USART1_puts);
    shell_input_init(&shellx, USART1_puts);
    shell_conteol_register();
}
