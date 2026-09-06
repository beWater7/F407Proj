/***************************************************************
 * @file    : upg_apply.c
 * @brief   : 流式解析 upg.bin（UPG_HDR_MAGIC_LDR）并写入各介质
 *
 *   loader blob -> SPI PART_LOADER + 内Flash 备份（内容不变则跳过写入）
 *   APP1/APP2   -> 内部 Flash
 *   web         -> SPI PART_WEB（可选，CRC 前缀与 APP 侧一致）
 ***************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "upg_apply.h"
#include "upgrade.h"
#include "loader_meta.h"
#include "flash_manage.h"
#include "bsp_internalFlash.h"
#include "iap.h"
#include "crc.h"
#include "os_debug.h"

#define UPG_WEB_CRC_LEN  9u
#define UPG_MAX_FILE     (2u * 1024u * 1024u)

typedef enum {
    UPG_ST_HDR = 0,
    UPG_ST_LOADER,
    UPG_ST_FW1,
    UPG_ST_FW2,
    UPG_ST_WEB,
    UPG_ST_DONE,
    UPG_ST_ERR
} upg_st_t;

typedef struct {
    uint8_t active;
    upg_st_t phase;
    uint8_t hdr_buf[sizeof(struct upg_header_ldr)];
    uint8_t hdr_got;
    struct upg_header_ldr hdr;
    uint8_t *loader_buf;
    uint32_t loader_got;
    uint32_t fw1_got;
    uint32_t fw2_got;
    uint32_t web_got;
    uint8_t web_started;
    uint32_t crc_run;
    uint32_t total_len;
    uint8_t want_reboot;
} upg_ctx_t;

static upg_ctx_t s_upg;

static void upg_erase_range(uint32_t start_addr, uint32_t size)
{
    uint32_t addr;
    uint32_t end_addr;

    if (size == 0u) {
        return;
    }
    end_addr = start_addr + size;
    addr = start_addr;
    while (addr < end_addr) {
        internal_flash_erase(addr);
        addr = GetNextSectorAddr(addr);
        if (addr <= start_addr) {
            break;
        }
    }
}

static int upg_commit_ota_idle(void)
{
    ota_flag_t done;

    memset(&done, 0, sizeof(done));
    done.magic = OTA_FLAG_MAGIC;
    done.active_app = 0;
    done.upgrade_flag = 0;
    done.state = 0xAAu;
    if (SPI_FLASH_WRITE_VERIFY(PART_OTA, 0, (uint8_t *)&done, sizeof(done))) {
        BOOTL_PRINT(BOOT_ERROR"upg: commit OTA idle flag failed\n");
        return -1;
    }
    return 0;
}

/* SPI PART_LOADER 现有内容是否与 blob 完全一致（读回比对，避免无谓擦写）。
 * 1=一致可跳过；0=不同需重写；-1=读失败按需重写处理。 */
static int upg_spi_loader_matches(const uint8_t *blob, uint32_t len)
{
    uint8_t *rb;
    int same;

    rb = (uint8_t *)malloc(len);
    if (rb == NULL) {
        return -1;
    }
    if (SPI_FLASH_READ(PART_LOADER, 0, rb, len) < 0) {
        free(rb);
        return -1;
    }
    same = (memcmp(rb, blob, len) == 0) ? 1 : 0;
    free(rb);
    return same;
}

static int upg_write_loader_media(const uint8_t *blob, uint32_t len)
{
    loader_header_t hdr;
    uint32_t i;

    if (len < sizeof(hdr)) {
        return -1;
    }
    memcpy(&hdr, blob, sizeof(hdr));
    if (hdr.magic != LOADER_HDR_MAGIC ||
        hdr.size == 0u || hdr.size > LOADER_MAX_SIZE ||
        len != sizeof(hdr) + hdr.size) {
        BOOTL_PRINT(BOOT_ERROR"upg: loader blob header invalid\n");
        return -1;
    }
    if (crc32_checksum(blob + sizeof(hdr), hdr.size) != hdr.crc32) {
        BOOTL_PRINT(BOOT_ERROR"upg: loader blob crc fail\n");
        return -1;
    }

    /* 内部 Flash 备份：内容相同则跳过（省一次扇区擦写）。 */
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
    }

    /* SPI 主分区：日常只改 APP 时 loader blob 通常不变，读回比对一致就跳过
     * 64K 擦除+写入+校验——这是单次升级里最大的一段卡顿/掉电窗口/Flash 磨损。
     * PART_LOADER_BK 在当前 boot 方案里已不再被读取（死分区），不再写。 */
    if (upg_spi_loader_matches(blob, len) == 1) {
        BOOTL_PRINT(BOOT_INFO"upg: loader unchanged (ver=0x%08lx), skip SPI write\n",
                    (unsigned long)hdr.version);
        return 0;
    }
    if (SPI_FLASH_WRITE_VERIFY(PART_LOADER, 0, (uint8_t *)blob, len) != 0) {
        BOOTL_PRINT(BOOT_ERROR"upg: write PART_LOADER fail\n");
        return -1;
    }
    BOOTL_PRINT(BOOT_INFO"upg: loader ver=0x%08lx size=%lu written\n",
                (unsigned long)hdr.version, (unsigned long)hdr.size);
    return 0;
}

static int upg_start_web(void)
{
    uint8_t ph[UPG_WEB_CRC_LEN];
    uint32_t base = spi_flash_table[PART_WEB].start_addr;
    uint32_t end = base + PARTITION_HEADER_SIZE + UPG_WEB_CRC_LEN + s_upg.hdr.web_len;

    if (SPI_FLASH_ERASE(PART_WEB, base, end)) {
        BOOTL_PRINT(BOOT_ERROR"upg: web erase fail\n");
        return -1;
    }
    memset(ph, '0', UPG_WEB_CRC_LEN - 1u);
    ph[UPG_WEB_CRC_LEN - 1u] = '\n';
    if (SPI_FLASH_WRITE(PART_WEB, 0, ph, UPG_WEB_CRC_LEN)) {
        BOOTL_PRINT(BOOT_ERROR"upg: web placeholder fail\n");
        return -1;
    }
    s_upg.web_started = 1;
    return 0;
}

/* web 段独立 CRC，供 PART_WEB 前缀 */
static uint32_t s_web_crc_run;

static int upg_finalize_web_prefix(void)
{
    char tmp[16];
    uint8_t prefix[UPG_WEB_CRC_LEN];
    uint8_t *sector;
    uint32_t web_crc;

    if (s_upg.hdr.web_len == 0u) {
        return 0;
    }
    if (!s_upg.web_started || s_upg.web_got != s_upg.hdr.web_len) {
        BOOTL_PRINT(BOOT_ERROR"upg: web incomplete %lu/%lu\n",
                    (unsigned long)s_upg.web_got,
                    (unsigned long)s_upg.hdr.web_len);
        return -1;
    }

    web_crc = crc32_finish(s_web_crc_run);
    snprintf(tmp, sizeof(tmp), "%08lx", (unsigned long)web_crc);
    memset(prefix, 0, sizeof(prefix));
    memcpy(prefix, tmp, UPG_WEB_CRC_LEN - 1u);
    prefix[UPG_WEB_CRC_LEN - 1u] = '\n';

    sector = (uint8_t *)malloc(SECTOR_SIZE);
    if (sector == NULL) {
        BOOTL_PRINT(BOOT_ERROR"upg: web sector malloc fail\n");
        return -1;
    }
    if (SPI_FLASH_READ(PART_WEB, 0, sector, SECTOR_SIZE) < 0) {
        free(sector);
        return -1;
    }
    memcpy(sector, prefix, UPG_WEB_CRC_LEN);
    if (SPI_FLASH_WRITE(PART_WEB, 0, sector, SECTOR_SIZE)) {
        free(sector);
        return -1;
    }
    free(sector);
    spi_flash_table[PART_WEB].size_used = s_upg.hdr.web_len + UPG_WEB_CRC_LEN;
    BOOTL_PRINT(BOOT_INFO"upg: web ok len=%lu crc=0x%08lx\n",
                (unsigned long)s_upg.hdr.web_len, (unsigned long)web_crc);
    return 0;
}

static int upg_parse_header(void)
{
    struct upg_header_ldr *h = &s_upg.hdr;

    memcpy(h, s_upg.hdr_buf, sizeof(*h));
    if (h->magic != UPG_HDR_MAGIC_LDR) {
        BOOTL_PRINT(BOOT_ERROR"upg: bad magic 0x%08lx\n", (unsigned long)h->magic);
        return -1;
    }
    if (h->loader_len > (sizeof(loader_header_t) + LOADER_MAX_SIZE) ||
        h->fw1_len > APP_FLASH_SIZE ||
        h->fw2_len > APP_FLASH_SIZE ||
        h->web_len > (1024u * 1024u)) {
        BOOTL_PRINT(BOOT_ERROR"upg: section too large\n");
        return -1;
    }
    if ((sizeof(*h) + h->loader_len + h->fw1_len + h->fw2_len + h->web_len) > s_upg.total_len) {
        BOOTL_PRINT(BOOT_ERROR"upg: sizes exceed file\n");
        return -1;
    }

    if (h->loader_len > 0u) {
        s_upg.loader_buf = (uint8_t *)malloc(h->loader_len);
        if (s_upg.loader_buf == NULL) {
            BOOTL_PRINT(BOOT_ERROR"upg: loader malloc fail\n");
            return -1;
        }
    }
    if (h->fw1_len > 0u) {
        upg_erase_range(APP1_ADDRESS, h->fw1_len);
    }
    if (h->fw2_len > 0u) {
        upg_erase_range(APP2_ADDRESS, h->fw2_len);
    }
    if (h->web_len > 0u) {
        if (upg_start_web() != 0) {
            return -1;
        }
        s_web_crc_run = crc32_begin();
    }

    s_upg.crc_run = crc32_begin();
    BOOTL_PRINT(BOOT_INFO"upg: ldr=%lu fw1=%lu fw2=%lu web=%lu\n",
                (unsigned long)h->loader_len,
                (unsigned long)h->fw1_len,
                (unsigned long)h->fw2_len,
                (unsigned long)h->web_len);

    if (h->loader_len > 0u) {
        s_upg.phase = UPG_ST_LOADER;
    } else if (h->fw1_len > 0u) {
        s_upg.phase = UPG_ST_FW1;
    } else if (h->fw2_len > 0u) {
        s_upg.phase = UPG_ST_FW2;
    } else if (h->web_len > 0u) {
        s_upg.phase = UPG_ST_WEB;
    } else {
        s_upg.phase = UPG_ST_DONE;
    }
    return 0;
}

static void upg_advance_after(upg_st_t done)
{
    if (done == UPG_ST_LOADER) {
        s_upg.phase = (s_upg.hdr.fw1_len > 0u) ? UPG_ST_FW1 :
                      (s_upg.hdr.fw2_len > 0u) ? UPG_ST_FW2 :
                      (s_upg.hdr.web_len > 0u) ? UPG_ST_WEB : UPG_ST_DONE;
    } else if (done == UPG_ST_FW1) {
        s_upg.phase = (s_upg.hdr.fw2_len > 0u) ? UPG_ST_FW2 :
                      (s_upg.hdr.web_len > 0u) ? UPG_ST_WEB : UPG_ST_DONE;
    } else if (done == UPG_ST_FW2) {
        s_upg.phase = (s_upg.hdr.web_len > 0u) ? UPG_ST_WEB : UPG_ST_DONE;
    } else {
        s_upg.phase = UPG_ST_DONE;
    }
}

int upg_apply_begin(uint32_t total_len)
{
    upg_apply_abort();
    if (total_len < sizeof(struct upg_header_ldr) || total_len > UPG_MAX_FILE) {
        return -1;
    }
    memset(&s_upg, 0, sizeof(s_upg));
    s_upg.active = 1;
    s_upg.phase = UPG_ST_HDR;
    s_upg.total_len = total_len;
    return 0;
}

int upg_apply_feed(const uint8_t *data, uint32_t len)
{
    if (!s_upg.active || s_upg.phase == UPG_ST_ERR) {
        return -1;
    }

    while (len > 0u) {
        uint32_t n;
        uint32_t remain;

        if (s_upg.phase == UPG_ST_HDR) {
            n = (uint32_t)(sizeof(s_upg.hdr_buf) - s_upg.hdr_got);
            if (n > len) {
                n = len;
            }
            memcpy(s_upg.hdr_buf + s_upg.hdr_got, data, n);
            s_upg.hdr_got = (uint8_t)(s_upg.hdr_got + n);
            data += n;
            len -= n;
            if (s_upg.hdr_got >= sizeof(s_upg.hdr_buf)) {
                if (upg_parse_header() != 0) {
                    s_upg.phase = UPG_ST_ERR;
                    return -1;
                }
            }
            continue;
        }

        if (s_upg.phase == UPG_ST_LOADER) {
            remain = s_upg.hdr.loader_len - s_upg.loader_got;
            n = (remain > len) ? len : remain;
            memcpy(s_upg.loader_buf + s_upg.loader_got, data, n);
            s_upg.crc_run = crc32_update(s_upg.crc_run, data, n);
            s_upg.loader_got += n;
            data += n;
            len -= n;
            if (s_upg.loader_got == s_upg.hdr.loader_len) {
                if (upg_write_loader_media(s_upg.loader_buf, s_upg.hdr.loader_len) != 0) {
                    s_upg.phase = UPG_ST_ERR;
                    return -1;
                }
                free(s_upg.loader_buf);
                s_upg.loader_buf = NULL;
                upg_advance_after(UPG_ST_LOADER);
            }
            continue;
        }

        if (s_upg.phase == UPG_ST_FW1) {
            remain = s_upg.hdr.fw1_len - s_upg.fw1_got;
            n = (remain > len) ? len : remain;
            internal_flash_write(APP1_ADDRESS + s_upg.fw1_got, (uint8_t *)data, n);
            s_upg.crc_run = crc32_update(s_upg.crc_run, data, n);
            s_upg.fw1_got += n;
            data += n;
            len -= n;
            if (s_upg.fw1_got == s_upg.hdr.fw1_len) {
                upg_advance_after(UPG_ST_FW1);
            }
            continue;
        }

        if (s_upg.phase == UPG_ST_FW2) {
            remain = s_upg.hdr.fw2_len - s_upg.fw2_got;
            n = (remain > len) ? len : remain;
            internal_flash_write(APP2_ADDRESS + s_upg.fw2_got, (uint8_t *)data, n);
            s_upg.crc_run = crc32_update(s_upg.crc_run, data, n);
            s_upg.fw2_got += n;
            data += n;
            len -= n;
            if (s_upg.fw2_got == s_upg.hdr.fw2_len) {
                upg_advance_after(UPG_ST_FW2);
            }
            continue;
        }

        if (s_upg.phase == UPG_ST_WEB) {
            remain = s_upg.hdr.web_len - s_upg.web_got;
            n = (remain > len) ? len : remain;
            if (SPI_FLASH_WRITE(PART_WEB, UPG_WEB_CRC_LEN + s_upg.web_got,
                                (uint8_t *)data, n)) {
                s_upg.phase = UPG_ST_ERR;
                return -1;
            }
            s_upg.crc_run = crc32_update(s_upg.crc_run, data, n);
            s_web_crc_run = crc32_update(s_web_crc_run, data, n);
            s_upg.web_got += n;
            data += n;
            len -= n;
            if (s_upg.web_got == s_upg.hdr.web_len) {
                upg_advance_after(UPG_ST_WEB);
            }
            continue;
        }

        /* 头之后的填充（YMODEM 1A）忽略 */
        break;
    }
    return 0;
}

int upg_apply_finish(void)
{
    uint32_t crc;

    if (!s_upg.active || s_upg.phase == UPG_ST_ERR) {
        return -1;
    }
    if (s_upg.phase != UPG_ST_DONE &&
        !(s_upg.phase == UPG_ST_WEB && s_upg.web_got == s_upg.hdr.web_len)) {
        if (!(s_upg.loader_got == s_upg.hdr.loader_len &&
              s_upg.fw1_got == s_upg.hdr.fw1_len &&
              s_upg.fw2_got == s_upg.hdr.fw2_len &&
              s_upg.web_got == s_upg.hdr.web_len)) {
            BOOTL_PRINT(BOOT_ERROR"upg: incomplete sections\n");
            return -1;
        }
    }

    crc = crc32_finish(s_upg.crc_run);
    if (crc != s_upg.hdr.crc) {
        BOOTL_PRINT(BOOT_ERROR"upg: crc mismatch got=0x%08lx expect=0x%08lx\n",
                    (unsigned long)crc, (unsigned long)s_upg.hdr.crc);
        return -1;
    }
    if (upg_finalize_web_prefix() != 0) {
        return -1;
    }
    if (upg_commit_ota_idle() != 0) {
        return -1;
    }
    s_upg.want_reboot = 1;
    s_upg.phase = UPG_ST_DONE;
    BOOTL_PRINT(BOOT_REPORT"upg: apply ok, reboot to load new loader\n");
    return 0;
}

void upg_apply_abort(void)
{
    if (s_upg.loader_buf != NULL) {
        free(s_upg.loader_buf);
        s_upg.loader_buf = NULL;
    }
    memset(&s_upg, 0, sizeof(s_upg));
}

int upg_apply_want_reboot(void)
{
    return s_upg.want_reboot ? 1 : 0;
}
