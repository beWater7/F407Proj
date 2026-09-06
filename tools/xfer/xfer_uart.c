#define _DEFAULT_SOURCE
#include "xfer.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/select.h>

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return 0;   /* 0=不支持, 上层应放弃切速并维持原速率 */
    }
}

/* 在已打开且非阻塞的 fd 上应用波特率。返回 0=成功。 */
static int uart_apply_baud(int fd, int baud)
{
    struct termios tio;
    speed_t spd = baud_to_speed(baud);

    if (spd == 0) {
        return -1;
    }
    if (tcgetattr(fd, &tio) != 0) {
        return -1;
    }
    if (cfsetispeed(&tio, spd) != 0 || cfsetospeed(&tio, spd) != 0) {
        return -1;
    }
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return -1;
    }
    /* 切速瞬间采样到的乱码字节全部丢弃, 避免被当成协议字符 */
    tcflush(fd, TCIFLUSH);
    return 0;
}

static int uart_open(void *vctx, const char *path, int baud)
{
    uart_ctx_t *ctx = (uart_ctx_t *)vctx;
    struct termios tio;
    int fd;
    int m;

    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        errno = EBUSY;
        return -1;
    }
    (void)ioctl(fd, TIOCEXCL);

    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);
    {
        speed_t spd = baud_to_speed(baud);
        if (spd == 0) {
            spd = B115200;   /* 未知速率回退基线, 与原行为一致 */
        }
        cfsetispeed(&tio, spd);
        cfsetospeed(&tio, spd);
    }
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    /* 打开后固定 DTR/RTS 为置位（TIOCM 位=1）。实测本板复位电路对
     * "DTR/RTS 清 0（TIOCMBIC，CH340 引脚输出高）" 敏感：若打开即清 0，
     * 会在 open 瞬间意外复位板子，boot 横幅全部错过。置位=空闲不复位，
     * 与 pyserial 打开串口时 DTR/RTS 默认 True 的行为一致。
     * 不要 TCIFLUSH，否则会丢掉 open/复位瞬间的 boot 日志 */
    if (ioctl(fd, TIOCMGET, &m) == 0) {
        m |= (TIOCM_DTR | TIOCM_RTS);
        ioctl(fd, TIOCMSET, &m);
    }
    tcflush(fd, TCOFLUSH);
    (void)fcntl(fd, F_SETFL, 0);
    ctx->fd = fd;
    return 0;
}

static void uart_close(void *vctx)
{
    uart_ctx_t *ctx = (uart_ctx_t *)vctx;

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

static int uart_read(void *vctx, uint8_t *buf, int n, int timeout_ms)
{
    uart_ctx_t *ctx = (uart_ctx_t *)vctx;
    struct timeval tv;
    fd_set rfds;
    int r;
    int got = 0;

    while (got < n) {
        FD_ZERO(&rfds);
        FD_SET(ctx->fd, &rfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        r = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return got;
        }
        r = (int)read(ctx->fd, buf + got, (size_t)(n - got));
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return got;
        }
        got += r;
        timeout_ms = 50;
    }
    return got;
}

static int uart_write(void *vctx, const uint8_t *buf, int n)
{
    uart_ctx_t *ctx = (uart_ctx_t *)vctx;
    int off = 0;

    while (off < n) {
        int r = (int)write(ctx->fd, buf + off, (size_t)(n - off));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += r;
    }
    return n;
}

static int uart_set_baud(void *vctx, int baud)
{
    uart_ctx_t *ctx = (uart_ctx_t *)vctx;

    if (ctx->fd < 0) {
        return -1;
    }
    return uart_apply_baud(ctx->fd, baud);
}

void xfer_uart_pulse_reset(void *vctx)
{
    uart_ctx_t *ctx = (uart_ctx_t *)vctx;
    int dtr = TIOCM_DTR;
    int rts = TIOCM_RTS;
    if (ctx->fd < 0) {
        return;
    }
    /* 空闲 → 断言 250ms → 空闲。DTR/RTS 任一控制线经三极管/电容接板子 RST 都能
     * 因此产生复位脉冲。用 TIOCMBIS/BIC，避免 TIOCMSET 改掉其它 modem 位。
     * 复位后不要 flush，否则会丢掉 boot 横幅和紧随其后的 'C'。
     *
     * 时序/极性（2026-08-29 实测）：DTR 与 RTS 必须分两次 ioctl 依次操作，
     * 不能用一次 ioctl 同时清 DTR|RTS——CH340 对"同步清两位"不产生复位
     * （实测无 boot 横幅，只有 YMODEM 'C' 残留）。有效序列（pyserial 验证）：
     *   BIS DTR,RTS → 100ms → BIC DTR → BIC RTS → 250ms → BIS DTR → BIS RTS
     * 即先拉高（置位）稳定，再分先后拉低（清零）250ms 触发复位，最后恢复。 */
    ioctl(ctx->fd, TIOCMBIS, &dtr);
    ioctl(ctx->fd, TIOCMBIS, &rts);
    usleep(100 * 1000);
    ioctl(ctx->fd, TIOCMBIC, &dtr);
    ioctl(ctx->fd, TIOCMBIC, &rts);
    usleep(250 * 1000);
    ioctl(ctx->fd, TIOCMBIS, &dtr);
    ioctl(ctx->fd, TIOCMBIS, &rts);
    usleep(50 * 1000);
}

const xfer_ops_t xfer_uart_ops = {
    .open = uart_open,
    .close = uart_close,
    .read = uart_read,
    .write = uart_write,
    .set_baud = uart_set_baud,
};
