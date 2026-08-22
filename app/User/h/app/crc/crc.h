/***************************************************************
 * @file    :  crc.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __CRC_H__
#define __CRC_H__


unsigned int crc32_checksum(const unsigned char *ptr, unsigned int len);
/** 流式 CRC32（与 crc32_checksum 同一多项式）；begin→update*→finish */
unsigned int crc32_begin(void);
unsigned int crc32_update(unsigned int crc, const unsigned char *ptr, unsigned int len);
unsigned int crc32_finish(unsigned int crc);


#endif /* __CRC_H__ */


