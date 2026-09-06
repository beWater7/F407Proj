/***************************************************************
 * @file    :  telnet.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "os_debug.h"
#include "malloc.h"
#include "shell.h"
#include "lwip/sys.h"
#include "sys_arch.h"
#include "lwip/stats.h"
#include "safe_utils.h"
#include "shell_src.h"
#include "telnet_server.h"


#if 0 
/* 使用hash查找的方式减少循环，使用链表组织连接的节点, 这样做的好处是灵活，可以根据实际需要分配空间
 * 避免了一次性分配大量的连续空间, 使用hash便于多连接并发场景提升查找效率
 */
typedef struct telnet_connection {
    struct tcp_pcb *pcb;
    struct telnet_connection *prev;
    struct telnet_connection *next;
} telnet_connection_t;

telnet_connection_t *telnet_connections = NULL;

// 使用哈希表来直接存取
telnet_connection_t *connection_table[HASH_TABLE_SIZE];

// 通过标识符获取连接
telnet_connection_t* get_connection_by_id(int id) {
    return connection_table[id % HASH_TABLE_SIZE];
}

// 增加连接
void add_connection(struct tcp_pcb *new_pcb, int id) {
    telnet_connection_t *new_conn = malloc(sizeof(telnet_connection_t));
    if (new_conn != NULL) {
        new_conn->pcb = new_pcb;
        new_conn->prev = NULL;
        new_conn->next = telnet_connections;
        if (telnet_connections != NULL) {
            telnet_connections->prev = new_conn;
        }
        telnet_connections = new_conn;

        // 保存到哈希表
        connection_table[id % HASH_TABLE_SIZE] = new_conn;
    }
}

// 删除连接
void remove_connection(int id) {
    telnet_connection_t *conn = get_connection_by_id(id);
    if (conn != NULL) {
        if (conn->prev != NULL) {
            conn->prev->next = conn->next;
        } else {
            telnet_connections = conn->next;
        }
        if (conn->next != NULL) {
            conn->next->prev = conn->prev;
        }
        free(conn);
        connection_table[id % HASH_TABLE_SIZE] = NULL;  // 清除哈希表中的记录
    }
}
#endif

int next_empty_slot = 0;  // 指向telnet_pcb中下一个空闲位置

__EXRAM struct tcp_pcb *telnet_pcb[TELNETCLIENTNUM];

//__EXRAM uint8 byTelnetRecvedBuFF[LINELENTH] = {0}; //保存打印内容
extern char __print_buf__[LINELENTH]; //保存打印内容

extern __print_callback__ __print_hook;

static os_mutex_t telnet_recv_mutex;

static uint8_t byTelnetClientCount = 0; //记录最大连接数

/**
 * @internal
 * @brief The initialization sequence sent to a remote telnet client when it first connects to the telnet server.
 */
static const char TelnetInit[] = { TELNET_IAC, TELNET_DO,
TELNET_OPT_SUPPRESS_GA, TELNET_IAC, TELNET_WILL, TELNET_OPT_ECHO };

/**
 * @internal
 * @brief This telnet server will always suppress go ahead generation, regardless of this setting.
 */
static TelnetOpts_t TelnetOptions[] = { { .option = TELNET_OPT_SUPPRESS_GA,
                .flags = (0x01 << OPT_FLAG_WILL) }, { .option = TELNET_OPT_ECHO,
                .flags = (1 << OPT_FLAG_DO) } };


/* telent 协议的控制字符 
 * Telnet 协议默认包含一些控制字符
 * 如 IAC (0xFF)，表示“命令开始”， 后面会跟其他控制命令，如 DO (0xFD)、WILL (0xFB) 等。
 * FF FB 表示 WILL 命令：表示愿意开启某个选项。
 * FF FD 表示 DO 命令：要求开启某个选项。
 * 当 telnet 客户端连接时，通常会发送一组协商命令用于设置选项。
 * FF FB 1F	WILL 终端窗口大小
 * FF FB 20	WILL 远程流控制
 * FF FB 18	WILL 终端类型
 * FF FB 27	WILL 新环境选项
 * FF FD 01	DO 回显
 * FF FB 03	WILL 终端速度
 * FF FD 03	DO 终端速度
 * 
 *--------------------------------------------------------------- 
 * 本机收到的telnet控制指令如下: 
 * FF FB 1F FF FB 20 FF FB 18 FF FB 27 FF FD 01 FF FB 03 FF FD 03
 *--------------------------------------------------------------- 
 * 可以选择对其进行回复: 或者跳过忽略
 * WON'T (0xFC)：拒绝客户端的选项请求。
 * DON'T (0xFE)：要求客户端不要开启某个选项。
 */

// #define IAC     0xFF
// #define WILL    0xFB
// #define WONT    0xFC
// #define DO      0xFD
// #define DONT    0xFE


// /* 回复telnet客户端的控制指令 */
// const uint8_t telnet_response[] = {
//     0xFF, 0xFC, 0x1F,  // IAC WON'T 终端窗口大小
//     0xFF, 0xFC, 0x20,  // IAC WON'T 远程流控制
//     0xFF, 0xFC, 0x18,  // IAC WON'T 终端类型
//     0xFF, 0xFC, 0x27,  // IAC WON'T 新环境选项
//     0xFF, 0xFE, 0x01,  // IAC DON'T 回显
//     0xFF, 0xFC, 0x03   // IAC WON'T 终端速度
// };

static uint8_t byRecvedFlag = 0;

void TelnetWriteDebugMessage(TELNET_SESSION_PTR session, char *message)
{
    /* Write the data from the transmit buffer. */
    tcp_write(session->pcb, message, strlen(message), 1);
    /* Output the telnet data. */
    tcp_output(session->pcb);
}


void TelnetWrite(TELNET_SESSION_PTR session)
{
    /* Write the data from the transmit buffer. */
    tcp_write(session->pcb, session->send_buffer, session->send_len, 1);
    /* Output the telnet data. */
    tcp_output(session->pcb);
    // memset(session->send_buffer, 0, sizeof(session->send_buffer));
    // session->send_len = 0;
}


int add_connection(struct tcp_pcb *new_pcb) 
{
    /* 连接数计数 */
    byTelnetClientCount++;
    if (next_empty_slot < TELNETCLIENTNUM) 
    {
        telnet_pcb[next_empty_slot] = new_pcb;
        next_empty_slot++;
        return 0;
    }
    return -1;  // 空间不足
}


int remove_connection(struct tcp_pcb *pcb) 
{
    int i, j = 0;
    byTelnetClientCount--;
    for (i = 0; i < TELNETCLIENTNUM; i++) 
    {
        if (telnet_pcb[i] == pcb) 
        {
            telnet_pcb[i] = NULL;
            // 将后续连接前移，避免中间出现空洞
            for (j = i; j < TELNETCLIENTNUM - 1; j++) 
            {
                telnet_pcb[j] = telnet_pcb[j + 1];
            }
            telnet_pcb[TELNETCLIENTNUM - 1] = NULL;
            /* 插入位置就是当前用户数 */
            next_empty_slot = byTelnetClientCount;
            if (byTelnetClientCount == 0) {
                print_redirect(NULL, NULL);
            }
            return 0;
        }
    }
    return -1;  // 未找到连接
}


/**
 * This function will handle a WILL request for a telnet option.  If it is an
 * option that is known by the telnet server, a DO response will be generated
 * if the option is not already enabled.  For unknown options, a DONT response
 * will always be generated.
 *
 * The response (if any) is written into the telnet transmit buffer.
 *
 * @param option char Option for the WILL command.
 * @retval none
 */
void TelnetProcessWill(TELNET_SESSION_PTR telnet_server, char option) {
    unsigned long ulIdx;
#ifdef TELNET_CHAR_DEBUG
    LOGD(PRINT_TAG, "[Telnet Server] Processing WILL command with option: %c/0x%02X\n\r", option, option);
#endif
    /* Loop through the known options. */
    for (ulIdx = 0; ulIdx < (sizeof(TelnetOptions) / sizeof(TelnetOptions[0])); ulIdx++) {
        /* See if this option matches the option in question. */
        if (TelnetOptions[ulIdx].option == option) {
            /* See if the WILL flag for this option has already been set. */
            if (((TelnetOptions[ulIdx].flags >> OPT_FLAG_WILL) & 0x01) == 0) {
                /* Set the WILL flag for this option. */
                TelnetOptions[ulIdx].flags = (TelnetOptions[ulIdx].flags & 0xFD)
                                              | (0x01 << OPT_FLAG_WILL);
                /* Send a DO response to this option. */
                telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
                telnet_server->send_buffer[telnet_server->send_len++] = TELNET_DO;
                telnet_server->send_buffer[telnet_server->send_len++] = option;
            }
            /* Return without any further processing. */
            return;
        }
    }

    /* This option is not recognized, so send a DONT response. */
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_DONT;
    telnet_server->send_buffer[telnet_server->send_len++] = option;
}

/**
 * This function will handle a WONT request for a telnet option.  If it is an
 * option that is known by the telnet server, a DONT response will be generated
 * if the option is not already disabled.  For unknown options, a DONT response
 * will always be generated.
 *
 * The response (if any) is written into the telnet transmit buffer.
 *
 * @param server Pointer to the server to process.
 * @param option char Option for the WONT command.
 * @retval none
 */
void TelnetProcessWont(TELNET_SESSION_PTR telnet_server, char option) {
    unsigned long ulIdx;
#ifdef TELNET_CHAR_DEBUG
    LOGD(PRINT_TAG, "[Telnet Server] Processing WONT command with option: %c/0x%02X\n\r", option, option);
#endif
    /* Loop through the known options. */
    for (ulIdx = 0; ulIdx < (sizeof(TelnetOptions) / sizeof(TelnetOptions[0])); ulIdx++) {
        /* See if this option matches the option in question. */
        if (TelnetOptions[ulIdx].option == option) {
            /* See if the WILL flag for this option is currently set. */
            if (((TelnetOptions[ulIdx].flags >> OPT_FLAG_WILL) & 0x01) == 1) {
                /* Clear the WILL flag for this option. */
                TelnetOptions[ulIdx].flags = (TelnetOptions[ulIdx].flags & 0xFD)
                                              | 0x00;
                /* Send a DONT response to this option. */
                telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
                telnet_server->send_buffer[telnet_server->send_len++] = TELNET_DONT;
                telnet_server->send_buffer[telnet_server->send_len++] = option;
            }
            /* Return without any further processing. */
            return;
        }
    }

    /* This option is not recognized, so send a DONT response. */
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_DONT;
    telnet_server->send_buffer[telnet_server->send_len++] = option;
}


/**
 * This function will handle a DO request for a telnet option.  If it is an
 * option that is known by the telnet server, a WILL response will be generated
 * if the option is not already enabled.  For unknown options, a WONT response
 * will always be generated.
 *
 * The response (if any) is written into the telnet transmit buffer.
 *
 * @param option char Option for the DO command.
 * @return none
 */
void TelnetProcessDo(TELNET_SESSION_PTR telnet_server, char option) {
    unsigned long ulIdx;
#ifdef TELNET_CHAR_DEBUG
    LOGD(PRINT_TAG, "[Telnet Server] Processing DO command with option: %c/0x%02X\n\r", option, option);
#endif
    /* Loop through the known options. */
    for (ulIdx = 0; ulIdx < (sizeof(TelnetOptions) / sizeof(TelnetOptions[0])); ulIdx++) {
        /* See if this option matches the option in question. */
        if (TelnetOptions[ulIdx].option == option) {
            /* See if the DO flag for this option has already been set. */
            if (((TelnetOptions[ulIdx].flags >> OPT_FLAG_DO) & 0x01) == 0) {
           	    /* Set the DO flag for this option. */
           	    TelnetOptions[ulIdx].flags = (TelnetOptions[ulIdx].flags & 0xFB)
                                              | (0x01 << OPT_FLAG_DO);
           	    /* Send a WILL response to this option. */
               	telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
           	    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_WILL;
               	telnet_server->send_buffer[telnet_server->send_len++] = option;
            }
            /* Return without any further processing. */
            return;
        }
    }

    // This option is not recognized, so send a WONT response.
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_WONT;
    telnet_server->send_buffer[telnet_server->send_len++] = option;
}

/**
 * This funciton will handle a DONT request for a telnet option.  If it is an
 * option that is known by the telnet server, a WONT response will be generated
 * if the option is not already disabled.  For unknown options, a WONT resopnse
 * will always be generated.
 *
 * The response (if any) is written into the telnet transmit buffer.
 *
 * @param option char Option for the DONT command.
 * @return none
 */
void TelnetProcessDont(TELNET_SESSION_PTR telnet_server, char option) {
    unsigned long ulIdx;
#ifdef TELNET_CHAR_DEBUG
    LOGD(PRINT_TAG, "[Telnet Server] Processing DONT command with option: %c/0x%02X\n\r", option, option);
#endif
    /* Loop through the known options. */
    for (ulIdx = 0; ulIdx < (sizeof(TelnetOptions) / sizeof(TelnetOptions[0])); ulIdx++) {
        /* See if this option matches the option in question. */
        if (TelnetOptions[ulIdx].option == option) {
            /* See if the DO flag for this option is currently set. */
            if (((TelnetOptions[ulIdx].flags >> OPT_FLAG_DO) & 0x01) == 1) {
                /* Clear the DO flag for this option. */
           	    TelnetOptions[ulIdx].flags = (TelnetOptions[ulIdx].flags & 0xFB)
                                              | 0x00;
       	        /* Send a WONT response to this option. */
           	    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
           	    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_WONT;
       	        telnet_server->send_buffer[telnet_server->send_len++] = option;
           	}
       	    /* Return without any further processing. */
       	    return;
        }
    }

    /* This option is not recognized, so send a WONT response. */
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_IAC;
    telnet_server->send_buffer[telnet_server->send_len++] = TELNET_WONT;
    telnet_server->send_buffer[telnet_server->send_len++] = option;
}


/*
 * This function processes a character received from the telnet port, handling
 * the interpretation of telnet commands (as indicated by the telnet interpret
 * as command (IAC) byte).
 *
 * @param character char The character to process.
 * @retval none
 */
void TelnetProcessCharacter(TELNET_SESSION_PTR telnet_server, char character) {
#ifdef TELNET_CHAR_DEBUG
    LOGD(PRINT_TAG, "[Telnet Server] Processing Character: %c/0x%02X\n\r", character, character);
#endif
    /* Determine the current state of the telnet command parser. */
    switch (telnet_server->telnet_state) {
    /* The normal state of the parser, were each character is either sent
     to the UART or is a telnet IAC character. */
    case STATE_NORMAL: {
        /* See if this character is the IAC character. */
        if (character == TELNET_IAC) {
            /* Skip this character and go to the IAC state. */
            telnet_server->telnet_state = STATE_IAC;
        } else {
            // /* Write this character to the receive buffer. */
            // TelnetRecvBufferWrite(character);
            // /* Echo this character */
            // TelnetWrite(character);                    //Echo the character back
            //os_sem_give(telnet_server->recv_sem);
        }
        break;
    }
    /* The previous character was the IAC character. */
    case STATE_IAC: {
        /* Determine how to interpret this character. */
        switch (character) {
        /* See if this character is also an IAC character. */
        case TELNET_IAC: {
            /* Write 0xff to the receive buffer. */
            //TelnetRecvBufferWrite(0xff);
            /* Switch back to normal mode. */
            telnet_server->telnet_state = STATE_NORMAL;
            /* This character has been handled. */
            break;
        }
        /* See if this character is the WILL request. */
        case TELNET_WILL: {
            /* Switch to the WILL mode; the next character will have
             the option in question. */
            telnet_server->telnet_state = STATE_WILL;
            /* This character has been handled. */
            break;
        }
        /* See if this character is the WONT request. */
        case TELNET_WONT: {
            /* Switch to the WONT mode; the next character will have
             the option in question. */
            telnet_server->telnet_state = STATE_WONT;
            /* This character has been handled. */
            break;
        }
    	/* See if this character is the DO request. */
        case TELNET_DO: {
       	    /* Switch to the DO mode; the next character will have the
       	      option in question. */
       	    telnet_server->telnet_state = STATE_DO;
       	    /* This character has been handled. */
            break;
        }
       	/* See if this character is the DONT request. */
        case TELNET_DONT: {
       	    /* Switch to the DONT mode; the next character will have
       	     the option in question. */
            telnet_server->telnet_state = STATE_DONT;
       	    /* This character has been handled. */
            break;
        }
        /* See if this character is the AYT request. */
        case TELNET_AYT: {
            /* Send a short string back to the client so that it knows
             that the server is still alive. */
            telnet_server->send_buffer[telnet_server->send_len++] = '\r';
            telnet_server->send_buffer[telnet_server->send_len++] = '\n';
            telnet_server->send_buffer[telnet_server->send_len++] = '[';
            telnet_server->send_buffer[telnet_server->send_len++] = 'Y';
            telnet_server->send_buffer[telnet_server->send_len++] = 'e';
            telnet_server->send_buffer[telnet_server->send_len++] = 's';
            telnet_server->send_buffer[telnet_server->send_len++] = ']';
            telnet_server->send_buffer[telnet_server->send_len++] = '\r';
            telnet_server->send_buffer[telnet_server->send_len++] = '\n';
            /* Switch back to normal mode. */
            telnet_server->telnet_state = STATE_NORMAL;
            /* This character has been handled. */
            break;
        }
        /* Explicitly ignore the GA and NOP request, plus provide a
         catch-all ignore for unrecognized requests. */
        case TELNET_GA:
        case TELNET_NOP:
        default: {
            /* Switch back to normal mode. */
            telnet_server->telnet_state = STATE_NORMAL;
            /* This character has been handled. */
            break;
        }
        }
        /* This state has been handled. */
        break;
	}
    /* The previous character sequence was IAC WILL. */
    case STATE_WILL: {
        /* Process the WILL request on this option. */
        TelnetProcessWill(telnet_server, character);
        /* Switch back to normal mode. */
        telnet_server->telnet_state = STATE_NORMAL;
        /* This state has been handled. */
        break;
    }
    /* The previous character sequence was IAC WONT. */
    case STATE_WONT: {
        /* Process the WONT request on this option. */
        TelnetProcessWont(telnet_server, character);
        /* Switch back to normal mode. */
        telnet_server->telnet_state = STATE_NORMAL;
        /* This state has been handled. */
        break;
    }
    /* The previous character sequence was IAC DO. */
    case STATE_DO: {
        /* Process the DO request on this option. */
        TelnetProcessDo(telnet_server, character);
        /* Switch back to normal mode. */
        telnet_server->telnet_state = STATE_NORMAL;
        /* This state has been handled. */
        break;
    }
    /* The previous character sequence was IAC DONT. */
    case STATE_DONT: {
        /* Process the DONT request on this option. */
        TelnetProcessDont(telnet_server, character);
        /* Switch back to normal mode. */
        telnet_server->telnet_state = STATE_NORMAL;
        /* This state has been handled. */
        break;
    }
    /* A catch-all for unknown states.  This should never be reached, but
      is provided just in case it is ever needed. */
    default: {
        /* Switch back to normal mode. */
        telnet_server->telnet_state = STATE_NORMAL;
        /* This state has been handled. */
        break;
    }
    }
}


static void telent_send_task(uint8 *buff, uint8 len)
{
    uint8_t i = 0;
    err_t err = 0;

    if (buff == NULL || len == 0) {
        return;
    }

    os_mutex_lock(telnet_recv_mutex, OS_WAIT_FOREVER);
    for (i = 0; i < TELNETCLIENTNUM; i++) {
        /* 仅向 TCP 已 ESTABLISHED 的连接回传；否则 tcp_write 返回 -11(ERR_WOULDBLOCK) 刷屏 */
        if (telnet_pcb[i] == NULL || telnet_pcb[i]->state != ESTABLISHED) {
            continue;
        }

        err = tcp_write(telnet_pcb[i], buff, len, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            /* 发送失败时静默丢弃，避免 printf 递归刷屏 */
            continue;
        }
        (void)tcp_output(telnet_pcb[i]);
    }
    os_mutex_unlock(telnet_recv_mutex);
}


static err_t telnet_recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) 
{
    os_task_handle handle = NULL;
    BaseType_t xReturn = pdPASS;
    struct pbuf *pTmp = NULL;
    uint16 wTotalLen = 0; //记录pbuf保存的数据长度
    TELNET_SESSION_PTR session = (TELNET_SESSION_PTR)arg;

    if(NULL == p)
    {
        printf(" close connect !!!\n");
        /* 更新会话状态 */
        session->session_state = TELNET_STATE_CLOSED;
        return ERR_CLSD;
    }
    wTotalLen = p->tot_len;

    pTmp = p;

    for(; p != NULL; p = p->next)
    {
        if (p->payload && (session->recv_len + p->len < sizeof(session->recv_buffer)))
        {
            MEMCPY(session->recv_buffer + session->recv_len, p->payload, p->len);
            session->recv_len += p->len;
        }
        else
        {
            printf("Buffer overflow detected!\n");
            session->recv_len = 0;
        }
    }

    /* 更新tcp窗口 */
    tcp_recved(pcb, wTotalLen);
    /* 释放pbuf节点，否则会内存泄露 */
    pbuf_free(pTmp);
    //#ifdef TELNET_DEBUG
    printf("telnet recved: %s  %d\n", session->recv_buffer, session->recv_len);
    for (uint32_t i = 0; i < session->recv_len; i++) 
    {
        printf("%02X ", session->recv_buffer[i]);
    }
    //#endif
    os_sem_give(session->recv_sem);

    return ERR_OK;
}

static void telnet_err_callback(void *arg, err_t err) 
{
    LWIP_UNUSED_ARG(err);
    #ifdef TELNET_DEBUG
    os_debug("[Telnet Server] Telnet error received: %i\n\r", err);
    #endif
    TELNET_SESSION_PTR server = NULL;
    server = (TELNET_SESSION_PTR)arg;
    remove_connection(server->pcb);
    if (server != NULL) 
    {
        /* free es structure */
        os_free(server);
    }
    return;
}


void TelnetClose(void) 
{
    uint8 i = 0;

    os_printf("Closing telnet connection.\r\n");

    for(i = 0; i < TELNETCLIENTNUM; i++)
    {
        /* Remove all callbacks */
        tcp_arg(telnet_pcb[i], NULL);
        tcp_sent(telnet_pcb[i], NULL);
        tcp_recv(telnet_pcb[i], NULL);
        tcp_err(telnet_pcb[i], NULL);
        tcp_poll(telnet_pcb[i], NULL, 0);
        /* Clear the telnet data structure pointer, to indicate that there is no longer a connection. */
        /* Close tcp connection */
        tcp_close(telnet_pcb[i]);
    }
    os_mutex_destroy(telnet_recv_mutex);
    /* Re-initialize the Telnet Server */
    //InitializeTelnetServer();
}

void telnet_session_task(void *arg) 
{
    uint32 i = 0;
    TELNET_SESSION_PTR session = (TELNET_SESSION_PTR)arg;
    while (session->session_state != TELNET_STATE_CLOSED) 
    {
        /* 以下这个信号量的 */
        //if (xSemaphoreTake(session->recv_sem, pdMS_TO_TICKS(100)) == pdTRUE)
        if(os_sem_take(session->recv_sem, OS_MS(100)) == pdTRUE)
        {
            for(i = 0; i < session->recv_len; i++)
            {
                TelnetProcessCharacter(session, session->recv_buffer[i]);
            }
#ifdef TELNETCLIDATADIV
            /* 数据分别发给telnet 客户端 */
            if(strlen(session->send_buffer))
            {
                tcp_write(session->pcb, session->send_buffer, strlen(session->send_buffer), TCP_WRITE_FLAG_COPY);
                tcp_output(session->pcb);
            }
            memset(session->send_buffer, 0, sizeof(session->send_buffer));
            #if 0
            session->send_len = snprintf(session->send_buffer, sizeof(session->send_buffer), "telnet Received: %s", session->recv_buffer);
            tcp_write(session->pcb, session->send_buffer, session->send_len, TCP_WRITE_FLAG_COPY);
            #endif
            //shell_exec_cmdlist(session->recv_buffer, session->send_buffer);
            shell_input(&shellx ,session->recv_buffer ,strlen(session->recv_buffer));
            printf("session->send_buffer:%s  :%p\n", session->send_buffer, session->send_buffer);
#else
            // 处理接收到的数据
            //os_printf("telnet Received: %s\n", session->recv_buffer);
            /* 同步处理指令, 如果指令执行时间长可考虑异步处理方式, 指令进入对队列，
             * 单独起一个任务处理指令
             */
            shell_exec_cmdlist(session->recv_buffer, NULL);
            shell_input(&shellx ,session->recv_buffer ,strlen(session->recv_buffer));
#endif
            /* 清空recvbuffer */
            memset(session->recv_buffer, 0, sizeof(session->recv_buffer));
            session->recv_len = 0;
        }
        os_sleep_ms(50);
    }

    printf("[Telnet Server] Telnet connection closed.\n\r");
    /* 回收资源 */
    tcp_close(session->pcb);

    /* 释放连接 */
    remove_connection(session->pcb);

    /* 信号量回收 */

    os_sem_delete(session->recv_sem);
    session->recv_sem = NULL;

    os_sem_delete(session->send_sem);
    session->send_sem = NULL;

    /* free之前要释放控制块资源 */
    os_free(session);
    /* 删除正在执行的任务 */
    os_task_exit();
}


/**
 * @internal
 * Creates and initializes a Telnet server.
 *
 * @param none
 * @retval TelnetServer_t* The structure used to represent the current server.
 */
static TELNET_SESSION_PTR CreateTelnetSession(void)
{
#ifdef TELNET_DEBUG
    os_printf("[Telnet Server] Allocating memory for server.\n\r");
#endif
    /* Allocate structure server to maintain Telnet connection information */
    TELNET_SESSION_PTR session = NULL;
    session = os_malloc(sizeof(TELNET_SESSION_T));
    if(!session) 
    {
        os_printf("[%s:%d]malloc fail\n",__FUNCTION__,__LINE__);
        return NULL;
    }
    memset_s(session, sizeof(TELNET_SESSION_T), 0, sizeof(TELNET_SESSION_T));

    return session;
}


static err_t telnet_accept_callback(void *arg, struct tcp_pcb *new_pcb, err_t err) 
{
    TELNET_SESSION_PTR session = NULL;

    session = CreateTelnetSession();
    if(!session)
    {
        os_printf("malloc fail\n");
        tcp_close(new_pcb);
        return ERR_MEM;
    }

    /* 参数检验,    最大并发数校验 */
    if (byTelnetClientCount > TELNETCLIENTNUM - 1)
    {
        printf("Telnet Client Count:%d > %d!!\r\n",byTelnetClientCount, TELNETCLIENTNUM);
        tcp_close(new_pcb);
        os_free(session);
        return ERR_MEM;
    }

    /* 需要找到空闲的写入, 避免覆盖其他连接 */
    add_connection(new_pcb);

    /* 有客户端后才把串口日志镜像到 telnet */
    if (NULL == __print_hook) {
        print_redirect(telent_send_task, NULL);
    }

    memset(session, 0, sizeof(TELNET_SESSION_T));
    session->pcb = new_pcb;
    session->session_state = TELNET_STATE_INIT;

    tcp_nagle_enable(new_pcb);
    session->pcb = new_pcb;
    session->pcb->so_options |= SOF_KEEPALIVE;
    session->pcb->keep_idle = 300000UL; // 5 Minutes
    session->pcb->keep_intvl = 1000UL; // 1 Second
    session->pcb->keep_cnt = 9; // 9 Consecutive failures terminate

    os_sem_init(session->recv_sem);
    os_sem_init(session->send_sem);

    /* 将session作为一个参数传给tcp */
    tcp_arg(new_pcb, session);
    tcp_recv(new_pcb, telnet_recv_callback);
    tcp_err(new_pcb, telnet_err_callback);

    /* 启动负责和telnet客户端进行数据交互的任务 */
    if (pdPASS != os_task_create(telnet_session_task, 
                                "TelnetSession", 
                                1024, 
                                session, 
                                TASK_PRIORITY_BELOW_NORMAL, 
                                &session->task_handle)) 
    {
        os_printf("Failed to create session task\n");
        tcp_close(new_pcb);
        os_free(session);
        return ERR_ABRT;
    }

    TelnetWriteDebugMessage(session,"[TELNET] Telnet Server Connected. Welcome.");
    return ERR_OK;
}


void telnet_server_init(void) 
{
    struct tcp_pcb *listen_pcb = NULL;

    /* 监听控制块 */
    listen_pcb = tcp_new();
    if (NULL == listen_pcb) 
    {
        os_printf("Failed to create new PCB\n");
        return;
    }
    // 创建互斥锁
    os_mutex_init(telnet_recv_mutex);

    /* 不在这里无条件 print_redirect：无客户端时每条日志都会对空/半开 PCB
     * tcp_write 失败并刷屏。有客户端 accept 后再挂 hook（见 telnet_accept）。 */

    // 绑定到23端口（Telnet）
    tcp_bind(listen_pcb, IP_ADDR_ANY, 23);
    /* 监听 */
    listen_pcb = tcp_listen(listen_pcb);

    /* 接收连接，可并发，不需要while循环 */
    tcp_accept(listen_pcb, telnet_accept_callback);  // 设置接受连接回调

}


