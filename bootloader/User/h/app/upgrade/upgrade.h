/***************************************************************
 * @file    :  storage_manage.h
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/

#ifndef __UPGRADE_H__
#define __UPGRADE_H__

#include <stdint.h>
#include "typedef.h"



typedef struct {
    uint32_t active_app;   // 现在有效的是 APP1 还是 APP2
    uint32_t upgrade_flag; // 1 = 有新固件待切换
    uint32_t target_app;   // 新固件放在哪个分区
    uint32_t len;          //
    uint32_t crc32;        // 新固件的 CRC32
    uint32_t magic;        // 固定魔术字，比如 0xA5A5A5A5
} ota_flag_t;







#endif


