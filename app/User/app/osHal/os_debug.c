#include "os_debug.h"
#include "os_mutex.h"   /* os_mutex_* / os_in_isr() / os_critical_* —— 不直接用 RTOS raw 接口 */
#include "os_task.h"    /* os_scheduler_running() */
#include "malloc.h"
#include "hal_uart.h"
#include "ustdio.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>


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
	/* 越界值会令 log_level_str[level] 读越界，打印出代码区乱码 */
	if (level > DLEVEL_TRACE) level = DLEVEL_TRACE;
	if (level < DLEVEL_ALERT) level = DLEVEL_ALERT;
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


/* ==================== 打印串行化 ==================== */
/* __print_buf__ 是所有打印(os_printf_api / os_log / os_debug)共用的全局缓冲。
 * 原先出口处只有 hal_uart_flush()，但那只是"等串口发完"的 drain，没有互斥语义：
 * 任务 A 还在 hal_uart_write() 里逐字节读缓冲时，任务 B 已经 memset 把它清零，
 * 串口上就吐出半截字符串和零字节 —— 日志出现字符级交错，例如：
 *   [2[2001][app] 001][app] rrecv: first ecv: first loop, apply loop, applypending...
 * 这里用一把互斥锁把 "取缓冲 -> 渲染 -> 冲刷" 整段串行化。 */
static os_mutex_t s_print_lock = NULL;
static int        s_print_held = 0;   /* 本上下文是否真的持锁 */

/* 进入渲染前调用。返回 0 表示本轮放弃打印，调用方必须直接返回。 */
int os_print_buf_begin(void)
{
    int in_isr = os_in_isr();

    if (s_print_lock == NULL) {
        /* 调度器未启动(启动早期)只有一个上下文，无需串行化 */
        if (!os_scheduler_running()) {
            return 1;
        }
        /* 中断里不建锁：建锁要走堆分配，非 ISR 安全 */
        if (in_isr) {
            return 0;
        }
        /* 首次在任务上下文调用时补建锁(与 esp8266_mutex_ensure 同一范式)，
         * 用临界区防止两个任务同时创建出两把锁 */
        os_critical_enter();
        if (s_print_lock == NULL) {
            os_mutex_init(s_print_lock);
        }
        os_critical_exit();
        if (s_print_lock == NULL) {
            return 1;   /* 建锁失败不能把打印彻底堵死 */
        }
    }

    if (in_isr) {
        /* 中断上下文不能阻塞等锁，拿不到就丢弃本条：
         * 丢一条日志远好过撕裂缓冲或死锁 */
        if (!os_mutex_trylock(s_print_lock)) {
            return 0;
        }
    } else {
        os_mutex_lock(s_print_lock);
    }

    s_print_held = 1;
    return 1;
}

/* 释放锁。与 os_print_buf_begin() 成对调用。 */
void os_print_buf_release(void)
{
    if (s_print_held) {
        s_print_held = 0;
        os_mutex_unlock(s_print_lock);
    }
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

    /* 取打印锁：此后任何一条 return 都必须先 os_print_buf_release() */
    if(!os_print_buf_begin())
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
        /* 已持锁，必须成对释放，否则这把锁再也回不来 */
        os_print_buf_release();
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
            byTmpLen = vsnprintf((char *)__print_buf__ + byBuffLen, (size_t)remain, format, args);
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

    os_print_buf_flush();

    return;
}

/* 把 __print_buf__[0..__print_buf_len__) 发往 telnet hook 与 UART。
 * os_printf_api / os_log(模块化打印)共用：
 *  - 先回调 telnet 客户端(如有)
 *  - 再 UART 直写(绕过 newlib 行缓冲，提示符不会卡在 stdout)
 *  - 最后 flush 等串口发完，避免与其它任务打印交错成乱码 */
void os_print_buf_flush(void)
{
    uint8 byBuffLen = __print_buf_len__;

    /* 回调函数, 发送打印内容到客户端如telnet客户端 */
    if (__print_hook) {
        __print_hook((uint8 *)__print_buf__, byBuffLen);
    }

    /* 直接写 USART，禁止再走 printf：
     * newlib 行缓冲会把 "\nSTM32F407 >" 里提示符卡在 stdout，
     * 与其它任务打印交错后，serialTerm 里就变成 STM3 / STM32F / 07 > */
    if (byBuffLen > 0) {
        hal_uart_write((uint8_t *)__print_buf__, byBuffLen);
    }

    /* 等到串口传输完成，本行日志才算真正写完。
     * 注意：这只是 drain，本身不提供互斥；排他由 os_print_buf_begin() 的锁负责 */
    hal_uart_flush();

    /* 与 os_print_buf_begin() 成对：两个调用方(os_printf_api / os_log)都经由此处出口 */
    os_print_buf_release();
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
    byTmpLen = vsnprintf((char *)byTmpBuff + byBuffLen, len, format, args);
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

    hal_uart_flush();

EXIT:
    free(byTmpBuff);
    return;
}




