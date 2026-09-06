#ifndef _HAL_UART_H
#define _HAL_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_uart_init(void);
void hal_uart_write(const uint8_t *buf, uint32_t len);
uint32_t hal_uart_read(uint8_t *buf, uint32_t len);
int hal_uart_printf(const char *fmt, ...);
void hal_uart_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* _HAL_UART_H */
