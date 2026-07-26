/***************************************************************
 * @file    : log.c
 * @brief   : 简易环形日志缓冲（SPI 分区日志未合入前的编译占位实现）
 ***************************************************************/
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define LOG_RING_SIZE  LOG_BUF_MAX_SIZE

static char s_log_ring[LOG_RING_SIZE];
static unsigned s_log_len;

void log_init(void)
{
    s_log_len = 0;
    s_log_ring[0] = '\0';
}

void writeLog(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int n;

    if (fmt == NULL)
    {
        return;
    }

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (n <= 0)
    {
        return;
    }
    if (n >= (int)sizeof(line))
    {
        n = (int)sizeof(line) - 1;
    }

    /* 追加；空间不足则丢弃旧数据，保留尾部 */
    if (s_log_len + (unsigned)n + 2 > LOG_RING_SIZE)
    {
        s_log_len = 0;
        s_log_ring[0] = '\0';
    }

    memcpy(s_log_ring + s_log_len, line, (size_t)n);
    s_log_len += (unsigned)n;
    if (s_log_len + 1 < LOG_RING_SIZE)
    {
        s_log_ring[s_log_len++] = '\n';
        s_log_ring[s_log_len] = '\0';
    }
}

int readLogAll(char *buf, int max_len)
{
    int copy_len;

    if (buf == NULL || max_len <= 0)
    {
        return 0;
    }

    copy_len = (int)s_log_len;
    if (copy_len >= max_len)
    {
        copy_len = max_len - 1;
    }
    memcpy(buf, s_log_ring, (size_t)copy_len);
    buf[copy_len] = '\0';
    return copy_len;
}
