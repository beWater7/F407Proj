/*
 * stage0/syscalls.c — stage0 的 newlib-nano 系统调用
 *
 * printf 走 _write 输出到 USART1；堆固定到 loader 区（0x20010000）
 * 与 stage0 数据区（0x2001C000）之间的空闲 SRAM，见 link.ld。
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include "bsp_debug_usart.h"

int _read(int file, char *ptr, int len) { return 0; }

int _write(int file, char *ptr, int len)
{
    int i;

    (void)file;
    for (i = 0; i < len; i++)
    {
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
void _exit(int status) { while (1); }

caddr_t _sbrk(int incr)
{
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
