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

/* 与 APP upgrade.h / genUpgBin.py 必须一致 */
#define UPG_HDR_MAGIC       0x55475021u
#define UPG_HDR_MAGIC_DUAL  0x55475022u
#define UPG_HDR_MAGIC_LDR   0x55475023u  /* [hdr][loader_blob][APP1][APP2][web?] */

struct upg_header_ldr {
    uint32_t magic;
    uint32_t loader_len;
    uint32_t fw1_len;
    uint32_t fw2_len;
    uint32_t web_len;
    uint32_t crc;
};

/* 与 APP upgrade.c 写入值必须一致 */
#define OTA_FLAG_MAGIC          (0xA5A5A5A5u)
#define OTA_FLAG_UPGRADE_PENDING (1u)

typedef struct {
    uint32_t state;        /* UpdateStateTypeDef，与 APP 侧 ota_flag_t 必须一致 */
    uint32_t active_app;   // 现在有效的是 APP1 还是 APP2
    uint32_t upgrade_flag; // 1 = 有新固件待切换
    uint32_t target_app;   // 新固件放在哪个分区
    uint32_t len;          //
    uint32_t crc32;        // 新固件的 CRC32
    uint32_t magic;        // OTA_FLAG_MAGIC
} ota_flag_t;

/*
 * pyocd / flash.sh 写在内部 PART_RES（0x080A0000）的一次性覆盖标记。
 * SPI 里的 pending OTA 调试器碰不到；直烧 APP 后写此标记，Boot 取消 SPI 搬运。
 */
#define OTA_HOST_FLASH_MAGIC   (0x534C4648u) /* "HFLS" */
#define OTA_HOST_FLASH_ADDR    (0x080A0000u)

typedef struct {
    uint32_t magic; /* OTA_HOST_FLASH_MAGIC */
    uint32_t slot;  /* 0=APP1 @0x08008000, 1=APP2 @0x08060000 */
} ota_host_flash_t;







#endif


