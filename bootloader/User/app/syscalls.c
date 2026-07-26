#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include "bsp_debug_usart.h"

int _read(int file, char *ptr, int len) { return 0; }

/* newlib-nano（Makefile 里 --specs=nano.specs）的 printf 走的是 _write，
 * 不会调用 bsp_debug_usart.c 里的 fputc。以前这里直接 return len，
 * 等于把 boot 的全部串口输出丢掉，看起来像“boot 没跑、直接进 APP”。 */
int _write(int file, char *ptr, int len)
{
    int i;

    (void)file;
    for (i = 0; i < len; i++)
    {
        /* 已有 \r\n 时不要再插 \r，否则变成 \r\r\n。
         * serialTerm3 会把单独的 \r 当成“清掉当前行”，命令结果刚打印就被抹掉。 */
        if (ptr[i] == '\n' && (i == 0 || ptr[i - 1] != '\r'))
        {
            USART_SendData(DEBUG_USART, '\r');
            while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);
        }
        USART_SendData(DEBUG_USART, (uint8_t)ptr[i]);
        while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET);
    }
    return len;
}
int _close(int file) { return -1; }
int _fstat(int file, struct stat *st) { st->st_mode = S_IFCHR; return 0; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _isatty(int file) { return 1; }
int _kill(int pid, int sig) { errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
void _exit(int status) { while(1); }

caddr_t _sbrk(int incr) {
    /* _heap_start / _heap_end 由链接脚本(link.ld)定义，指向外部PSRAM区域。
     * 内部SRAM只有128K，而OTA固件buffer可能申请到几百KB，
     * 必须使用容量更大的外部PSRAM作为堆，否则malloc会踩到栈/其他内存 */
    extern char _heap_start;
    extern char _heap_end;
    static char *heap_ptr = 0;
    char *prev_heap_ptr;

    if (heap_ptr == 0) heap_ptr = &_heap_start;

    prev_heap_ptr = heap_ptr;

    if (heap_ptr + incr > &_heap_end) {
        errno = ENOMEM;
        return (caddr_t) -1;
    }

    heap_ptr += incr;
    return (caddr_t) prev_heap_ptr;
}
