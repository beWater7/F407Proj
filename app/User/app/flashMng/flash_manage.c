/***************************************************************
 * @file    :  storage_manage.c
 * @author  :  LDY
 * @version :  1.0
 * @date    :  2025-06-28
 * @brief   :  存储分区初始化模块
 *
 * @note    :
 *
 * @copyright Copyright (c) [2025] [LDY/STM32F407]
 ***************************************************************/
#include <string.h>
#include "hal_flash.h"
#include "flash_manage.h"
#include "crc.h"
#include "upgrade.h"
#include "dts.h"


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
static int flash_partition_erase(STORAGE_CTRL_T *self,  uint8_t index, uint32_t start_addr, uint32_t end_addr)
{
    uint32_t sector_start = 0; // 向下对齐
    uint32_t sector_end = 0;
    uint32_t addr = 0;
    uint32_t dwUnitLen = 0;

    /* 入参校验 */
    CUSTOM_ASSERT(NULL == self, return -1);
    CUSTOM_ASSERT(start_addr > end_addr, return -1);
    CUSTOM_ASSERT(start_addr < self->pPartInfo[index].start_addr, return -1);
    CUSTOM_ASSERT(end_addr > self->pPartInfo[index].start_addr + self->pPartInfo[index].size, return -1);

    /* 必须按介质类型选擦除方式：以前用 byManage 会误把 SPI 当成内部 Flash
     * 走 hal_int_flash_get_sector()，配置区永远擦不干净 → DEV_MNG_MAGIC 写不进去 */
    if (SPI_FLASH_DEV_ID == self->dev.dev_id)
    {
        if (self->byManage) {
            start_addr += PARTITION_HEADER_SIZE;
        }
        sector_start = start_addr & ~(SECTOR_SIZE - 1);
        sector_end = (end_addr - 1) & ~(SECTOR_SIZE - 1);
        dwUnitLen = SECTOR_SIZE;
    }
    else
    {
        /* 内部flash：hal_int_flash_get_sector 返回扇区编号，步进 8 */
        sector_start = hal_int_flash_get_sector(start_addr);
        sector_end  = hal_int_flash_get_sector(end_addr - 1);
        dwUnitLen = 8;
    }

    os_mutex_lock(self->lock);

    STOR_INFO("erase sector start:0x%x end:0x%x dwUnitLen:%d\n", sector_start,sector_end,dwUnitLen);
    /* 擦除指定区域的数据 */
    for(addr = sector_start; addr <= sector_end; addr += dwUnitLen)
    {
        HW_ERASE(addr); // 擦除扇区
    }

    os_mutex_unlock(self->lock);

    return 0;
}


/*****************************************************
 * @fn       flash_partition_write
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
static int flash_partition_write(STORAGE_CTRL_T *self, uint8_t index, uint32_t offset, uint8_t *data, uint32_t len)
{
    uint32_t start_addr = self->pPartInfo[index].start_addr;
    uint32_t write_addr = start_addr + offset;
    uint32_t part_size = self->pPartInfo[index].size;
    uint32_t done = 0;
    uint32_t last_erased = 0xFFFFFFFFu;
    uint8_t verify_buf[256];
    PartitionHeader hdr = {0};

    CUSTOM_ASSERT(NULL == self, return -1);
    CUSTOM_ASSERT(NULL == data, return -1);
    CUSTOM_ASSERT(0 == len, return -1);

    if (self->byManage) {
        write_addr += PARTITION_HEADER_SIZE;
        if (offset + len > part_size - PARTITION_HEADER_SIZE) {
            os_debug("write out of range\n");
            return -1;
        }
    } else {
        if (offset + len > part_size) {
            os_debug("write out of range offset=%lu len=%lu size=%lu\n",
                     (unsigned long)offset, (unsigned long)len, (unsigned long)part_size);
            return -1;
        }
    }

    os_mutex_lock(self->lock);

    /*
     * SPI：按扇区边擦边写。原先 offset==0 时一次性擦几十个扇区（320KB≈80 扇区），
     * 耗时可到数十秒，httpd/任务易被拖死，表现为“升级成功但 hex_dump 全 FF”。
     */
    if (SPI_FLASH_DEV_ID == self->dev.dev_id) {
        while (done < len) {
            uint32_t phys = write_addr + done;
            uint32_t sector = phys & ~(SECTOR_SIZE - 1);
            uint32_t in_sector = phys - sector;
            uint32_t chunk = SECTOR_SIZE - in_sector;
            uint32_t wlen;
            uint32_t off = 0;

            if (chunk > len - done) {
                chunk = len - done;
            }

            /* 覆盖写(offset==0)或写入扇区起始处：先擦该扇区 */
            if (sector != last_erased && (0 == offset || 0 == in_sector)) {
                HW_ERASE(sector);
                last_erased = sector;
                if ((done & 0xFFFF) == 0) {
                    os_printf(KERN_INFO"spi write %lu/%lu\n",
                              (unsigned long)done, (unsigned long)len);
                }
            }

            /* 分 256B page 写 */
            while (off < chunk) {
                wlen = chunk - off;
                if (wlen > 256) {
                    wlen = 256;
                }
                HW_WRITE(phys + off, data + done + off, wlen);

                /* 写后校验，避免“返回成功但颗粒仍是 0xFF” */
                HW_READ(phys + off, verify_buf, wlen);
                if (memcmp(data + done + off, verify_buf, wlen) != 0) {
                    os_debug("SPI verify fail @0x%08lx (part %u)\n",
                             (unsigned long)(phys + off), index);
                    os_mutex_unlock(self->lock);
                    return -1;
                }
                off += wlen;
            }
            done += chunk;
        }
    } else {
        /* 内部 Flash：保持原逻辑，offset==0 先擦再写 */
        uint32_t end_addr = write_addr + len;
        uint32_t dwWriteLen = 0;
        uint32_t dwLen = 0;

        os_mutex_unlock(self->lock); /* erase 自己会加锁 */
        if (0 == offset) {
            if (flash_partition_erase(self, index, start_addr, end_addr)) {
                os_debug("partition_erase err!\n");
                return -1;
            }
        }
        os_mutex_lock(self->lock);

        while (dwWriteLen < len) {
            dwLen = (len - dwWriteLen) > 256 ? 256 : (len - dwWriteLen);
            HW_WRITE(write_addr + dwWriteLen, data + dwWriteLen, dwLen);
            dwWriteLen += dwLen;
        }
    }

    /* 更新 size_used（勿用跨分区 static hdr） */
    if (offset + len > self->pPartInfo[index].size_used) {
        self->pPartInfo[index].size_used = offset + len;
    }

    if (self->byManage) {
        hdr.magic = PARTITION_MAGIC;
        hdr.used_size = self->pPartInfo[index].size_used;
        hdr.crc = crc32_checksum((uint8_t *)&hdr, sizeof(hdr) - sizeof(hdr.crc));
        HW_ERASE(start_addr);
        HW_WRITE(start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));
    }

    os_mutex_unlock(self->lock);
    return 0;
}


/*****************************************************
 * @fn       flash_partition_read
 * @brief    storage partition read
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
static int flash_partition_read(STORAGE_CTRL_T *self, uint8_t index, uint32_t offset, uint8_t *data, uint32_t len)
{
    PartitionHeader hdr = {0};
    uint32_t start_addr = self->pPartInfo[index].start_addr;
    uint32_t read_addr = start_addr + offset; /* 数据区起始 + offset */

    /* 入参校验 */
    CUSTOM_ASSERT(NULL == self, return -1);
    CUSTOM_ASSERT(NULL == data, return -1);

    /* SPI FLASH 等介质需要偏移管理头 */
    if(self->byManage)
    {
        read_addr += PARTITION_HEADER_SIZE;
    }

    STOR_INFO("%d-%s read_addr:0x%08x offset:%d len:%d\n",index,self->pPartInfo[index].name, read_addr, offset, len);

    /* 读取范围校验,       目前仅对spi flash 生效       */
    if(self->byManage)
    {
        /* 读取头，获取合法已用长度 */
        HW_READ(start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));

        /* 读起始地址超出已用范围 */
        CUSTOM_ASSERT(offset > hdr.used_size, return -1);

        if (offset + len > hdr.used_size)
        {
            /* 自动截断 */
            len = hdr.used_size - offset;
            STOR_INFO("hdr.used_size:%d offset:%d, len:%d\n", hdr.used_size, offset, len);
        }
    }

    /* 读取正文 */
    HW_READ(read_addr, data, len);

    return len;
}


/*****************************************************
 * @fn       flash_partition_write_and_verify
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
static int flash_partition_write_and_verify(STORAGE_CTRL_T *self, uint8_t index, uint32_t offset, uint8_t *data, uint32_t len)
{
    uint8_t verify_buf[256] = {0};
    uint32_t remaining = len;
    uint32_t addr = offset;
    uint32_t chunk = 0;

    /* 写入数据 */
    if(flash_partition_write(self, index, offset, data, len))
    {
        os_debug("flash_partition_write err\n");
        return -1;
    }

    /* 循环读取并校验 */
    while (remaining > 0)
    {
        chunk = remaining > sizeof(verify_buf) ? sizeof(verify_buf) : remaining;
        if (-1 == flash_partition_read(self, index, addr, verify_buf, chunk))
        {
            os_debug("flash_partition_read err\n");
            return -1;
        }
        /* 内存比较 */
        if (memcmp(data + (addr - offset), verify_buf, chunk) != 0)
        {
            os_debug("Mismatch at addr: 0x%08x\n", addr);
            os_debug("expect:\n");
            flash_data_print(data + (addr - offset), chunk);
            os_debug("actual:\n");
            flash_data_print(verify_buf, chunk);
            os_debug("remaining:%d\n", remaining);
            return -1;
        }
        addr += chunk;
        remaining -= chunk;
    }
    return 0;
}


/*****************************************************
 * @fn       FlashPartition_Init
 * @brief    storage partition init
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void FlashPartition_Init(STORAGE_CTRL_PTR self, STORAGE_PART_INFO_PTR pStPartInfo, uint8_t byPartNum, STORAGE_HW_OPS_PTR pPartHwOps)
{
    uint32_t calc_crc = 0;
    uint32_t addr = 0;
    (void)addr;
    uint32_t end_addr = 0;
    (void)end_addr;
    PartitionHeader hdr = {0};
    uint8_t i = 0;

    /* 入参校验 */
    CUSTOM_ASSERT(NULL == self, return);
    CUSTOM_ASSERT(NULL == pStPartInfo, return);
    CUSTOM_ASSERT(NULL == pPartHwOps, return);

    os_mutex_init(self->lock);
    self->byPartNum = byPartNum;
    self->pPartInfo = pStPartInfo;
    self->stPartOps.hw_ops = pPartHwOps;
    self->stPartOps.partition_read   = flash_partition_read;
    self->stPartOps.partition_write  = flash_partition_write;
    self->stPartOps.write_verify     = flash_partition_write_and_verify;
    self->stPartOps.partition_erase  = flash_partition_erase;

    /* 目前仅SPI_FLASH支持管理头, 其他存储介质直接return */
    if(!self->byManage)
    {
        return;
    }

    for(i = 0; i < byPartNum; i++)
    {
        memset(&hdr, 0, sizeof(PartitionHeader));

        /* 读取头 */
        HW_READ(self->pPartInfo[i].start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));

        /* crc 用来保护的是头的可靠性 —— 防止断电或异常写操作把头破坏了，造成头信息混乱。
         * 它不是用来保证数据区的完整性的!, 如果管理头保存分区的CRC可能效率很低, 数据的正确性由应用层完成校验
         */
        calc_crc = crc32_checksum((uint8_t *)&hdr, sizeof(hdr) - sizeof(hdr.crc));

        /* 检验分区管理头 */
        if ((hdr.magic == PARTITION_MAGIC) && (hdr.crc == calc_crc))
        {
            /* 有效，直接使用 */
            self->pPartInfo[i].size_used = hdr.used_size;
        }
        else
        {
            /* 头无效：只重建管理头扇区，不要整分区擦除（会误伤 web/config） */
            os_debug("name:%s hdr invalid (magic=0x%08lx crc=0x%08lx), reset header only\n",
                      self->pPartInfo[i].name,
                      (unsigned long)hdr.magic,
                      (unsigned long)hdr.crc);

            os_mutex_lock(self->lock);
            hdr.magic = PARTITION_MAGIC;
            hdr.used_size = 0;
            hdr.crc = crc32_checksum((uint8_t *)&hdr, sizeof(hdr) - sizeof(hdr.crc));
            HW_ERASE(self->pPartInfo[i].start_addr);
            HW_WRITE(self->pPartInfo[i].start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));
            self->pPartInfo[i].size_used = 0;
            os_mutex_unlock(self->lock);
            os_debug("new hdr.crc:0x%08x\n", hdr.crc);
        }
#if STORAGE_MNG_DEBUG
        os_printf(
            KERN_INFO"%-10s   addr:0x%08x  size:0x%-8x  size_used:%-6d\n", 
            self->pPartInfo[i].name,
            self->pPartInfo[i].start_addr,
            self->pPartInfo[i].size,
            self->pPartInfo[i].size_used
        );
#endif
    }
    return;
}


/*****************************************************
 * @fn       dts_apply_partitions
 * @brief    用设备树(dts)分区表覆盖 spi_flash_table 的 addr/size/flags
 * @note     dts 未描述的分区保持代码默认值(前向兼容); 返回应用到分区数
 *****************************************************/
int dts_apply_partitions(void)
{
    int i;
    int applied = 0;
    const dts_ctx_t *ctx = dts_ctx();

    if (ctx == NULL || ctx->hdr == NULL || ctx->nodes == NULL) {
        os_debug("dts_apply: dts 未加载\n");
        return -1;
    }

    for (i = 0; i < SPI_FLASH_PART_MAX; i++) {
        const dts_node_t *n = dts_find_by_name(spi_flash_table[i].name);
        uint32_t crc = 0;

        if (n == NULL || n->reg_size == 0) {
            os_debug("dts_apply: 分区 '%s' 未在 dts 中描述, 保持代码默认\n",
                     spi_flash_table[i].name);
            continue;
        }
        spi_flash_table[i].start_addr = n->reg_addr;
        spi_flash_table[i].size       = n->reg_size;
        spi_flash_table[i].flags      = 0;
        if (dts_prop_u32(n, "crc", &crc) == 0 && crc) {
            spi_flash_table[i].flags |= PART_CRCCHECK_EN;
        }
        applied++;
    }
    return applied;
}


/*****************************************************
 * @fn       flash_data_print
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void flash_data_print(uint8_t *addr, uint16_t len)
{
    uint32_t i = 0;
    uint8_t  j = 0;
    uint8_t byLineCnt = 16;

    printf("\n");
#if FLASH_DATA_LITTLE_END
    printf("---------------------------- little end + ASCII -----------------------------\n");
    printf(" ADDR    | RAW HEX BYTES                                    | ASCII          |\n");
    printf("-----------------------------------------------------------------------------\n");
    // 先打印小端原始字节 + ASCII（小段多行）
    for (i = 0; i < len; i += byLineCnt)
    {
        printf("%08X", (unsigned int)(uint32_t)(addr + i));
        printf(" | ");

        for (j = 0; j < byLineCnt; j++)
        {
            printf("%02X ", (i + j < len) ? addr[i + j] : 0x00);
            if(7 == j)
            {
                printf(" ");
            }
        }
        printf("|");

        for (j = 0; j < byLineCnt; j++)
        {
            if (i + j < len)
            {
               char c = addr[i + j];
               printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
            else
               printf(" ");
        }

        printf("|\n");
    }
#else
    // 空一行分隔
    printf("\n");

    // 再单独打印大端翻转视图（32-bit 一组，多行）
    printf("---------------------------- big end + ASCII    -----------------------------\n");
    printf(" ADDR    | RAW HEX BYTES                                    | ASCII          |\n");
    printf("-----------------------------------------------------------------------------\n");
    for (i = 0; i < len; i += byLineCnt)
    {
        printf("%08X", (unsigned int)(uint32_t)(addr + i));
        printf(" | ");

        for (j = 0; j < byLineCnt; j += 4)
        {
            uint8_t b0 = (i + j + 0 < len) ? addr[i + j + 0] : 0x00;
            uint8_t b1 = (i + j + 1 < len) ? addr[i + j + 1] : 0x00;
            uint8_t b2 = (i + j + 2 < len) ? addr[i + j + 2] : 0x00;
            uint8_t b3 = (i + j + 3 < len) ? addr[i + j + 3] : 0x00;

            printf("%02X %02X %02X %02X ", b3, b2, b1, b0);
            if(0 == j)
            {
                printf(" ");
            }
        }

        printf(" |");

        for (j = 0; j < byLineCnt; j++)
        {
           if (i + j < len)
           {
               char c = addr[i + j];
               printf("%c", (c >= 32 && c <= 126) ? c : '.');
           }
           else
               printf(" ");
        }

        printf("|\n");

    }

#endif
    printf("-----------------------------------------------------------------------------\n");
    return;
}


void hex_dump(uint8_t part, uint32_t offset, uint32_t len, uint8_t dev_id)
{
    uint8_t chunk[256];
    uint32_t remain;
    uint32_t cur;
    uint32_t n;
    uint32_t phys;
    STORAGE_CTRL_T *self = &g_stSpiFlashPart;
    (void)dev_id;

    if (part >= SPI_FLASH_PART_MAX || len == 0) {
        return;
    }
    if (offset >= spi_flash_table[part].size) {
        return;
    }
    if (offset + len > spi_flash_table[part].size) {
        len = spi_flash_table[part].size - offset;
    }

    /* 调试转储绕过 used_size：管理头只记录上次写入长度，截断后会把栈上残留 JSON 打出来 */
    remain = len;
    cur = offset;
    while (remain > 0) {
        n = (remain > sizeof(chunk)) ? sizeof(chunk) : remain;
        phys = spi_flash_table[part].start_addr + cur;
        if (self->byManage) {
            phys += PARTITION_HEADER_SIZE;
        }
        memset(chunk, 0, sizeof(chunk));
        HW_READ(phys, chunk, n);
        flash_data_print(chunk, (uint16_t)n);
        cur += n;
        remain -= n;
    }
}


/* end */

