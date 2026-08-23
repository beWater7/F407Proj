/***************************************************************
 * @file    :  telnet.h
 * @brief   :  Telnet 服务（协议实现见 telnet.c，头文件不暴露 RTOS/lwIP 类型）
 ***************************************************************/
#ifndef __TELNET_H__
#define __TELNET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "os_types.h"

struct tcp_pcb;

#ifndef TELNETBUFFSIZE
#define TELNETBUFFSIZE     256
#endif
#ifndef TELNETSENDBUFFSIZE
#define TELNETSENDBUFFSIZE 1024
#endif

typedef enum {
    TELNET_STATE_INIT,
    TELNET_STATE_AUTH,
    TELNET_STATE_ACTIVE,
    TELNET_STATE_CLOSED
} TELNET_SESSION_STATE;

typedef struct telnet_session {
    struct tcp_pcb *pcb;
    TELNET_SESSION_STATE state;
    char recv_buffer[TELNETBUFFSIZE];
    uint8 send_buffer[TELNETSENDBUFFSIZE];
    uint16_t recv_len;
    uint16_t send_len;
    os_sem_t recv_sem;
    os_sem_t send_sem;
    os_task_handle task_handle;
    void *user_context;
} TELNET_SESSION_T, *TELNET_SESSION_PTR;

void telnet_server_init(void);

#define TELNETCLIDATADIV
#define TELNETCLIENTNUM 3

#ifdef __cplusplus
}
#endif

#endif /* __TELNET_H__ */
