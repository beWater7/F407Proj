#include "stm32f4xx.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "os_debug.h"
#include "malloc.h"
#include "bsp_usart.h"
#include "ustdio.h"


#if USE_CUSTOM_PRINTK
#define debug_printf printk
#else
#define debug_printf printf
#endif

__EXRAM char __print_buf__[LINELENTH] = {0}; //保存打印内容

/* 打印内容长度 */
uint8 __print_buf_len__ = 0;

__print_callback__ __print_hook = NULL;

/*os_debug串口打印使能 */
static uint8 __printf_enable__ = 1;

/* os_debug默认打印级别, 高于此级别才能在串口输出 */
uint8 __printf_level__ = DLEVEL_REPORT;

/* 打印内容颜色 */
const char *__color_output__[] = {
    "\033[1;31;44m",  //1-加粗 31-前景色 44-背景色
    "\033[1;31m",
    "\033[1;33m",
    "\033[1;32m",
    "\033[1;37m",
    "\033[1;37m",
    "\033[0m",
};


/* 打印级别字符 */
const char *log_level_str[] = {
    "ALERT ",
    "ERROR ",
    "WARN ",
    "REPORT ",
    "INFO ",
    "TRACE ",
};


/*!
 * @brief 获取当前时间戳
 *
 * @param [in] base: 10-10进制, 16-16进制
 *
 * @retval 当前时间戳
 */
void os_itoa(int value, char *str, int base) {
    char *p = str;
    char *p1, *p2;
    int sign = value;

    // 处理负数
    if (value < 0) {
        value = -value;
    }

    // 转换整数为字符串
    do {
        *p++ = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value > 0);

    // 添加负号
    if (sign < 0) {
        *p++ = '-';
    }
    *p = '\0';

    // 反转字符串
    p1 = str;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
}


void set_print_color(int level)
{
    if (level > DLEVEL_TRACE || level < DLEVEL_ALERT) {
        debug_printf("invalid debug level!\n");
        return; // 无效日志等级
    }

    debug_printf("%s", __color_output__[level]);
}

void reset_print_color(void)
{
    /* 复位颜色 */
    debug_printf("\033[0m");
}


void setDebugLevel(uint8 level)
{
    __printf_level__ = level;
}

uint8 getDebugLevel(void)
{
    os_debug(KERN_REPORT"[%s:%d] DebugLevel:%s:%d\n",__FUNCTION__,__LINE__,log_level_str[__printf_level__],__printf_level__);
    return __printf_level__;
}


void print_redirect(__print_callback__ func, void *argc)
{
    __print_hook = func;

    return;
}


/* 实现打印等级参数可缺省的设计, format前就不能带参数 */
void os_printf_api( const char *format, ...)
{
    va_list args;
    uint8 byPrintLevel = DLEVEL_INFO;
    uint8 byBuffLen = 0;
    uint8 byTmpLen = 0;
    uint8 __color__ = 0;
    uint8 byTickFlag = 0;
    uint8 byJiffies[16] = {0};

    if(0 == __printf_enable__)
        return;

    /* 将打印等级对应前缀和颜色转移字符写入打印缓冲区buff */
    memset(__print_buf__, 0, LINELENTH);
    __print_buf_len__ = 0;


    /* 校验打印是否需要带TICK[]*/
    if(2 == format[0])
    {
        byTickFlag = 1;
        format++;
    }

    /* 写入系统启动时间  ,用自定义的itoa函数比snprintf效率高 */
    os_itoa(sys_jiffies(), (char *)byJiffies, 10);
    if(byTickFlag)
    {
        byTmpLen = sprintf(__print_buf__ + byBuffLen, "[%s]", byJiffies);
        byBuffLen += byTmpLen;
    }

    /* DEBUG模式下默认使用ERROR级别 */
    if(3 == format[0])
    {
        format++;
        byPrintLevel = DLEVEL_ERROR;
        __color__ = 1;
        goto NEXT;
    }

    /* 校验是否带打印等级 */
    if(1 == format[0])
    {
        byPrintLevel = DLEVEL_ALERT + (format[1] - '0');
        /* 打印级别最低只能为TRACE */
        if(byPrintLevel > DLEVEL_TRACE) byPrintLevel = DLEVEL_TRACE;
        __color__ = 1;
        format += 2;
    }

    /* 打印级别判断, 低于默认级别的不打印(PrintLevel越小级别越高) */
    if(__color__ && byPrintLevel > __printf_level__)
    {
        return;
    }

NEXT:
#if DEBUGBUFFWITHCOLOR
    if(__color__)
    {
        /* 打印内容颜色转义字符 */
        byBuffLen += sprintf(__print_buf__ + byBuffLen, "%s", __color_output__[byPrintLevel]);
    }
#endif

#if DEBUGWITHPROMPT
    if(__color__)
        byBuffLen += sprintf(__print_buf__ + byBuffLen, "%s", log_level_str[byPrintLevel]);
#endif

    /* 获取格式化字符串, 保存到buff（剩余空间，避免越界）
     * 注意 LINELENTH==256，不可用 uint8 表示 remain */
    {
        int remain = (int)LINELENTH - (int)byBuffLen;
        if (remain > 1) {
            va_start(args, format);
            byTmpLen = vsnprintf(__print_buf__ + byBuffLen, (size_t)remain, format, args);
            va_end(args);
            if (byTmpLen < 0) {
                byTmpLen = 0;
            } else if (byTmpLen >= remain) {
                byTmpLen = remain - 1;
            }
            byBuffLen += (uint8)byTmpLen;
        }
    }

#if DEBUGBUFFWITHCOLOR
    if(__color__ && byBuffLen + 4 < LINELENTH)
    {
        /* 写入缓存区 */
        byBuffLen += sprintf(__print_buf__ + byBuffLen, "\033[0m");
    }
#endif

    __print_buf_len__ = byBuffLen;

    /* 回调函数, 发送打印内容到客户端如telnet客户端 */
    if(__print_hook)
    {
#if DEBUGBUFFWITHCOLOR
        __print_hook((uint8 *)__print_buf__, byBuffLen);
#else
        __print_hook((uint8 *)__color_output__[byPrintLevel], strlen(__color_output__[byPrintLevel]));
        __print_hook((uint8 *)__print_buf__, byBuffLen);
        __print_hook((uint8 *)__color_output__[6], strlen(__color_output__[6]));
#endif
     }

    /* 直接写 USART，禁止再走 printf：
     * newlib 行缓冲会把 "\nSTM32F407 >" 里提示符卡在 stdout，
     * 与其它任务打印交错后，serialTerm 里就变成 STM3 / STM32F / 07 > */
    if (byBuffLen > 0) {
        usart_api_write((uint8_t *)__print_buf__, byBuffLen);
    }

    /* 等待串口传输完成, 避免乱码 */
    waitUsartSend(DEBUG_USART);

    return;
}


/* 打印内容重定向到用户指定的缓存区中 */
void os_printf_buff_redirect(uint8_t *param, uint16 len, const char *format, ...)
{
    va_list args;
    uint8 byPrintLevel = DLEVEL_INFO;
    uint8 byBuffLen = 0;
    static uint32 byParamLen = 0;
    uint8 byTmpLen = 0;
    uint8 __color__ = 0;
    uint8 byTickFlag = 0;
    uint8 byJiffies[16] = {0};
    uint8 *byTmpBuff = NULL;


    if(0 == __printf_enable__)
        return;

#if 0
    /* 传入NULL标识写入结束 */
    if(NULL == param)
    {
        //printf("%d\n", (char *)param, byParamLen);
        printf("invalid cmd!\n");
        byParamLen = 0;
        return;
    }
#endif

    /* 校验打印是否需要带TICK[]*/
    if(2 == format[0])
    {
        byTickFlag = 1;
        format++;
    }

    byTmpBuff = (uint8 *)malloc(LINELENTH);
    if(NULL == byTmpBuff)
    {
        debug_api("os_exsram_malloc\n");
        return;
    }
    memset(byTmpBuff, 0, LINELENTH);


    /* 写入系统启动时间  ,用自定义的itoa函数比snprintf效率高 */
    os_itoa(sys_jiffies(), (char *)byJiffies, 10);
    if(byTickFlag)
    {
        byTmpLen = sprintf((char *)byTmpBuff + byBuffLen, "[%s]", byJiffies);
        byBuffLen += byTmpLen;
    }

    /* DEBUG模式下默认使用ERROR级别 */
    if(3 == format[0])
    {
        format++;
        byPrintLevel = DLEVEL_ERROR;
        __color__ = 1;
        goto NEXT;
    }

    /* 校验是否带打印等级 */
    if(1 == format[0])
    {
        byPrintLevel = DLEVEL_ALERT + (format[1] - '0');
        /* 打印级别最低只能为TRACE */
        if(byPrintLevel > DLEVEL_TRACE) byPrintLevel = DLEVEL_TRACE;
        __color__ = 1;
        format += 2;
    }

    /* 打印级别判断, 低于默认级别的不打印(PrintLevel越小级别越高) */
    if(__color__ && byPrintLevel > __printf_level__)
    {
        return;
    }

NEXT:
#if DEBUGBUFFWITHCOLOR
    if(__color__)
    {
        /* 打印内容颜色转义字符 */
        byBuffLen += sprintf((char *)byTmpBuff + byBuffLen, "%s", __color_output__[byPrintLevel]);
    }
#endif

#if DEBUGWITHPROMPT
    if(__color__)
        byBuffLen += sprintf((char *)byTmpBuff + byBuffLen, "%s", log_level_str[byPrintLevel]);
#endif

    /* 获取格式化字符串, 保存到buff */
    va_start(args, format);
    byTmpLen = vsnprintf(byTmpBuff + byBuffLen, len, format, args);
    va_end(args);

    /* 更新长度 */
    byBuffLen += byTmpLen;

#if DEBUGBUFFWITHCOLOR
    if(__color__)
    {
        /* 写入缓存区 */
        byBuffLen += sprintf((char *)byTmpBuff + byBuffLen, "\033[0m");
    }
#endif

#if 0
    /* 回调函数, 发送打印内容到客户端如telnet客户端 */
    if(__print_hook)
    {
#ifdef DEBUGBUFFWITHCOLOR
        __print_hook(byTmpBuff, byBuffLen);
#else
        __print_hook((uint8 *)__color_output__[byPrintLevel], strlen(__color_output__[byPrintLevel]));
        __print_hook(byTmpBuff, byBuffLen);
        __print_hook((uint8 *)__color_output__[6], strlen(__color_output__[6]));
#endif
     }
#endif
    /* 直接return, 不打印 */
    if(0 == len)
    {
        /* 结束的时候需要将长度清0 */
        byParamLen = 0;
        goto EXIT;
    }

    /* 打印内容写入用户自定义的buff中 */
    //printf("byParamLen:%d\n", byParamLen);
    if(param)
    {
        memcpy(param + byParamLen, byTmpBuff, byBuffLen);
        byParamLen += byBuffLen;
    }
    //printf("byTmpBuff:%s\n", byTmpBuff);
    //printf("param:%s %p\n", param, param);

    /* 带等级打印：颜色已在 buffer 时一次发出，避免 CSI 拆包露码 */
    if(__color__)
    {
#if DEBUGBUFFWITHCOLOR
        debug_printf("%s", byTmpBuff);
#else
        set_print_color(byPrintLevel);
        debug_printf("%s", byTmpBuff);
        reset_print_color();
#endif
    }
    else
    {
        debug_printf("%s", byTmpBuff);
    }

    waitUsartSend(DEBUG_USART);

EXIT:
    free(byTmpBuff);
    return;
}




