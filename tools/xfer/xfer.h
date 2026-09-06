#ifndef XFER_H
#define XFER_H

#include <stdint.h>

/*
 * 传输层抽象：默认 UART（termios）。后续 USB 实现同一组回调即可，
 * 上层 YMODEM 不用改。
 */
typedef struct xfer {
    int (*open)(void *ctx, const char *path, int baud);
    void (*close)(void *ctx);
    /* 读最多 n 字节，超时 timeout_ms；返回实际字节数，0=超时，<0=错误 */
    int (*read)(void *ctx, uint8_t *buf, int n, int timeout_ms);
    int (*write)(void *ctx, const uint8_t *buf, int n);
    /* 会话中切换波特率(高速 YMODEM 握手用)。返回 0=成功，<0=不支持该速率 */
    int (*set_baud)(void *ctx, int baud);
} xfer_ops_t;

typedef struct uart_ctx {
    int fd;
} uart_ctx_t;

extern const xfer_ops_t xfer_uart_ops;

/* CH340 常见接法：DTR/RTS 脉冲 NRST。open 后调用，串口保持打开。 */
void xfer_uart_pulse_reset(void *ctx);

#endif
