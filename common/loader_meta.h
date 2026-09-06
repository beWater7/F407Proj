/***************************************************************
 * @file    : loader_meta.h
 * @author  : LDY
 * @version : 1.0
 * @date    : 2026-08-25
 * @brief   : loader 元数据与跨模块常量（stage0 / loader / app 三方共享）
 *
 * loader 以 [loader_header_t][loader.bin] 的 "blob" 形式存放在 SPI Flash
 * 分区与内部 Flash 备份区中；stage0 解析该 blob，CRC 校验后搬运到 SRAM
 * 0x20000000 执行。
 *
 * @note    三镜像共用本文件（common/loader_meta.h）。
 * @copyright Copyright (c) [2026] [LDY/STM32F407]
 ***************************************************************/

#ifndef __LOADER_META_H__
#define __LOADER_META_H__

#include <stdint.h>

#define LOADER_HDR_MAGIC   0x52444C32u   /* "2LDR" */
/* loader 版本号，打包/日志/人工核对用。改 loader 逻辑后必须递增，
 * 否则无法从日志/烧录记录区分新旧 loader。 */
#define LOADER_VERSION     0x00000103u

/* loader 在 SRAM 的运行区 */
#define LOADER_RAM_BASE    0x20000000u
#define LOADER_MAX_SIZE    0x10000u      /* 64KB，超出则 stage0 拒绝加载 */

/* loader 在 SPI 的主分区（需与 flash_manage.h PART_LOADER 一致） */
#define LOADER_SPI_ACTIVE_BASE  0x000000u  /* PART_LOADER  主 loader */

/* loader 内部 Flash 备份区（专用扇区，boot 在 SPI 损坏时兜底） */
#define LOADER_BACKUP_ADDR  0x080C0000u
#define LOADER_BACKUP_MAX   0x10000u

typedef struct {
    uint32_t magic;    /* LOADER_HDR_MAGIC */
    uint32_t version;  /* loader 版本标识（打包/日志/人工核对用） */
    uint32_t size;     /* loader.bin 字节数（不含本头） */
    uint32_t crc32;    /* loader.bin 的 CRC32 */
} loader_header_t;

#endif /* __LOADER_META_H__ */
