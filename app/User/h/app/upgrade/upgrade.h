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

/* 无感双槽包：magic=UPG_HDR_MAGIC_DUAL，payload = fw1 | fw2 | web */
struct upg_header_dual {
    uint32_t magic;     // UPG_HDR_MAGIC_DUAL
    uint32_t fw1_len;   // APP1 链接镜像长度
    uint32_t fw2_len;   // APP2 链接镜像长度
    uint32_t web_len;   // web.bin 长度
    uint32_t crc;       // fw1+fw2+web CRC32
};


/* 与 bootloader upgrade.h 必须一致 */
#define OTA_FLAG_MAGIC           (0xA5A5A5A5u)
#define OTA_FLAG_UPGRADE_PENDING (1u)

typedef struct {
    uint32_t state;
    uint32_t active_app;   // 现在有效的是 APP1 还是 APP2
    uint32_t upgrade_flag; // 1 = 有新固件待切换
    uint32_t target_app;   // 新固件放在哪个分区
    uint32_t len;          //
    uint32_t crc32;        // 新固件的 CRC32
    uint32_t magic;        // OTA_FLAG_MAGIC
} ota_flag_t;


/* 跳过第一个boundary报文 */
#define CONTENT_TYPE      "Content-Type: "
#define CONTENT_TYPE_LEN   14

#define UPG_HDR_MAGIC       0x55475021u  /* 单槽: fw + web */
#define UPG_HDR_MAGIC_DUAL  0x55475022u  /* 双槽: fw1 + fw2 + web */
/* 升级模式 */
#define MAIN_APP  1
#define DUAL_APP  2

/* 软件 A/B：OTA 写入空闲槽，掉电仍可从旧槽启动 */
#define OTA_MODE  DUAL_APP

/* 使用SPI FLASH 缓存固件 */
#define OTA_REGION_SPI_FLASH  1

/* 升级固件同时升级web */
#define OTA_FW_WITH_WEB       1

/*
 * 整包 malloc 失败时回退为流式写入 SPI（边收边写）。
 * 置 0 可完全关闭流式路径，失败则直接报错。
 */
#ifndef OTA_STREAM_FALLBACK
#define OTA_STREAM_FALLBACK   1
#endif

#define ota_printf printf
// 定义进度条长度
#define PROGRESS_BAR_LENGTH 53

#define APP1_ADDRESS (ADDR_FLASH_SECTOR_0 + 0x00008000) //0x08008000
#define APP2_ADDRESS ADDR_FLASH_SECTOR_7                //0x08060000
#define APP_FLASH_SIZE       (256u * 1024u)

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
#define WEB_UPGRADE_DEBUG  0


/*--------------------------WEB--------------------------*/

uint8_t web_upgrade(void *hs, char *webFile, int len);
uint8_t fw_upgrade(void *hs, char *webFile, int len);
void upgrade_buf_release(void);
uint8_t getWebUpgrade(void);
void setWebUpgrade(uint8_t byStatus);
void PrintProgressBar(uint32_t size, uint32_t total_size);
int upgrade_write_fw(uint8 *fw, uint32 len);
int upgrade_write_fw_v2(uint8 *fw, uint32 len);
/** 固件已在 SPI PART_APP1 时，只写 OTA 标志（流式升级收尾用） */
int upgrade_commit_ota_flag(uint32 len, uint32 crc32);
uint8_t ota_get_active_slot(void);

/* OTA 前腾堆：挂起后台任务、关闭其它 HTTP 连接 */
void ota_register_background_task(void *task_handle);
void ota_prepare_heap(void *keep_http_state);
/* web 升级不重启：成功/失败/POST 结束都必须调，否则 fs 会一直 OTA busy */
void ota_finish_heap_prepare(void);
uint8_t ota_heap_busy(void);

#endif



