/***************************************************************
 * @file    : os_log.c
 * @author  : LDY
 * @version : 1.0
 * @date    : 2026-09-06
 * @brief   : 统一模块化打印层实现。
 *
 *   模块注册表 g_log_mods[] + 过滤(模块闸 && 等级闸) +
 *   渲染复用 os_printf_api 的共享缓冲/颜色/输出链(os_print_buf_flush)。
 ***************************************************************/
#include "os_log.h"
#include "os_mutex.h"   /* sys_jiffies() -> xTaskGetTickCount */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* os_itoa 定义于 os_debug.c(非 static)，此处补声明 */
extern void os_itoa(int value, char *str, int base);

/* 模块注册表：索引即 log_mod_t。enabled 默认 1，重启后回到全开
 * (调试开关不落 flash；如需持久化可后续把表并入 DEVINFO_PARAM)。 */
log_mod_tab_t g_log_mods[LOG_MOD_MAX] = {
    [LOG_MOD_SYS]     = { "sys",     "[SYS] ",      1 },
    [LOG_MOD_DEVCFG]  = { "devcfg",  "[DEVCFG] ",   1 },
    [LOG_MOD_ESP8266] = { "esp8266", "[ESP8266] ",  1 },
    [LOG_MOD_NET]     = { "net",     "[NET] ",      1 },
    [LOG_MOD_HTTP]    = { "http",    "[HTTP] ",     1 },
};

/* 模块化日志主入口 */
void os_log(uint8_t mod, uint8_t level, const char *fmt, ...)
{
    va_list args;
    uint8_t byBuffLen = 0;
    char byJiffies[16] = {0};
    int remain;
    int n;
    const char *prefix;

    /* 非法模块索引回落到 SYS */
    if (mod >= LOG_MOD_MAX) {
        mod = LOG_MOD_SYS;
    }

    /* 闸1：模块运行时开关(整模块静默，含 ERROR/ALERT) */
    if (g_log_mods[mod].enabled == 0) {
        return;
    }

    /* 等级钳制 + 闸2：全局 debugLevel 阈值 */
    if (level > DLEVEL_TRACE) {
        level = DLEVEL_TRACE;
    }
    if (level > __printf_level__) {
        return;
    }

    memset(__print_buf__, 0, LINELENTH);
    __print_buf_len__ = 0;

    /* [tick]：与 os_printf/os_debug 一致，用 os_itoa 免去 snprintf 开销 */
    os_itoa(sys_jiffies(), (char *)byJiffies, 10);
    byBuffLen += (uint8_t)sprintf(__print_buf__ + byBuffLen, "[%s]", byJiffies);

    /* 颜色转义 */
    byBuffLen += (uint8_t)sprintf(__print_buf__ + byBuffLen, "%s",
                                  __color_output__[level]);

    /* [模块前缀]，由注册表给出 */
    prefix = g_log_mods[mod].prefix;
    if (prefix) {
        byBuffLen += (uint8_t)sprintf(__print_buf__ + byBuffLen, "%s", prefix);
    }

    /* body(带剩余空间保护，避免越界) */
    remain = (int)LINELENTH - (int)byBuffLen;
    if (remain > 8) {
        va_start(args, fmt);
        n = vsnprintf((char *)__print_buf__ + byBuffLen, (size_t)remain, fmt, args);
        va_end(args);
        if (n < 0) {
            n = 0;
        } else if (n >= remain) {
            n = remain - 1;
        }
        byBuffLen += (uint8_t)n;
    }

    /* 复位颜色 */
    if (byBuffLen + 4 < LINELENTH) {
        byBuffLen += (uint8_t)sprintf(__print_buf__ + byBuffLen, "\033[0m");
    }

    __print_buf_len__ = byBuffLen;
    os_print_buf_flush();
}
