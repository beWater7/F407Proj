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
#include <stdlib.h>
#include "flash_manage.h"
#include "bsp_internalFlash.h"
#include "iap.h"
#include "crc.h"

extern STORAGE_HW_OPS_T spi_flash_ops; 
extern STORAGE_HW_OPS_T internal_flash_ops;

extern STORAGE_CTRL_T g_stSpiFlashPart;
extern STORAGE_CTRL_T g_stInternalFlashPart; 

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

    // HW_READ(self->pPartInfo[index].start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));
    // STOR_PRINT(" start_addr :%08x, hdr.used_size :%d \r\n", start_addr, hdr.used_size);
    if(SPI_FLASH_DEV_ID == self->dev.dev_id)
    {
        /* SPI flash */
        start_addr += PARTITION_HEADER_SIZE;
        /* end_addr 不能加管理头偏移, 因为会造成误擦除 */
        //end_addr += PARTITION_HEADER_SIZE;
        sector_start = start_addr & ~(SECTOR_SIZE - 1); // 向下对齐
        if(end_addr == self->pPartInfo[index].start_addr + self->pPartInfo[index].size)
        {
            end_addr -= 1;
        }
        sector_end = (end_addr) & ~(SECTOR_SIZE - 1);
        dwUnitLen = SECTOR_SIZE;
    }
    else
    {
        /* 内部flash */
        sector_start = GetSector(start_addr);
        sector_end  = GetSector(end_addr-1);
        dwUnitLen = 8;
    }

    STOR_PRINT("erase sector start:0x%x end:0x%x dwUnitLen:%d\n", sector_start,sector_end,dwUnitLen);

    /* 擦除指定区域的数据 */
    for(addr = sector_start; addr <= sector_end; addr += dwUnitLen)
    {
        HW_ERASE(addr); // 擦除扇区
    }

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
    // 计算需要擦除的范围
    PartitionHeader hdr = {0};
    uint32_t start_addr = self->pPartInfo[index].start_addr;
    uint32_t write_addr = start_addr + offset;  /* 数据区起始 + offset */
    uint32_t end_addr = 0;
    uint32_t new_end = 0;
    uint32_t dwWriteLen = 0;
    uint32_t dwLen = 0;

    /* 入参校验 */
    CUSTOM_ASSERT(NULL == self, return -1);
    CUSTOM_ASSERT(NULL == data, return -1);
    CUSTOM_ASSERT(0 == len, return -1);

    /* SPI FLASH 等介质需要偏移管理头 */
    if(SPI_FLASH_DEV_ID == self->dev.dev_id)
    {
        write_addr += PARTITION_HEADER_SIZE;
    }
    else
    {
        /* 校验写入范围：只允许写到数据区 */
        CUSTOM_ASSERT(offset + len > self->pPartInfo[index].size - PARTITION_HEADER_SIZE , return -1);  
    }

    end_addr = write_addr + len;

    /* offset为0表示覆盖写入,写入前擦除范围内数据 , offset不为0追加写入不进行擦除 */
    if(0 == offset)
    {
        STOR_PRINT("Partition erase addr: [0x%08x ~ 0x%08x]\n", start_addr, end_addr);
        if(flash_partition_erase(self, index, start_addr, end_addr))
        {
            os_debug("partition_erase err!\n");
            return -1;
        }
    }

    STOR_PRINT("Write: write_addr=0x%08x, offset=0x%08x, len=%d\n", write_addr, offset, len);

    /* 分块写入, 否则固件数据大可能写入失败 */
    while(dwWriteLen < len)
    {
        dwLen = (len - dwWriteLen) > 256 ? 256:(len-dwWriteLen);
        HW_WRITE(write_addr + dwWriteLen, data + dwWriteLen, dwLen);
        dwWriteLen += dwLen;
        #if USE_PROCESS_BAR
        /* 写入固件时展示进度 */
        if((PART_FW1 == index || PART_FW2 == index) && INTERNAL_FLASH_DEV_ID == self->dev.dev_id)
        {
            PrintProgressBar(dwWriteLen, len);            
        }
        #endif
    }

    /* === 是否需要更新 used_size 并重写头 === */
    new_end = offset + len;
    if (new_end > hdr.used_size)
    {
        /* 写入新头 */
        hdr.magic = PARTITION_MAGIC;
        hdr.used_size = new_end;
        hdr.crc = crc32_checksum((uint8_t *)&hdr, sizeof(hdr) - sizeof(hdr.crc));

        STOR_PRINT("after write hdr.used_size:%d hdr.crc:0x%08x\n",hdr.used_size,hdr.crc);

        self->pPartInfo[index].size_used = hdr.used_size;

        /* 注意！写头必须是头扇区的地址，不要偏移！ */
        if(self->pPartInfo[index].byManage)
        {
            HW_ERASE(start_addr);  // 擦头扇区
            HW_WRITE(start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));
        }
    }

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
    int32 dwTmpLen = 0;
    PartitionHeader hdr = {0};
    uint32_t start_addr = self->pPartInfo[index].start_addr;
    uint32_t read_addr = start_addr + offset; /* 数据区起始 + offset */

    /* 入参校验 */
    CUSTOM_ASSERT(NULL == self, return -1);
    CUSTOM_ASSERT(NULL == data, return -1);

    /* SPI FLASH 等介质需要偏移管理头 */
    if(SPI_FLASH_DEV_ID == self->dev.dev_id)
    {
        read_addr += PARTITION_HEADER_SIZE;
    }

    STOR_PRINT("%d-%s read_addr:0x%08x offset:%d len:%d\n",index,self->pPartInfo[index].name, read_addr, offset, len);

    /* 读取范围校验,   目前仅对spi flash 生效    */
    if(self->pPartInfo[index].byManage)
    {
        /* 读取头，获取合法已用长度 */
        HW_READ(start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));
        #if 1
        /* 读起始地址超出已用范围 */
        if(offset >= hdr.used_size)
        {
            os_debug("offset:%d > hdr.used_size:%d\r\n", offset, hdr.used_size);
            //return -1;
        }

        if (offset + len > hdr.used_size)
        {
            /* 自动截断 */
            dwTmpLen = hdr.used_size - offset;
            //STOR_PRINT("hdr.used_size:%d offset:%d, len:%d\n", hdr.used_size, offset, len);
            os_printf(KERN_WARN"hdr.used_size:%d offset:%d, len:%d\n", hdr.used_size, offset, dwTmpLen);
            if(dwTmpLen <= 0)
            {
                os_debug("hdr.used_size:%d offset:%d, dwTmpLen:%d\r\n", hdr.used_size, offset, dwTmpLen);
                return -1;
            }
            len = dwTmpLen;
        }
        #endif
    }

    //printf("read_addr:%08x len:%d\n", read_addr, len);
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
 * @fn       PartitionErase
 * @brief    storage partition data erase
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
uint32 PartitionErase(uint8_t index, uint32_t offset, uint8_t SectorNum, uint8_t id)
{
    uint8_t  i = 0;
    uint32_t dwAddr = 0;
    switch (id)
    {
        case SPI_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= SPI_FLASH_PART_MAX, break);
            dwAddr = spi_flash_table[index].start_addr + offset;
            os_mutex_lock(g_stSpiFlashPart.storage_mutex);
            for(i = 0; i < SectorNum; i ++)
            {
                STOR_PRINT("erase addr:%08x\n", dwAddr);
                spi_flash_ops.hw_erase(dwAddr);
                dwAddr += SECTOR_SIZE;
            }
            os_mutex_unlock(g_stSpiFlashPart.storage_mutex);
            break;
        case INTERNAL_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= INTERNAL_FLASH_PART_MAX, break);
            dwAddr = internal_flash_table[index].start_addr + offset;
            dwAddr = GetSector(dwAddr);
            os_mutex_lock(g_stInternalFlashPart.storage_mutex);
            for(i = 0; i < SectorNum; i++)
            {
                internal_flash_ops.hw_erase(dwAddr);
                dwAddr += 8;
            }
            os_mutex_unlock(g_stInternalFlashPart.storage_mutex);
            break;
        default:
            break;
    }
    return dwAddr;
}


/*****************************************************
 * @fn       getPartitionAddr
 * @brief    storage partition data write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
uint32 PartitionWrite(uint8_t index, uint32_t offset, uint8_t* data, uint32_t len, uint8_t id)
{
    uint8_t  i = 0;
    uint8_t  bySectorNum = 0;
    uint32_t dwEraseAddr = 0;
    uint32_t dwAddr = 0;

    CUSTOM_ASSERT(NULL == data, return 1);

    switch (id)
    {
        case SPI_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= SPI_FLASH_PART_MAX, return 1);
            dwAddr = spi_flash_table[index].start_addr + offset;
            dwEraseAddr = dwAddr & ~(SECTOR_SIZE - 1);  // 向下对齐
            // bySectorNum = len/SECTOR_SIZE;
            // if(len % SECTOR_SIZE)
            // {
            //     bySectorNum++;
            // }
            bySectorNum = ((dwAddr + len - dwEraseAddr) + SECTOR_SIZE - 1) >> SECTOR_SHIFT;
            STOR_PRINT("bySectorNum:%d\r\n", bySectorNum);
            os_mutex_lock(g_stSpiFlashPart.storage_mutex);
            #if 0
            for(i = 0; i < bySectorNum; i++)
            {
                STOR_PRINT("dwEraseAddr:%08x\r\n", dwEraseAddr);
                spi_flash_ops.hw_erase(dwEraseAddr);
                dwEraseAddr += SECTOR_SIZE;
            }
            #endif
            spi_flash_ops.hw_write(dwAddr, data, len);
            os_mutex_unlock(g_stSpiFlashPart.storage_mutex);
            break;
        case INTERNAL_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= INTERNAL_FLASH_PART_MAX,  return 1);
            dwAddr = internal_flash_table[index].start_addr + offset;
            os_mutex_lock(g_stInternalFlashPart.storage_mutex);
            internal_flash_ops.hw_write(dwAddr, data, len);
            os_mutex_unlock(g_stInternalFlashPart.storage_mutex);
            break;
        default:
            break;
    }

    return 0;
}


/*****************************************************
 * @fn       PartitionRead
 * @brief    storage partition data read
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
uint32 PartitionRead(uint8_t index, uint32_t offset, uint8_t* data, uint32_t len, uint8_t id)
{
    uint32_t dwAddr = 0;

    CUSTOM_ASSERT(NULL == data, return 1);

    switch (id)
    {
        case SPI_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= SPI_FLASH_PART_MAX, break);
            dwAddr = spi_flash_table[index].start_addr + offset;
            os_mutex_lock(g_stSpiFlashPart.storage_mutex);
            STOR_PRINT("read addr:%08x, read len:%d\n", dwAddr, len);
            spi_flash_ops.hw_read(dwAddr, data, len);
            os_mutex_unlock(g_stSpiFlashPart.storage_mutex);
            break;
        case INTERNAL_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= INTERNAL_FLASH_PART_MAX, break);
            dwAddr = internal_flash_table[index].start_addr + offset;
            os_mutex_lock(g_stInternalFlashPart.storage_mutex);
            internal_flash_ops.hw_read(dwAddr, data, len);
            os_mutex_unlock(g_stInternalFlashPart.storage_mutex);
            break;
        default:
            break;
    }

    return 0;
}


/*****************************************************
 * @fn       getPartitionAddr
 * @brief    storage partition init
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
uint32 getPartitionAddr(uint8_t index, uint8_t id)
{
    uint32_t dwAddr = 0;
    switch (id)
    {
        case SPI_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= SPI_FLASH_PART_MAX, break);
            dwAddr = spi_flash_table[index].start_addr;
            break;
        case INTERNAL_FLASH_DEV_ID:
            CUSTOM_ASSERT(index >= INTERNAL_FLASH_PART_MAX, break);
            dwAddr = internal_flash_table[index].start_addr;
            break;
        default:
            break;
    }
    return dwAddr;
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
    uint32_t end_addr = 0;
    PartitionHeader hdr = {0};
    uint8_t i = 0;

    /* 入参校验 */
    CUSTOM_ASSERT(NULL == self, return);
    CUSTOM_ASSERT(NULL == pStPartInfo, return);
    CUSTOM_ASSERT(NULL == pPartHwOps, return);

    os_mutex_init(self->storage_mutex);
    self->byPartNum = byPartNum;
    self->pPartInfo = pStPartInfo;
    self->stPartOps.hw_ops = pPartHwOps;
    self->stPartOps.partition_read   = flash_partition_read;
    self->stPartOps.partition_write  = flash_partition_write;
    self->stPartOps.write_verify     = flash_partition_write_and_verify;
    self->stPartOps.partition_erase  = flash_partition_erase;

    /* 目前仅SPI_FLASH支持管理头, 其他存储介质直接return */
    if(!(SPI_FLASH_DEV_ID == self->dev.dev_id))
    {
        return;
    }

    for(i = 0; i < byPartNum; i++)
    {
        memset(&hdr, 0, sizeof(PartitionHeader));

        /* 不支持crc校验的part直接下一次循环 */
        if(!self->pPartInfo[i].byManage)
        {
            continue;
        }
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
            #if 0
                        /* 管理头错误直接全擦除 */
            os_debug("name:%s hdr.magic: %d hdr.crc: %d Partition header CRC error, 
                      erasing whole partition...\n", 
                      self->pPartInfo[i].name, hdr.magic, hdr.crc);         
            addr = self->pPartInfo[i].start_addr;
            end_addr = addr + self->pPartInfo[i].size;
            if(flash_partition_erase(self, i, addr, end_addr))
            {
                os_debug("partition_erase err!\n");
                return;
            }
            
            /* 写入新头 */
            hdr.magic = PARTITION_MAGIC;
            hdr.used_size = 0;
            hdr.crc = crc32_checksum((uint8_t *)&hdr, sizeof(hdr) - sizeof(hdr.crc));

            //flash_data_print((uint8_t *)0x080A0000, 128);
            HW_WRITE(self->pPartInfo[i].start_addr, (uint8_t *)&hdr, sizeof(PartitionHeader));
            //flash_data_print((uint8_t *)0x080A0000, 128);
            os_debug("new hdr.crc:0x%08x\n", hdr.crc);
            #endif
            self->pPartInfo[i].size_used = 0;
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
 * @fn       flash_data_print
 * @brief    storage partition write
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void flash_data_print(uint8_t *addr, uint32_t len)
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


/*****************************************************
 * @fn       data_dump
 * @brief    spi_flash data dump
 * @note     N/A
 * @param    
 * @retval   N/A
 *****************************************************/
void hex_dump(uint8_t index, uint32_t offset, uint32_t len, uint8_t id)
{
    uint8_t *data = NULL;
    uint32_t dwAddr = 0;

    CUSTOM_ASSERT(0 == len, return);
    data = (uint8_t*)malloc(len);
    CUSTOM_ASSERT(!data, return);

    switch (id)
    {
        case SPI_FLASH_DEV_ID:
            dwAddr = spi_flash_table[index].start_addr + offset;
            printf("partName: %s\r\n", g_stSpiFlashPart.pPartInfo[index].name);
            os_mutex_lock(g_stSpiFlashPart.storage_mutex);
            spi_flash_ops.hw_read(dwAddr, data, len);
            os_mutex_unlock(g_stSpiFlashPart.storage_mutex);
            break;
        case INTERNAL_FLASH_DEV_ID:
            dwAddr = internal_flash_table[index].start_addr + offset;
            printf("partName: %s\r\n", g_stSpiFlashPart.pPartInfo[index].name);
            os_mutex_lock(g_stInternalFlashPart.storage_mutex);
            internal_flash_ops.hw_read(dwAddr, data, len);
            os_mutex_unlock(g_stInternalFlashPart.storage_mutex);
            break;
        default:
            break;
    }

    flash_data_print(data, len);
    free(data);
}
/* end */

