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

#include "typedef.h"
#include "lwip/apps/fs.h"

typedef enum {
  UPDATE_IDLE             = 0x00, // 无升级
  UPDATE_DOWNLOADING      = 0x01, // App正在下载
  UPDATE_DOWNLOAD_OK      = 0x02, // 下载完成待重启
  UPDATE_FLASH_ERR        = 0x03, // 下载失败
  UPDATE_COPYING          = 0x04, // Boot正在搬运
  UPDATE_COPY_OK          = 0x05, // 搬运完成
  UPDATE_SUCCESS          = 0xAA, // 最终成功（唯一有效成功）
  UPDATE_ERROR            = 0xFF  // 失败回滚
} UpdateStateTypeDef;


struct upg_header {
    uint32_t magic;     // 固定标志，例如 0x55475021 ("UGP!")
    uint32_t fw_len;    // 固件长度
    uint32_t web_len;   // web 长度
    uint32_t crc;       // （可选）整体校验
};


typedef struct {
    uint32_t state;
    uint32_t active_app;   // 现在有效的是 APP1 还是 APP2
    uint32_t upgrade_flag; // 1 = 有新固件待切换
    uint32_t target_app;   // 新固件放在哪个分区
    uint32_t len;          //
    uint32_t crc32;        // 新固件的 CRC32
    uint32_t magic;        // 固定魔术字，比如 0xA5A5A5A5
} ota_flag_t;


/* 跳过第一个boundary报文 */
#define CONTENT_TYPE      "Content-Type: "
#define CONTENT_TYPE_LEN   14

#define UPG_HDR_MAGIC  0x55475021
/* 升级模式 */
#define MAIN_APP  1
#define DUAL_APP  2


#define OTA_MODE  MAIN_APP

/* 使用SPI FLASH 缓存固件 */
#define OTA_REGION_SPI_FLASH  1

/* 升级固件同时升级web */
#define OTA_FW_WITH_WEB       1

#define ota_printf printf
// 定义进度条长度
#define PROGRESS_BAR_LENGTH 53

#define APP1_ADDRESS (ADDR_FLASH_SECTOR_0 + 0x00008000) //0x08000000
#define APP2_ADDRESS ADDR_FLASH_SECTOR_7

/*--------------------------WEB--------------------------*/
#define MAX_FILES 16

typedef enum{
  WEB_HTML,
  WEB_CSS,
  WEB_JS,
  WEB_PNG,
  WEB_JPG,
  WEB_ICO,
  WEB_GIF,
  WEB_GZIP,
  WEB_TYPE_MAX
}WEB_FILE_TYPE;


typedef struct {
    uint32_t type;    // 文件类型枚举
    uint32_t size;   // 文件大小
    uint32_t offset; // 文件在数据区起始偏移
} WebFileEntry;


typedef struct {
    char byName[48];
    uint8 byType;
    uint8 byIsGz;
    uint8 byRes[2];
}WebFileDesc;


typedef struct {
    uint32_t file_count;      // 文件总数
    WebFileEntry files[MAX_FILES];     // 文件索引表
} WebBinHeader;

#define WEB_FILE_GZIP      1
#define WEB_BIN_HDR_SIZE   (sizeof(WebBinHeader))
#define WEB_UPGRADE_DEBUG  1


/*--------------------------WEB--------------------------*/

uint8_t web_upgrade(void *hs, char *webFile, int len);
uint8_t fw_upgrade(void *hs, char *webFile, int len);
void upgrade_buf_release(void);
uint8_t getWebUpgrade(void);
void setWebUpgrade(uint8_t byStatus);
void PrintProgressBar(uint32_t size, uint32_t total_size);

#endif



