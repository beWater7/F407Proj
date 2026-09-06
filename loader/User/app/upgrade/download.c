/*
 * download.c
 *
 *  Created on: 2017年9月21日
 *      Author: Administrator
 *    Function:等待用户选择传送文件操作,或者放弃操作以及一些提示信息，
 *             但真正实现传送的是ymodem．c源文件。
 */
#include "include.h"
#include "flash_manage.h"
#include "upg_apply.h"
#include "bsp_debug_usart.h"

extern uint8_t file_name[FILE_NAME_LENGTH];
uint8_t tab_1024[1024] = {0};
extern YMODEM_FILE_CTRL_T g_stYmodemFileCtrl;
extern uint32_t dwCurrentAppAddr;
void DelayMs(uint32_t ms);

/*
 * 高速 YMODEM(双向确认): SerialDownload 以 115200 打出握手横幅后, 打印 "BAUD <N>",
 * 然后在 115200 等 xfer 回一个确认字节 'B'(≤1s):
 *   - 收到 'B' -> 切到 N 发 'C'(高速路径)
 *   - 超时/其它字节 -> 留在 115200 发 'C'(低速路径, 兼容旧 xfer / 人为钉 115200)
 * 因此"是否切速"由 loader 等待 + xfer 确认共同决定, 单端无法决定。
 * 注意: N 必须是 tools/xfer/xfer_uart.c baud_to_speed() 支持的档位(460800/921600)。
 */
#define UPG_FAST_BAUD   460800u
#define UPG_FAST_CONFIRM 'B'   /* xfer 侧 BAUD_CONFIRM, 两端需一致 */

/* 切速前等 xfer 确认字节。返回 1=收到 'B'(切高速); 0=超时/其它(留 115200) */
static uint8_t upg_wait_fast_confirm(void)
{
    uint32_t i;

    for (i = 0; i < 100; i++) {   /* ~1s, 步进 10ms */
        if (USART1->SR & USART_FLAG_RXNE) {
            uint8_t c = (uint8_t)(USART1->DR & 0xFFu);
            return (c == UPG_FAST_CONFIRM) ? 1u : 0u;
        }
        DelayMs(10);
    }
    return 0;
}

/**
  * @brief   通过串口接收 upg.bin（或裸 app.bin）并编程
  */
void SerialDownload(void)
{
    uint8_t Number[16] = {0};
    char baud_buf[24];
    uint8_t fast = 0;
    int32_t Size = 0;

    SerialPutString("Waiting for upg.bin ... (press 'a' to abort)\n\r");
    /* 115200 打印高速声明, 等 xfer 确认后再决定是否切速 */
    snprintf(baud_buf, sizeof(baud_buf), "BAUD %lu\n\r",
             (unsigned long)UPG_FAST_BAUD);
    SerialPutString(baud_buf);
    fast = upg_wait_fast_confirm();
    if (fast) {
        Debug_USART_SetBaud(UPG_FAST_BAUD);
        DelayMs(5);
    }
    Size = Ymodem_Receive(&tab_1024[0]);
    /* 切过高速才回切 115200: 之后的完成提示/CLI 在基线速率, 对端可读 */
    if (fast) {
        Debug_USART_SetBaud(DEBUG_USART_BAUDRATE);
    }
    if (Size > 0)
    {
        SerialPutString("\n\n\r Programming Completed Successfully!\n\r--------------------------------\r\n Name: ");
        SerialPutString(file_name);
        Int2Str(Number, Size);
        SerialPutString("\n\r Size: ");
        SerialPutString(Number);
        SerialPutString(" Bytes\r\n");
        SerialPutString("-------------------\r\n");

        if (Ymodem_LastIsDtb())
        {
            SerialPutString("DTB written. Reboot to apply new partition table.\r\n");
            DelayMs(400);
            NVIC_SystemReset();
            return;
        }

        dwCurrentAppAddr = APP1_ADDRESS;

        if (upg_apply_want_reboot())
        {
            SerialPutString("loader+app written. Rebooting...\r\n");
            DelayMs(400);
            NVIC_SystemReset();
        }
        SerialPutString("APP programmed at 0x08008000\r\n");
        SerialPutString("Use \"goto 0\" or \"reset\" to jump to APP\r\n");
    }
    else if (Size == -1)
    {
        SerialPutString("\n\n\rThe image size is higher than the allowed space memory!\n\r");
    }
    else if (Size == -2)
    {
        SerialPutString("\n\n\rVerification failed!\n\r");
    }
    else if (Size == -3)
    {
        SerialPutString("\r\n\nAborted by user.\n\r");
    }
    else
    {
        SerialPutString("\n\rFailed to receive the file!\n\r");
    }
}
