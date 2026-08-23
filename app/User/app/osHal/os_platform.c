#include "os_task.h"

/* 供 upgrade/post.c 等模块延时复位，实现从 http 层剥离 */
void sys_reboot_delay(uint32_t sec)
{
    os_reboot_delay_sec(sec);
}
