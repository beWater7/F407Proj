/***************************************************************
 * @file    :  storage_manage.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __TELNET_H__
#define __TELNET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/tcp.h"
#include "lwip/sys.h"

#ifndef TELNETBUFFSIZE
#define TELNETBUFFSIZE     256
#endif
#ifndef TELNETSENDBUFFSIZE
#define TELNETSENDBUFFSIZE 1024
#endif


/* telnet会话状态 */
typedef enum {
    TELNET_STATE_INIT,
    TELNET_STATE_AUTH,
    TELNET_STATE_ACTIVE,
    TELNET_STATE_CLOSED
} TELNET_SESSION_STATE;


typedef struct telnet_session {
    struct tcp_pcb *pcb;             // 当前会话的 TCP PCB
    TELNET_SESSION_STATE state;      // 会话状态
    char recv_buffer[TELNETBUFFSIZE];           // 输入缓冲区
    uint8 send_buffer[TELNETSENDBUFFSIZE];           // 输出缓冲区
    uint16_t recv_len;               // 接收数据长度
    uint16_t send_len;               // 发送数据长度
    SemaphoreHandle_t recv_sem;      // 接收数据的信号量
    SemaphoreHandle_t send_sem;      // 发送数据的信号量
    TaskHandle_t task_handle;        // FreeRTOS 任务句柄
    void *user_context;              // 用户上下文数据（如用户名等）
} TELNET_SESSION_T, *TELNET_SESSION_PTR;


void telnet_server_init(void); 


#define TELNETCLIDATADIV    //定义telnet客户数据分离
    
#define TELNETCLIENTNUM 3   //定义最大支持连个并发
    
#ifdef __cplusplus
}
#endif

#endif /* __TYPEDEF_H__ */



