/***************************************************************
 * @file    :  telnet_server.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __TELNET_SERVER_H__
#define __TELNET_SERVER_H__

#include "os_mutex.h"
#include "lwip/tcp.h"

#define TELNETCLIDATADIV    //定义telnet客户数据分离

#define TELNETCLIENTNUM 3   //定义最大支持连个并发

#define TELNETBUFFSIZE 512  //定义收发缓存区的大小

#define TELNETSENDBUFFSIZE 1024

/**
 * @internal
 * Telnet commands, as defined by RFC854.
 */
#define TELNET_IAC              ((char) 255)
#define TELNET_WILL             ((char) 251)
#define TELNET_WONT             ((char) 252)
#define TELNET_DO               ((char) 253)
#define TELNET_DONT             ((char) 254)
#define TELNET_SE               ((char) 240)
#define TELNET_NOP              ((char) 241)
#define TELNET_DATA_MARK        ((char) 242)
#define TELNET_BREAK            ((char) 243)
#define TELNET_IP               ((char) 244)
#define TELNET_AO               ((char) 245)
#define TELNET_AYT              ((char) 246)
#define TELNET_EC               ((char) 247)
#define TELNET_EL               ((char) 248)
#define TELNET_GA               ((char) 249)
#define TELNET_SB               ((char) 250)

/**
 * @internal
 * Telnet options, as defined by RFC856-RFC861.
 */
#define TELNET_OPT_BINARY       ((char) 0)
#define TELNET_OPT_ECHO         ((char) 1)
#define TELNET_OPT_SUPPRESS_GA  ((char) 3)
#define TELNET_OPT_STATUS       ((char) 5)
#define TELNET_OPT_TIMING_MARK  ((char) 6)
#define TELNET_OPT_EXOPL        ((char) 255)

/**
 * @def OPT_FLAG_WILL
 * @brief The bit in the ucFlags member of the tTelnetOpts that is set when the remote client has sent a WILL
 * request and the server has accepted it.
 */
#define OPT_FLAG_WILL           ((uint8_t) 1)

/**
 * @def OPT_FLAG_DO
 * @brief The bit in the ucFlags member of tTelnetOpts that is set when the remote
 * client has sent a DO request and the server has accepted it.
 */
#define OPT_FLAG_DO             ((uint8_t) 2)

/**
 * @brief Telnet options state structure.
 * A structure that contains the state of the options supported by the telnet
 * server, along with the possible flags.
 */
typedef struct {
	char option; /**< The option byte. */
	char flags; /**< The flags for this option. The bits in this byte are defined by OPT_FLAG_WILL and OPT_FLAG_DO. */
} TelnetOpts_t;


/* telnet会话状态 */
typedef enum {
    TELNET_STATE_INIT,
    TELNET_STATE_AUTH,
    TELNET_STATE_ACTIVE,
    TELNET_STATE_CLOSED
} TELNET_SESSION_STATE;

typedef enum {
	STATE_NORMAL, /**< The telnet option parser is in its normal mode.  Characters are passed as is until an IAC byte is received. */
	STATE_IAC, /**< The previous character received by the telnet option parser was an IAC byte. */
	STATE_WILL, /**< The previous character sequence received by the telnet option parser was IAC WILL. */
	STATE_WONT, /**< The previous character sequence received by the telnet option parser was IAC WONT. */
	STATE_DO, /**< The previous character sequence received by the telnet option parser was was IAC DO. */
	STATE_DONT, /**< The previous character sequence received by the telnet option parser was was IAC DONT. */
} TelnetState_t;


typedef struct telnet_session {
    struct tcp_pcb *pcb;             // 当前会话的 TCP PCB
    TELNET_SESSION_STATE session_state;      // 会话状态
    TelnetState_t telnet_state;             
    char recv_buffer[TELNETBUFFSIZE];           // 输入缓冲区
    uint8 send_buffer[TELNETSENDBUFFSIZE];           // 输出缓冲区
    uint16_t recv_len;               // 接收数据长度
    uint16_t send_len;               // 发送数据长度
    os_sem_t recv_sem;      // 接收数据的信号量
    os_sem_t send_sem;      // 发送数据的信号量
    os_task_handle task_handle;        // FreeRTOS 任务句柄
    void *user_context;              // 用户上下文数据（如用户名等）
} TELNET_SESSION_T, *TELNET_SESSION_PTR;


#endif /* __TELNET_SERVER_H__ */


