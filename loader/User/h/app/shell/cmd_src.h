#ifndef __CMD_SRC_H__
#define	__CMD_SRC_H__

#ifndef UNUSED_ARG
#define UNUSED_ARG(x) (void)x
#endif

#include "typedef.h"

typedef void (*cmd_func)(void *arg);

typedef struct {
    char byCmdName[16];
    cmd_func fn;
    char byCmdDesc[32];
}CMD_ENTRY_T;

typedef enum {
    MODE_CMD = 0,   // 命令行模式
    MODE_YMODEM,    // YMODEM 文件接收模式
} usart_mode_t;

#define USER_NAME        "@STM32:"
#define SHELL_BUF_SIZE    64
#define HIST_MAX          10              // 最大历史数据条数


uint8 getResetFlag(void);
void setResetFlag(void *arg);
void setStandByFlag(uint8 enabled);
uint8 getUsartMode();
void setUsartMode(uint8 enabled);
void cmd_help(void *arg);
void cmd_update(void *arg);
void cmd_goto(void *arg);
cmd_func parseCmd(uint8 *data/*, uint8 len*/);
void sys_hex_dump(void *arg);
void mini_cli_loop(void);


#endif /* __CMD_SRC_H__ */

