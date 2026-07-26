#include <string.h>
#include <stdio.h>
#include "typedef.h"
#include "node_tree.h"
#include "httpd.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

/*
 * node_tree.c 提供 processProtocol / sendCallback。
 * 本文件仅保留重启辅助，供 upgrade/post.c 使用。
 */
void sys_reboot_delay(uint32_t sec)
{
    if (sec == 0) {
        sec = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(sec * 1000U));
    NVIC_SystemReset();
}
