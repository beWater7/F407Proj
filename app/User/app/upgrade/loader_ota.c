/***************************************************************
 * @file    : loader_ota.c
 * @author  : LDY
 * @version : 2.0
 * @date    : 2026-08-29
 * @brief   : app 侧 loader 升级上传接口
 *
 * 调用方（web 上传 / 协议层）拿到 [loader_header_t][loader.bin] 的 blob 后，
 * 交给 upgrade_write_loader() 直接覆盖 SPI PART_LOADER 主区，并同步写入
 * 内部 Flash LOADER_BACKUP_ADDR 备份扇区。
 *
 * 断电安全：先写内部备份，再覆盖 SPI 主区；若覆盖途中断电导致 SPI 主区
 * 损坏，boot 会从内部备份加载旧 loader 兜底。
 *
 * @note    不再使用 SPI PART_LOADER_BK（staging）中转。
 * @copyright Copyright (c) [2026] [LDY/STM32F407]
 ***************************************************************/
#include <string.h>
#include <stdlib.h>
#include "upgrade.h"
#include "flash_manage.h"
#include "loader_meta.h"
#include "bsp_internalFlash.h"
#include "os_debug.h"
#include "crc.h"

/*****************************************************
 * @fn       upgrade_write_loader
 * @brief    把 loader blob 写入 SPI PART_LOADER 主区 + 内部 Flash 备份
 * @param    blob : [loader_header_t][loader.bin]
 *           len  : blob 总长（= sizeof(loader_header_t) + size）
 * @retval   0 成功  -1 参数/校验/写入失败
 *****************************************************/
int upgrade_write_loader(const uint8_t *blob, uint32_t len)
{
    loader_header_t hdr;
    uint32_t i;

    if (blob == NULL || len < sizeof(loader_header_t)) {
        os_debug("upgrade_write_loader: bad param len=%lu\r\n", (unsigned long)len);
        return -1;
    }

    memcpy(&hdr, blob, sizeof(hdr));
    if (hdr.magic != LOADER_HDR_MAGIC) {
        os_debug("upgrade_write_loader: bad magic 0x%08lx\r\n", (unsigned long)hdr.magic);
        return -1;
    }
    if (hdr.size == 0u || hdr.size > LOADER_MAX_SIZE ||
        len != sizeof(hdr) + hdr.size) {
        os_debug("upgrade_write_loader: bad size %lu len %lu\r\n",
                 (unsigned long)hdr.size, (unsigned long)len);
        return -1;
    }
    if (crc32_checksum(blob + sizeof(hdr), hdr.size) != hdr.crc32) {
        os_debug("upgrade_write_loader: crc fail\r\n");
        return -1;
    }

    /* 1) 先写内部 Flash 备份扇区（内容相同则跳过，避免无谓擦写） */
    if (len <= LOADER_BACKUP_MAX) {
        for (i = 0; i < len; i++) {
            if (*((const uint8_t *)LOADER_BACKUP_ADDR + i) != blob[i]) {
                break;
            }
        }
        if (i != len) {
            internal_flash_erase(LOADER_BACKUP_ADDR);
            internal_flash_write(LOADER_BACKUP_ADDR, (uint8_t *)blob, len);
        }
    } else {
        os_debug("upgrade_write_loader: too big for internal backup %lu\r\n",
                 (unsigned long)len);
        return -1;
    }

    /* 2) 再覆盖 SPI PART_LOADER 主区 */
    if (SPI_FLASH_WRITE_VERIFY(PART_LOADER, 0, (uint8_t *)blob, len) != 0) {
        os_debug("upgrade_write_loader: write PART_LOADER failed\r\n");
        return -1;
    }

    os_printf("loader OTA done: ver=0x%08lx size=%lu -> SPI PART_LOADER + internal backup\r\n",
              (unsigned long)hdr.version, (unsigned long)hdr.size);
    return 0;
}
