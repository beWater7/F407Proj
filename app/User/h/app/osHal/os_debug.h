#ifndef __OS_DEBUG_H__
#define __OS_DEBUG_H__

#include "typedef.h"
typedef void(*__print_callback__)(uint8 *buff, uint8 len);

/*
"\033[<属性>;<前景色>;<背景色>m"

属性	代码
重置/默认	0
加粗	      1
下划线	      4
反色     	7

颜色    	前景色代码	背景色代码
黑色 (Black)  	30	40
红色 (Red)	    31	41
绿色 (Green)    	32	42
黄色 (Yellow)	    33	43
蓝色 (Blue)	    34	44
紫色 (Magenta)	35	45
青色 (Cyan)	    36	46
白色 (White)   	37	47

\033[0m：重置颜色，回到默认状态。


\x1B[31m：切换到红色文本。
\x1B[0m：重置为默认颜色。
不同的颜色有不同的 ANSI 码，例如：

\x1B[32m：绿色
\x1B[33m：黄色
\x1B[34m：蓝色
*/


#define LINELENTH 256
#define CRLF "\r\n"
#define CRLFCRLF "\r\n\r\n"

enum{
    DLEVEL_ALERT,
    DLEVEL_ERROR,
    DLEVEL_WARN,
    DLEVEL_REPORT,
    DLEVEL_INFO,
    DLEVEL_TRACE,
    DLEVEL_MAX
};


// 定义日志等级前缀, 3个字节
#define KERN_DEBUG  "\003"
#define KERN_TICK   "\002"
#define KERN_LEVEL  "\001"
#define KERN_ALERT   KERN_LEVEL"0"
#define KERN_ERROR   KERN_LEVEL"1"
#define KERN_WARN    KERN_LEVEL"2"
#define KERN_REPORT  KERN_LEVEL"3"
#define KERN_INFO    KERN_LEVEL"4"
#define KERN_TRACE   KERN_LEVEL"5"

/* 连接符 */
#define __CONNECT(__A, __B)              __A##__B
/* 连接两个参数的宏 */
#define CONNECT(__A, __B)                __CONNECT(__A, __B)

extern char __print_buf__[LINELENTH]; //保存打印内容
extern uint8 __print_buf_len__;

/* 各等级颜色转义序列（os_debug.c 定义，os_log 模块化打印共用） */
extern const char *__color_output__[];

/* 把 __print_buf__[0..__print_buf_len__) 输出到 telnet hook + UART。
 * os_printf_api 与 os_log() 共用，保证单条日志完整写出不拆行。
 * 出口处会释放 os_print_buf_begin() 取得的打印锁 */
void os_print_buf_flush(void);

/* 打印串行化：__print_buf__ 是全局共享缓冲，任何要渲染到它的函数
 * 都必须先 begin() 再 flush()(flush 内部 release)，
 * 否则多任务/中断并发会撕裂缓冲，串口出现字符级交错的乱码。
 * begin() 返回 0 表示本轮放弃打印(锁被中断占着等)，调用方直接 return。 */
int  os_print_buf_begin(void);
void os_print_buf_release(void);

void print_redirect(__print_callback__ func, void* argc);
void os_printf_api( const char *format, ...);
void os_printf_buff_redirect(uint8 *param, uint16 len, const char *format, ...);

uint8 getDebugLevel(void);
void setDebugLevel(uint8 level);

extern uint8 __printf_level__;
extern const char *log_level_str[];


// 默认等级宏（使用 DEBUG 等级）
/* os_printf限制了打印内容长度 */
#define os_printf(format, ...) os_printf_api(KERN_TICK format, ##__VA_ARGS__)
#define __os_printf(format, ...) os_printf_api(format, ##__VA_ARGS__)
#define os_debug_header()  os_printf("-----%s:%u-----\r\n", __FUNCTION__,__LINE__)

/* 带有函数名和行号的打印, 默认打印级别为KERN_ERROR */
#define os_debug(format, ...)  os_printf_api(KERN_TICK KERN_DEBUG "[%s:%d] "format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/* 带有函数名和行号的打印, 默认打印级别为KERN_ERROR */
#define debug_api(format, ...) printf("[%s:%d] " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define DEBUG_HEADER(format, ...);  printf("[%s:%d] \n" format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/* 开启该宏表示打印内容包含颜色转义字符 */
#define DEBUGBUFFWITHCOLOR    1
#define DEBUGWITHPROMPT       0

#define arrert_printf os_debug

#if 0
/* 断言 */
#define CUSTOM_ASSERT(F, X) \
    do { \
        if ((F)) { \
            arrert_printf("ASSERT: %s\n", #F); \
            X;     \
        } \
    } while(0)
#endif

#define CUSTOM_ASSERT(F, X)  do { if ((F)) {arrert_printf("ASSERT: %s\n", #F); X;}} while(0)

#define cmd_prompt() os_printf_api("\r\nSTM32F407 >")  /* 必须带 \\r\\n，且走 usart 直写 */
//#define cmd_prompt() printf("[mcu@board]#")

#ifndef UNUSED_ARG
#define UNUSED_ARG(x) (void)x
#endif

#endif



