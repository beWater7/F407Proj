#include "FreeRTOS.h"
#include "task.h"
#include "hal_uart.h"
#include <string.h>

static void hook_uart_puts(const char *s)
{
    if (s != NULL) {
        hal_uart_write((const uint8_t *)s, (uint32_t)strlen(s));
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    taskDISABLE_INTERRUPTS();
    hook_uart_puts("\r\n*** stack overflow: ");
    hook_uart_puts(pcTaskName != NULL ? pcTaskName : "?");
    hook_uart_puts("\r\n");
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    hook_uart_puts("\r\n*** malloc failed\r\n");
    for (;;) {
    }
}
