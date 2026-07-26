/*
 * ymodem.h
 *
 *  Created on: 2017年9月21日
 *      Author: Administrator
 */
#ifndef _YMODEM_H_
#define _YMODEM_H_

#include "os_debug.h"

#define PACKET_SEQNO_INDEX      (1)
#define PACKET_SEQNO_COMP_INDEX (2)

#define PACKET_HEADER           (3)
#define PACKET_TRAILER          (2)
#define PACKET_OVERHEAD         (PACKET_HEADER + PACKET_TRAILER)
#define PACKET_8B_SIZE          (8)
#define PACKET_16B_SIZE         (16)
#define PACKET_32B_SIZE         (32)
#define PACKET_64B_SIZE			(64)
#define PACKET_128B_SIZE        (128)
#define PACKET_256B_SIZE        (256)
#define PACKET_512B_SIZE        (512)
#define PACKET_1K_SIZE          (1024)
#define PACKET_2K_SIZE          (2048)
#define PACKET_1KB_SIZE          (1024)
#define PACKET_2KB_SIZE          (2048)

#define PACKET_SIZE             (128)

#define FILE_NAME_LENGTH        (256)
#define FILE_SIZE_LENGTH        (16)

#define SOH                     (0x01)  //128字节数据包开始
#define STX                     (0x02)  //1024字节的数据包开始
#define STX_8B                  (0xA1)
#define STX_16B                 (0xA2)
#define STX_32B                 (0xA3)
#define STX_64B					(0xA4)  /* start of 64-byte data packet */
#define STX_128B                (0xA5)
#define STX_256B                (0xA6)
#define STX_512B				(0xA7)
#define STX_1KB                 (0xA8)
#define STX_2KB                 (0XA9)

#define EOT                     (0x04)  //结束传输
#define ACK                     (0x06)  //回应
#define NAK                     (0x15)  //没回应
#define CA                      (0x18)  //这两个相继中止转移
#define CRC16                   (0x43)  //'C' == 0x43, 需要 16-bit CRC 

#define ABORT1                  (0x41)  //'A' == 0x41, 用户终止 
#define ABORT2                  (0x61)  //'a' == 0x61, 用户终止

//#define NAK_TIMEOUT             (0x100000)
#define NAK_TIMEOUT             (1000)
#define MAX_ERRORS              (5)

#define YMODEM_DEBUG_EN    0

#define YMODEM_DEBUG(format, ...)                                 \
do {                                                         \
    if ((YMODEM_DEBUG_EN)) {                                      \
        os_printf_api(KERN_TICK KERN_WARN format, ##__VA_ARGS__);        \
    }                                                       \
} while(0)

//uint8 * g_byYmodemFile = NULL;
typedef struct{
    uint8 * byYmodemFile;
    uint32 dwYmodemFileRecvedLen;
}YMODEM_FILE_CTRL_T, *YMODEM_FILE_CTRL_PTR;

#define YMODEM_RX_BUFFER_SIZE 8192  // 例子大小，可调

int32_t Ymodem_Receive (uint8_t *);
uint8_t Ymodem_Transmit (uint8_t *,const  uint8_t* , uint32_t );

#endif  /* _YMODEM_H_ */


