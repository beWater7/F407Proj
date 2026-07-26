#include "cmd_src.h"
#include "common.h"
#include "flash_manage.h"
#include "iap.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void SerialDownload(void);

uint32_t dwCurrentAppAddr = APP1_ADDRESS;

void setAppAddr(uint8_t part_app)
{
    dwCurrentAppAddr = part_app ? APP2_ADDRESS : APP1_ADDRESS;
}
static uint8_t byBootStandbyFlag = 1;

static usart_mode_t g_usart_mode = MODE_CMD;

static CMD_ENTRY_T gs_cmd_t[] = {
  {"reset",    setResetFlag, "jump to APP"},
  {"help",     cmd_help,     "cmd description"},
  {"hex_dump", sys_hex_dump, "flash data hex dump"},
  {"upgrade",  cmd_update,   "usart upgrade fw"},
  {"goto",     cmd_goto,     "jump to APP"},
};

#define CMD_ENTRY_SIZE (sizeof(gs_cmd_t)/sizeof(gs_cmd_t[0]))

static char history[HIST_MAX][SHELL_BUF_SIZE]; // 历史命令
static int  hist_count = 0;                    // 已存历史条数
static int  hist_index = -1;                   // 当前查看的位置（-1 表示不在历史中）
static char cmd_buf[SHELL_BUF_SIZE];           // 指令缓冲
static int  cmd_len = 0;                        // 实际字符数
static int  cursor = 0;                        // 光标位置


/**
  * @brief   读取一个字节
  * @param   c 读取到的字节
  * @retval  0 成功 -1 失败
 **/
static int8_t cli_read_byte(char *c)
{
	// 非阻塞模式
    /* 等待接收缓冲区非空 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET);

    *c = (uint8_t)USART_ReceiveData(USART1);

    return 0;
}


/**
  * @brief   写入一个字节
  * @param   c 写入的字节
  * @retval  void
 **/
static void cli_write_byte(const char c)
{
    /* 等待发送数据寄存器空 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);

    USART_SendData(USART1, (uint8_t)c);

    /* 等待完成发送 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

/**
  * @brief    串口写字符串
  * @param    str 串口写的字符串
  * @retval   void
 **/
void cli_write_str(const char * const str)
{
	char *p_temp = (char *)str;

	while(*p_temp)
		cli_write_byte(*p_temp++);
}


static int cmdline_strtok(char * str ,char ** argv ,int maxread)
{
	int argc = 0;

	for ( ; ' ' == *str ; ++str) ; //跳过空格
	
	for ( ; *str && argc < maxread; ++argc,++argv ) { //字符号不为 '\0' 的时候
	
		for (*argv = str ; ' ' != *str && *str ; ++str);//记录这个参数，然后跳过非空字符
		
		for ( ; ' ' == *str; *str++ = '\0');//每个参数加字符串结束符，跳过空格
	}

	return argc;
}


uint8_t getResetFlag(void)
{
    return byBootStandbyFlag;
}


void setResetFlag(void *arg)
{
    UNUSED_ARG(arg);
    byBootStandbyFlag = 0;
    return;
}


void setStandByFlag(uint8 enabled)
{
    byBootStandbyFlag = enabled;
    return;
}


uint8 getUsartMode()
{
    return g_usart_mode;
}


void setUsartMode(uint8 enabled)
{
    g_usart_mode = enabled;
    return;
}


void cmd_help(void *arg)
{
    uint8 i = 0;
    UNUSED_ARG(arg);
    for(i = 0; i < CMD_ENTRY_SIZE; i++)
    {
        printf("%-16s  %s\r\n", gs_cmd_t[i].byCmdName, gs_cmd_t[i].byCmdDesc);
    }
    return;
}


/* spi flash only
 * usage: hex_dump <part_index> <offset> <len>
 * example: hex_dump 0 0 256
 */
static void hex_dump_print_usage(void)
{
    uint8 i;

    printf("usage: hex_dump <part_index> <offset> <len>\r\n");
    printf("  part_index (SPI flash):\r\n");
    for (i = 0; i < SPI_FLASH_PART_MAX; i++)
    {
        printf("    %u --- %-10s  offset=0x%08lx  size=0x%08lx\r\n",
               i,
               spi_flash_table[i].name ? spi_flash_table[i].name : "?",
               (unsigned long)spi_flash_table[i].start_addr,
               (unsigned long)spi_flash_table[i].size);
    }
    printf("  example: hex_dump 0 0 256\r\n");
}

void sys_hex_dump(void *arg)
{
	uint8 index = 0;
	uint32 dwOffset = 0;
	uint32 dwLen = 0;
	uint32 part_size = 0;
	char* argv[4] = {0};
    int argc = 0;

	argc = cmdline_strtok((char*)arg, argv, 4);
    if (argc < 4 ||
        (argv[1] != NULL && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0)))
    {
        hex_dump_print_usage();
        return;
    }

	index = (uint8)atoi(argv[1]);
	dwOffset = (uint32)atoi(argv[2]);
	dwLen = (uint32)atoi(argv[3]);

	if (index >= SPI_FLASH_PART_MAX)
	{
		printf("invalid part_index %u\r\n", index);
		hex_dump_print_usage();
		return;
	}

	part_size = spi_flash_table[index].size;
	if (dwLen == 0 || dwOffset >= part_size || (dwOffset + dwLen) > part_size)
	{
		printf("invalid offset/len (part %u:%s size=0x%lx)\r\n",
		       index,
		       spi_flash_table[index].name ? spi_flash_table[index].name : "?",
		       (unsigned long)part_size);
		return;
	}

	printf("part %u:%s offset:%lu len:%lu\r\n",
	       index,
	       spi_flash_table[index].name ? spi_flash_table[index].name : "?",
	       (unsigned long)dwOffset,
	       (unsigned long)dwLen);
	hex_dump(index, dwOffset, dwLen, SPI_FLASH_DEV_ID);
}


void cmd_update(void *arg)
{
    (void)arg;
    g_usart_mode = MODE_YMODEM;
    SerialDownload();
    g_usart_mode = MODE_CMD;
}


void cmd_goto(void *arg)
{
    uint8_t part_app = 0;
    char* argv[4] = {0};
    int argc = 0;
    char *args = (char *)arg;

    argc = cmdline_strtok(args, argv, 2);
    (void)argc;
    part_app = (argv[1] != NULL) ? (uint8_t)atoi(argv[1]) : 0;
    printf("jump to address:%08lx...\r\n",
           (unsigned long)(part_app ? APP2_ADDRESS : APP1_ADDRESS));
    setAppAddr(part_app);
    setResetFlag(NULL);
}


cmd_func parseCmd(uint8 *data/*, uint8 len*/)
{
    uint8 i = 0;
    char *cmd = (char *)data;
    size_t cmd_name_len;
    size_t token_len = 0;

    /* 只取第一个 token（到空格或结尾），避免 "helpxxx" 误匹配 "help" */
    while (cmd[token_len] != '\0' && cmd[token_len] != ' ')
    {
        token_len++;
    }

    for (i = 0; i < CMD_ENTRY_SIZE; i++)
    {
        cmd_name_len = strlen(gs_cmd_t[i].byCmdName);
        if (token_len == cmd_name_len &&
            0 == strncmp(cmd, gs_cmd_t[i].byCmdName, cmd_name_len))
        {
            return gs_cmd_t[i].fn;
        }
    }

    return NULL;
}


/**
  * @brief    历史指令更新方式
  * @param    buf 需要显示的历史字符
  * @retval   void
 **/
void history_redraw_line(const char *buf)
{
    int buf_num = strlen(buf);
    
    // 移动光标到正确位置
    for(int i = 0;i < cursor;i++)
        cli_write_byte('\b');
    for(int i = 0;i < cmd_len;i++)
        cli_write_byte(' ');
    for(int i = 0;i < cmd_len;i++)
        cli_write_byte('\b');
    cli_write_str(buf);
    cmd_len = cursor = strlen(buf); 
}


/**
  * @brief    命令行循环
  * @note     不逐字回显：PC 终端本地编辑后整行发送，避免 TX 环回/FIFO
  *           把回显字节再次读进 cmd_buf，造成 hexz__dump / hex_dimump 一类乱码。
  * @retval   void
 **/
void mini_cli_loop(void)
{
    char c;
    cmd_func fn = NULL;
    uint8_t skip_lf = 0;

    cli_write_str("\n");
    cli_write_str(USER_NAME);

    cmd_len = cursor = 0;
    memset(cmd_buf, 0, sizeof(cmd_buf));

    while (1)
    {
        if (cli_read_byte(&c) != 0)
            continue;

        /* 丢弃 ESC 序列（方向键等），避免残字节进缓冲区 */
        if (c == 0x1B)
        {
            char c1 = 0, c2 = 0;
            cli_read_byte(&c1);
            cli_read_byte(&c2);
            (void)c1;
            (void)c2;
            continue;
        }

        if (c == '\b' || c == 0x7F)
        {
            if (cmd_len > 0)
            {
                cmd_len--;
                cmd_buf[cmd_len] = '\0';
            }
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            if (c == '\n' && skip_lf)
            {
                skip_lf = 0;
                continue;
            }
            skip_lf = (c == '\r') ? 1 : 0;

            cli_write_str("\n");
            cmd_buf[cmd_len] = '\0';

            if (cmd_len > 0)
            {
                if (hist_count < HIST_MAX)
                    strcpy(history[hist_count++], cmd_buf);
                else
                {
                    for (int i = 1; i < HIST_MAX; i++)
                        strcpy(history[i - 1], history[i]);
                    strcpy(history[HIST_MAX - 1], cmd_buf);
                }

                hist_index = -1;
                printf("cmdbuf:%s\n", cmd_buf);
                fn = parseCmd((uint8 *)cmd_buf);
                if (fn)
                {
                    fn(cmd_buf);
                }
                else
                {
                    printf("unknown cmd: %s\n", cmd_buf);
                    printf("type \"help\" for command list\n");
                }
                if (0 == getResetFlag())
                {
                    break;
                }
            }

            cmd_len = cursor = 0;
            memset(cmd_buf, 0, sizeof(cmd_buf));
            cli_write_str(USER_NAME);
            continue;
        }

        if ((uint8_t)c < 0x20)
            continue;

        if (cmd_len < SHELL_BUF_SIZE - 1)
        {
            cmd_buf[cmd_len++] = c;
            cmd_buf[cmd_len] = '\0';
            cursor = cmd_len;
            /* 故意不 echo：由 serialTerm3 本地显示，杜绝环回串扰 */
        }
    }
}


